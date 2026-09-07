#pragma once

#include "KWeightingFilter.h"
#include <vector>
#include <cmath>
#include <array>
#include <algorithm>
#include <mutex>

namespace autolevel::dsp {

constexpr float SILENCE_LUFS = -120.0f;
constexpr float ABSOLUTE_GATE_LUFS = -70.0f;
constexpr float RELATIVE_GATE_LU = 10.0f;
constexpr int HISTOGRAM_BINS = 97; // -120 dBFS to -24 dBFS (1 dB per bin)
constexpr float HISTOGRAM_MIN_DB = -120.0f;

struct LoudnessReadings {
    float momentaryLUFS = SILENCE_LUFS;
    float shortTermLUFS = SILENCE_LUFS;
    float integratedLUFS = SILENCE_LUFS;
    float peakDbfs = SILENCE_LUFS;
};

class LoudnessMeter {
public:
    LoudnessMeter() = default;

    void prepare(double sampleRate) {
        m_sampleRate = sampleRate;
        m_kFilter.prepare(sampleRate);

        // 100 ms step (400 ms block with 75% overlap)
        m_stepSamples = static_cast<size_t>(sampleRate * 0.1);
        m_blockSamples = static_cast<size_t>(sampleRate * 0.4);

        // 3-second buffer for short-term (30 steps of 100ms)
        m_shortTermSteps = 30;
        m_stepEnergies.assign(m_shortTermSteps, 0.0);
        m_stepIndex = 0;

        // 4 steps in 400ms momentary block
        m_momentarySteps = 4;

        reset();
    }

    void reset() {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_kFilter.reset();
        m_currentStepSampleCount = 0;
        m_currentStepEnergySum = 0.0;
        std::fill(m_stepEnergies.begin(), m_stepEnergies.end(), 0.0);
        m_stepIndex = 0;

        std::fill(m_histogram.begin(), m_histogram.end(), 0.0);
        m_blocksIntegrated = 0;
        m_gatedMs = 0;

        m_momentaryLUFS = SILENCE_LUFS;
        m_shortTermLUFS = SILENCE_LUFS;
        m_integratedLUFS = SILENCE_LUFS;
        m_peakDbfs = SILENCE_LUFS;
    }

    void setMemoryHalfLife(float seconds) {
        m_halfLifeSeconds = seconds;
    }

    /**
     * Process an incoming stereo buffer (non-interleaved).
     */
    void process(const float* left, const float* right, size_t numSamples) {
        for (size_t i = 0; i < numSamples; ++i) {
            double l = left[i];
            double r = right[i];

            // True peak tracking
            float absMax = static_cast<float>(std::max(std::abs(l), std::abs(r)));
            float peakDb = (absMax > 1e-6f) ? 20.0f * std::log10(absMax) : SILENCE_LUFS;
            if (peakDb > m_peakDbfs) {
                m_peakDbfs = peakDb;
            } else {
                // Peak decay ~3 dB per second
                m_peakDbfs -= (3.0f / static_cast<float>(m_sampleRate));
                if (m_peakDbfs < SILENCE_LUFS) m_peakDbfs = SILENCE_LUFS;
            }

            // K-weighting filter
            double fL = 0.0;
            double fR = 0.0;
            m_kFilter.processSample(l, r, fL, fR);

            // Channel power sum (equal weighting for L and R)
            double sampleEnergy = fL * fL + fR * fR;
            m_currentStepEnergySum += sampleEnergy;
            m_currentStepSampleCount++;

            if (m_currentStepSampleCount >= m_stepSamples) {
                finalizeStep();
            }
        }
    }

    LoudnessReadings getReadings() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return {
            m_momentaryLUFS,
            m_shortTermLUFS,
            m_integratedLUFS,
            m_peakDbfs
        };
    }

