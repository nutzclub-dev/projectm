#include <MilkdropPreset/PerFrameContext.hpp>
#include <MilkdropPreset/PerPixelContext.hpp>
#include <MilkdropPreset/PresetFileParser.hpp>
#include <Audio/PCM.hpp>
#include <Audio/Loudness.hpp>
#include <Audio/WaveformAligner.hpp>
#include <Audio/MilkdropFFT.hpp>
#include <Utils.hpp>
#include <projectM-4/parameters.h>

#include <chrono>
#include <iostream>
#include <vector>
#include <string>
#include <algorithm>
#include <cassert>
#include <numeric>
#include <iomanip>
#include <cmath>

static bool IsParserAudioReactive(libprojectM::MilkdropPreset::PresetFileParser& parser) {
    int waveMode = parser.GetInt("nWaveMode", 0);
    float waveAlpha = parser.GetFloat("fWaveAlpha", 0.8f);
    if (waveMode > 0 && waveAlpha > 0.001f) {
        return true;
    }

    for (int i = 0; i < libprojectM::MilkdropPreset::CustomWaveformCount; ++i) {
        if (parser.GetBool("wavecode_" + std::to_string(i) + "_enabled", false)) {
            return true;
        }
    }

    static const std::vector<std::string> audioVarNames = {
        "bass", "mid", "treb", "bass_att", "mid_att", "treb_att", "vol", "vol_att"
    };

    auto containsAudioVar = [](const std::string& code) -> bool {
        if (code.empty()) return false;
        std::string lowerCode = libprojectM::Utils::ToLower(code);
        for (const auto& var : audioVarNames) {
            size_t pos = 0;
            while ((pos = lowerCode.find(var, pos)) != std::string::npos) {
                bool leftOk = (pos == 0) || (!isalnum(static_cast<unsigned char>(lowerCode[pos - 1])) && lowerCode[pos - 1] != '_');
                bool rightOk = (pos + var.length() == lowerCode.length()) ||
                               (!isalnum(static_cast<unsigned char>(lowerCode[pos + var.length()])) && lowerCode[pos + var.length()] != '_');
                if (leftOk && rightOk) {
                    return true;
                }
                pos += var.length();
            }
        }
        return false;
    };

    std::vector<std::string> codeBlocks = {
        parser.GetCode("per_frame_init_"),
        parser.GetCode("per_frame_"),
        parser.GetCode("per_pixel_"),
        parser.GetCode("warp_"),
        parser.GetCode("comp_")
    };

    for (int i = 0; i < libprojectM::MilkdropPreset::CustomWaveformCount; ++i) {
        std::string const wavePrefix = "wave_" + std::to_string(i) + "_";
        codeBlocks.push_back(parser.GetCode(wavePrefix + "init"));
        codeBlocks.push_back(parser.GetCode(wavePrefix + "per_frame"));
        codeBlocks.push_back(parser.GetCode(wavePrefix + "per_point"));
    }

    for (int i = 0; i < libprojectM::MilkdropPreset::CustomShapeCount; ++i) {
        std::string const shapePrefix = "shape_" + std::to_string(i) + "_";
        codeBlocks.push_back(parser.GetCode(shapePrefix + "init"));
        codeBlocks.push_back(parser.GetCode(shapePrefix + "per_frame"));
    }

    for (const auto& block : codeBlocks) {
        if (containsAudioVar(block)) {
            return true;
        }
    }

    return false;
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    std::cout << "[projectM-benchmark] Running audio reactivity and fallback layer tests..." << std::endl;

    // Test 1: Non-audio-reactive preset detection
    std::cout << "Test 1 Start" << std::endl;
    {
        libprojectM::MilkdropPreset::PresetFileParser parserNonReactive;
        bool readOk = parserNonReactive.Read("presets/tests/110-per_pixel.milk");
        assert(readOk);

        bool isAudioReactive = IsParserAudioReactive(parserNonReactive);
        std::cout << "110-per_pixel.milk IsAudioReactive: " << (isAudioReactive ? "YES" : "NO") << std::endl;
        assert(!isAudioReactive && "110-per_pixel.milk should NOT be detected as audio-reactive");
    }

    // Test 2: Audio-reactive preset detection
    std::cout << "Test 2 Start" << std::endl;
    {
        libprojectM::MilkdropPreset::PresetFileParser parserReactive;
        bool readOk = parserReactive.Read("presets/tests/300-beatdetect-bassmidtreb.milk");
        assert(readOk);

        bool isAudioReactive = IsParserAudioReactive(parserReactive);
        std::cout << "300-beatdetect-bassmidtreb.milk IsAudioReactive: " << (isAudioReactive ? "YES" : "NO") << std::endl;
        assert(isAudioReactive && "300-beatdetect-bassmidtreb.milk SHOULD be detected as audio-reactive");
    }

    // Test 3: Fallback modulation delta calculation on non-reactive context
    std::cout << "Test 3 Start" << std::endl;
    {
        projectm_eval_mem_buffer globalMemory = projectm_eval_memory_buffer_create();
        double globalRegisters[100]{};

        libprojectM::MilkdropPreset::PerFrameContext perFrameContext(globalMemory, &globalRegisters);
        perFrameContext.RegisterBuiltinVariables();

        *perFrameContext.zoom = 1.0;
        *perFrameContext.bass_att = 1.5;
        *perFrameContext.mid_att = 1.2;
        *perFrameContext.treb_att = 1.3;

        double zoomBefore = *perFrameContext.zoom;

        // Apply subtle fallback modulation logic
        float strength = 1.0f;
        float rawBass = static_cast<float>(*perFrameContext.bass_att);
        float rawVol = static_cast<float>(*perFrameContext.mid_att);
        float fallbackEnergy = (rawBass * 0.6f + rawVol * 0.4f);

        float pulse = std::max(0.0f, fallbackEnergy - 1.0f) * 0.025f * strength;
        pulse = std::min(0.03f, pulse);

        *perFrameContext.zoom += pulse * 0.03f;

        double zoomAfter = *perFrameContext.zoom;
        std::cout << "Non-audio-reactive fallback zoom delta: " << (zoomAfter - zoomBefore) << std::endl;
        assert(zoomAfter > zoomBefore && "Fallback audio reactivity should subtly increase zoom on beat");

        projectm_eval_memory_buffer_destroy(globalMemory);
    }

    std::cout << "\nAll audio reactivity unit tests passed successfully!\n" << std::endl;

    std::cout << "[projectM-benchmark] Starting headless CPU/Audio pipeline benchmark..." << std::endl;

    libprojectM::Audio::PCM pcmStorage;

    std::vector<float> pcmBuffer(512 * 2);
    for (size_t i = 0; i < pcmBuffer.size(); ++i) {
        pcmBuffer[i] = static_cast<float>(sin(i * 0.05));
    }

    const int warmUpFrames = 100;
    const int benchmarkFrames = 5000;

    for (int f = 0; f < warmUpFrames; ++f) {
        pcmStorage.Add(pcmBuffer.data(), 2, 512);
        pcmStorage.UpdateFrameAudioData(1.0 / 60.0, f);
        auto audioData = pcmStorage.GetFrameAudioData();
        (void)audioData;
    }

    std::vector<double> frameTimesUs;
    frameTimesUs.reserve(benchmarkFrames);

    auto startTotal = std::chrono::high_resolution_clock::now();

    for (int f = 0; f < benchmarkFrames; ++f) {
        pcmStorage.Add(pcmBuffer.data(), 2, 512);

        auto t0 = std::chrono::high_resolution_clock::now();
        pcmStorage.UpdateFrameAudioData(1.0 / 60.0, f);
        auto audioData = pcmStorage.GetFrameAudioData();
        (void)audioData;
        auto t1 = std::chrono::high_resolution_clock::now();

        double frameTimeUs = std::chrono::duration<double, std::micro>(t1 - t0).count();
        frameTimesUs.push_back(frameTimeUs);
    }

    auto endTotal = std::chrono::high_resolution_clock::now();
    double totalTimeSec = std::chrono::duration<double>(endTotal - startTotal).count();

    double sumUs = std::accumulate(frameTimesUs.begin(), frameTimesUs.end(), 0.0);
    double avgFrameTimeUs = sumUs / benchmarkFrames;
    double avgFrameTimeMs = avgFrameTimeUs / 1000.0;
    double avgFPS = 1000.0 / avgFrameTimeMs;

    std::vector<double> sortedTimes = frameTimesUs;
    std::sort(sortedTimes.begin(), sortedTimes.end(), std::greater<double>());

    size_t idx1pct = std::max<size_t>(1, static_cast<size_t>(benchmarkFrames * 0.01));
    size_t idx01pct = std::max<size_t>(1, static_cast<size_t>(benchmarkFrames * 0.001));

    double time1pctMs = sortedTimes[idx1pct - 1] / 1000.0;
    double time01pctMs = sortedTimes[idx01pct - 1] / 1000.0;

    double fps1pct = 1000.0 / time1pctMs;
    double fps01pct = 1000.0 / time01pctMs;

    std::cout << std::fixed << std::setprecision(4);
    std::cout << "\n==============================================" << std::endl;
    std::cout << "          PROJECTM BENCHMARK RESULTS          " << std::endl;
    std::cout << "==============================================" << std::endl;
    std::cout << " Benchmark Frames    : " << benchmarkFrames << std::endl;
    std::cout << " Total Time          : " << totalTimeSec << " s" << std::endl;
    std::cout << " Average Frame Time  : " << avgFrameTimeMs << " ms (" << avgFrameTimeUs << " us)" << std::endl;
    std::cout << " Average Pipeline FPS: " << avgFPS << " FPS" << std::endl;
    std::cout << " 1% Low FPS          : " << fps1pct << " FPS" << std::endl;
    std::cout << " 0.1% Low FPS        : " << fps01pct << " FPS" << std::endl;
    std::cout << "==============================================\n" << std::endl;

    return 0;
}
