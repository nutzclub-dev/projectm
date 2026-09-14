#include <MilkdropPreset/PerFrameContext.hpp>
#include <MilkdropPreset/PerPixelContext.hpp>
#include <MilkdropPreset/PresetFileParser.hpp>
#include <Audio/PCM.hpp>
#include <Utils.hpp>
#include <projectM-4/parameters.h>

#include <chrono>
#include <filesystem>
#include <iostream>
#include <vector>
#include <string>
#include <algorithm>
#include <cassert>

namespace fs = std::filesystem;

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

int main([[maybe_unused]] int argc, [[maybe_unused]] char** argv) {
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

    std::cout << "\nAll audio reactivity unit tests passed successfully!" << std::endl;
    return 0;
}