    size_t getBlocksIntegrated() const {
        return m_blocksIntegrated;
    }

private:
    void finalizeStep() {
        if (m_currentStepSampleCount == 0) return;

        double stepMeanPower = m_currentStepEnergySum / static_cast<double>(m_currentStepSampleCount);
        m_currentStepSampleCount = 0;
        m_currentStepEnergySum = 0.0;

        std::lock_guard<std::mutex> lock(m_mutex);

        // Store step energy in ring buffer
        m_stepEnergies[m_stepIndex] = stepMeanPower;
        m_stepIndex = (m_stepIndex + 1) % m_shortTermSteps;

        // Compute 400ms momentary loudness (sum of last 4 100ms steps)
        double momentarySum = 0.0;
        for (size_t i = 0; i < m_momentarySteps; ++i) {
            size_t idx = (m_stepIndex + m_shortTermSteps - 1 - i) % m_shortTermSteps;
            momentarySum += m_stepEnergies[idx];
        }
        double momentaryPower = momentarySum / static_cast<double>(m_momentarySteps);
        m_momentaryLUFS = toLUFS(momentaryPower);

        // Compute 3s short-term loudness (sum of last 30 100ms steps)
        double shortTermSum = 0.0;
        for (double p : m_stepEnergies) shortTermSum += p;
        double shortTermPower = shortTermSum / static_cast<double>(m_shortTermSteps);
        m_shortTermLUFS = toLUFS(shortTermPower);

        // Update online histogram for integrated loudness
        updateHistogram(m_momentaryLUFS, 100);
    }

    void updateHistogram(float blockLUFS, long dtMs) {
        if (blockLUFS <= ABSOLUTE_GATE_LUFS) return;

        // Half-life decay
        if (m_halfLifeSeconds > 0.0f) {
            double decay = std::exp(-0.69314718 * (dtMs / 1000.0) / m_halfLifeSeconds);
            for (auto& bin : m_histogram) {
                bin *= decay;
            }
        }

        int bin = static_cast<int>(std::round(blockLUFS - HISTOGRAM_MIN_DB));
        bin = std::clamp(bin, 0, HISTOGRAM_BINS - 1);
        m_histogram[bin] += 1.0;
        m_blocksIntegrated++;
        m_gatedMs += dtMs;

        m_integratedLUFS = computeGatedMean();
    }

    float computeGatedMean() {
        // Step 1: Mean above absolute gate (-70 LUFS)
        float ungated = meanAbove(ABSOLUTE_GATE_LUFS);
        if (ungated <= ABSOLUTE_GATE_LUFS) return SILENCE_LUFS;

        // Step 2: Mean above relative gate (ungated - 10 LU)
        float relativeThreshold = ungated - RELATIVE_GATE_LU;
        return meanAbove(relativeThreshold);
    }

    float meanAbove(float thresholdDb) {
        double powerSum = 0.0;
        double weightSum = 0.0;

        for (int i = 0; i < HISTOGRAM_BINS; ++i) {
            double n = m_histogram[i];
            if (n <= 0.0) continue;

            float centerDb = HISTOGRAM_MIN_DB + static_cast<float>(i);
            if (centerDb < thresholdDb) continue;

            // Power = 10^(dB / 10)
            powerSum += n * std::pow(10.0, centerDb / 10.0);
            weightSum += n;
        }

        if (weightSum <= 0.0) return SILENCE_LUFS;
        return static_cast<float>(10.0 * std::log10(powerSum / weightSum));
    }

    static inline float toLUFS(double meanPower) {
        if (meanPower <= 1e-12) return SILENCE_LUFS;
        // BS.1770-4 offset: -0.691
        return static_cast<float>(-0.691 + 10.0 * std::log10(meanPower));
    }

    double m_sampleRate = 48000.0;
    KWeightingFilter m_kFilter;

    size_t m_stepSamples = 4800;
    size_t m_blockSamples = 19200;
    size_t m_shortTermSteps = 30;
    size_t m_momentarySteps = 4;

    size_t m_currentStepSampleCount = 0;
    double m_currentStepEnergySum = 0.0;
    std::vector<double> m_stepEnergies;
    size_t m_stepIndex = 0;

    // Online 1-dB histogram
    std::array<double, HISTOGRAM_BINS> m_histogram{};
    size_t m_blocksIntegrated = 0;
    long m_gatedMs = 0;
    float m_halfLifeSeconds = 25.0f; // 25 second rolling memory for live DJ sets

    // Cached meter outputs
    mutable std::mutex m_mutex;
    float m_momentaryLUFS = SILENCE_LUFS;
    float m_shortTermLUFS = SILENCE_LUFS;
    float m_integratedLUFS = SILENCE_LUFS;
    float m_peakDbfs = SILENCE_LUFS;
};

} // namespace autolevel::dsp
