#include "../src/dsp/AutoLevelEngine.h"
#include <iostream>
#include <cassert>
#include <cmath>
#include <vector>

using namespace autolevel::dsp;

constexpr double TEST_PI = 3.14159265358979323846;

void testKWeightingSineWave() {
    std::cout << "[TEST] KWeightingFilter 1kHz calibration..." << std::endl;
    KWeightingFilter filter;
    double sampleRate = 48000.0;
    filter.prepare(sampleRate);

    // 1 kHz sine wave with amplitude 1.0 (0 dBFS peak, -3.01 dBFS RMS)
    double freq = 1000.0;
    double sumSqIn = 0.0;
    double sumSqOut = 0.0;
    size_t n = 48000; // 1 second

    for (size_t i = 0; i < n; ++i) {
        double t = static_cast<double>(i) / sampleRate;
        double s = std::sin(2.0 * TEST_PI * freq * t);
        sumSqIn += s * s;

        double outL = 0.0, outR = 0.0;
        filter.processSample(s, s, outL, outR);
        sumSqOut += outL * outL;
    }

    double rmsIn = std::sqrt(sumSqIn / n);
    double rmsOut = std::sqrt(sumSqOut / n);
    double gainDb = 20.0 * std::log10(rmsOut / rmsIn);

    std::cout << "  Input RMS: " << rmsIn << " (-3.01 dBFS)" << std::endl;
    std::cout << "  Output RMS: " << rmsOut << std::endl;
    std::cout << "  K-Weighting gain at 1kHz: " << gainDb << " dB" << std::endl;

    // K-weighting at 1kHz has a slight gain of ~+0.7 dB due to high shelf
    assert(gainDb > 0.0 && gainDb < 1.5);
    std::cout << "  -> PASS: 1kHz K-Weighting response is within specification." << std::endl;
}

void testLR4CrossoverSummation() {
    std::cout << "[TEST] Linkwitz-Riley 6-band crossover flat magnitude test..." << std::endl;
    MultibandCompressor mbc;
    double sampleRate = 48000.0;
    mbc.prepare(sampleRate);

    MBCParams params;
    params.enabled = true;
    params.compressionAmount = 0.0f; // Linear bypass (no compression) -> pure crossover sum

    std::vector<double> testFreqs = { 50.0, 120.0, 300.0, 400.0, 800.0, 1200.0, 2000.0, 3500.0, 6000.0, 8000.0, 14000.0 };

    for (double f : testFreqs) {
        mbc.reset();
        size_t n = 4800; // 100 ms
        std::vector<float> inL(n), inR(n), origL(n);
        for (size_t i = 0; i < n; ++i) {
            float s = static_cast<float>(std::sin(2.0 * TEST_PI * f * (static_cast<double>(i) / sampleRate)));
            inL[i] = s;
            inR[i] = s;
            origL[i] = s;
        }

        mbc.process(inL.data(), inR.data(), n, params);

        // Measure steady state RMS over last 2000 samples
        double sumSqIn = 0.0;
        double sumSqOut = 0.0;
        size_t count = 0;
        for (size_t i = 2800; i < n; ++i) {
            sumSqIn += origL[i] * origL[i];
            sumSqOut += inL[i] * inL[i];
            count++;
        }

        double rmsIn = std::sqrt(sumSqIn / count);
        double rmsOut = std::sqrt(sumSqOut / count);
        double errorDb = 20.0 * std::log10(rmsOut / rmsIn);

        std::cout << "  Freq " << f << " Hz -> RMS error: " << errorDb << " dB" << std::endl;
        assert(std::abs(errorDb) < 0.25);
    }
    std::cout << "  -> PASS: 6-band crossover tree sums to flat magnitude." << std::endl;
}

void testLevelerAndLimiter() {
    std::cout << "[TEST] AutoLevelEngine full pipeline leveling and limiting..." << std::endl;
    AutoLevelEngine engine;
    double sampleRate = 48000.0;
    engine.prepare(sampleRate);

    EngineParameters params;
    params.targetLUFS = -9.0f;
    params.maxBoostDb = 6.0f;
    params.maxCutDb = 12.0f;
    params.ceilingDb = -0.5f;
    params.compressionAmount = 0.5f;

    // Simulate quiet signal (-18 dBFS sine wave) for 3 seconds
    size_t block = 480; // 10ms blocks
    size_t totalBlocks = 300; // 3 seconds

    float maxOutputPeak = 0.0f;
    for (size_t b = 0; b < totalBlocks; ++b) {
        std::vector<float> left(block), right(block);
        for (size_t i = 0; i < block; ++i) {
            float s = 0.125f * static_cast<float>(std::sin(2.0 * TEST_PI * 440.0 * (b * block + i) / sampleRate));
            left[i] = s;
            right[i] = s;
        }

        engine.process(left.data(), right.data(), block, params);

        for (size_t i = 0; i < block; ++i) {
            maxOutputPeak = std::max(maxOutputPeak, std::abs(left[i]));
        }
    }

    auto state = engine.getVisualState();
    std::cout << "  Applied Gain: " << state.appliedGainDb << " dB" << std::endl;
    std::cout << "  Target Gain: " << state.targetGainDb << " dB" << std::endl;
    std::cout << "  Max Output Peak: " << maxOutputPeak << std::endl;

    // Gain should have increased smoothly towards target (positive boost)
    assert(state.appliedGainDb > 0.0f);
    // Safety limiter must ensure peak never exceeds ceiling (-0.5 dBFS = 0.944)
    assert(maxOutputPeak <= 0.95f);

    std::cout << "  -> PASS: Leveler smoothly boosted quiet signal and Limiter guarded ceiling." << std::endl;
}

int main() {
    std::cout << "============================================" << std::endl;
    std::cout << "   AutoLevel DJ DSP Unit Tests              " << std::endl;
    std::cout << "============================================" << std::endl;

    testKWeightingSineWave();
    testLR4CrossoverSummation();
    testLevelerAndLimiter();

    std::cout << "============================================" << std::endl;
    std::cout << "   ALL DSP TESTS PASSED SUCCESSFULLY!       " << std::endl;
    std::cout << "============================================" << std::endl;
    return 0;
}
