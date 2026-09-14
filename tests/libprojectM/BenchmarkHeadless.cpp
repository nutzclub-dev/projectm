#include <Audio/PCM.hpp>
#include <Audio/Loudness.hpp>
#include <Audio/WaveformAligner.hpp>
#include <Audio/MilkdropFFT.hpp>
#include <chrono>
#include <iostream>
#include <vector>
#include <numeric>
#include <algorithm>
#include <iomanip>
#include <cmath>

int main() {
    std::cout << "[projectM-benchmark] Starting headless CPU/Audio pipeline benchmark..." << std::endl;

    libprojectM::Audio::PCM pcmStorage;

    std::vector<float> pcmBuffer(512 * 2);
    for (size_t i = 0; i < pcmBuffer.size(); ++i) {
        pcmBuffer[i] = static_cast<float>(sin(i * 0.05));
    }

    const int warmUpFrames = 100;
    const int benchmarkFrames = 50000;

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
