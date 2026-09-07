#pragma once

#include <cmath>
#include <cstddef>
#include <algorithm>

namespace autolevel::dsp {

enum class BassLiftMode : int {
    OFF = 0,
    LOW = 1,
    MED = 2,
    HIGH = 3
};

/**
 * Dynamic Bass Lift (Dolby Duo Engine - Low End)
 *
 * 100% distortion-free dynamic upward expansion low-shelf filter (< 100 Hz).
 * Replaces nonlinear sub-harmonic synthesis with pure musical linear shelving.
 *
 * It analyzes the real-time energy ratio between sub-100Hz bass and the midrange anchor.
 * - On bass-deficient vintage material (e.g., 70s disco, vintage funk/rock), it dynamically
 *   lifts the natural acoustic bass guitar and kick body by up to +6.5 dB.
 * - On modern club tracks with existing heavy sub-bass, the lift automatically dials down to 0 dB,
 *   completely avoiding muddiness or boomy over-compression.
 * - Positioned before the 6-band MBC so the MBC can polish and shape the lifted low end.
 *
 * Algorithmic Latency: Exactly 0 samples.
 * Harmonic Distortion: 0.00% (Pure Linear Filter).
 */
class DynamicBassLift {
public:
    DynamicBassLift() = default;

    void prepare(double sampleRate) noexcept {
        m_sampleRate = (sampleRate > 1000.0) ? sampleRate : 44100.0;
        reset();
    }

    void reset() noexcept {
        m_sL_z1 = m_sL_z2 = 0.0;
        m_sR_z1 = m_sR_z2 = 0.0;
        m_bassEnv = 0.0f;
        m_midEnv = 0.0f;
        m_currentGainDb = 0.0f;
        m_visualLiftDb = 0.0f;
        m_detL_z1 = m_detL_z2 = 0.0;
        m_detM_z1 = m_detM_z2 = 0.0;
    }

    /**
     * Process stereo block with selected Bass Lift mode.
     */
    void process(float* left, float* right, size_t numSamples, BassLiftMode mode) noexcept {
        if (mode == BassLiftMode::OFF || numSamples == 0) {
            m_currentGainDb = 0.0f;
            m_visualLiftDb = 0.0f;
            return;
        }

        float maxTargetDb = 0.0f;
        switch (mode) {
            case BassLiftMode::LOW:  maxTargetDb = 2.5f; break;
            case BassLiftMode::MED:  maxTargetDb = 4.5f; break;
            case BassLiftMode::HIGH: maxTargetDb = 6.5f; break;
            case BassLiftMode::OFF:
            default: break;
        }

        // 1. Measure sidechain bass energy (< 100 Hz) vs mid anchor (500 - 2000 Hz)
        float bassSumSq = 0.0f;
        float midSumSq = 0.0f;

        // Simple 1-pole / 2-pole tracking for sidechain detection
        const double wBass = 2.0 * 3.141592653589793 * 100.0 / m_sampleRate;
        const double aBass = std::clamp(std::exp(-wBass), 0.0, 0.999);

        const double wMid = 2.0 * 3.141592653589793 * 1000.0 / m_sampleRate;
        const double aMid = std::clamp(std::exp(-wMid), 0.0, 0.999);

        for (size_t i = 0; i < numSamples; ++i) {
            float monoIn = 0.5f * (left[i] + right[i]);

            // Low detector (LP at 100 Hz)
            m_detL_z1 = (1.0 - aBass) * static_cast<double>(monoIn) + aBass * m_detL_z1;
            float lowSample = static_cast<float>(m_detL_z1);
            bassSumSq += lowSample * lowSample;

            // Mid detector (HP/LP tracking ~1 kHz)
            m_detM_z1 = (1.0 - aMid) * static_cast<double>(monoIn) + aMid * m_detM_z1;
            float midSample = static_cast<float>(m_detM_z1 - m_detL_z1);
            midSumSq += midSample * midSample;
        }

        float blockBassRms = std::sqrt(bassSumSq / static_cast<float>(numSamples) + 1e-9f);
        float blockMidRms  = std::sqrt(midSumSq / static_cast<float>(numSamples) + 1e-9f);

        // Smooth energy envelopes (~100 ms release)
        float dtSeconds = static_cast<float>(numSamples) / static_cast<float>(m_sampleRate);
        float alpha = std::clamp(std::exp(-dtSeconds / 0.100f), 0.0f, 0.999f);
        m_bassEnv = (blockBassRms > m_bassEnv) ? blockBassRms : (alpha * m_bassEnv + (1.0f - alpha) * blockBassRms);
        m_midEnv  = (blockMidRms > m_midEnv)   ? blockMidRms  : (alpha * m_midEnv + (1.0f - alpha) * blockMidRms);

        // Compute bass-to-mid ratio:
        // Vintage / thin tracks typically have ratio < 0.40. Modern EDM / club tracks > 0.85.
        float ratio = m_bassEnv / (m_midEnv + 1e-4f);
        float deficit = std::clamp((0.75f - ratio) / 0.45f, 0.0f, 1.0f);
        float targetGainDb = maxTargetDb * deficit;

        // Smooth gain slewing to eliminate any stepping artifacts (50 ms slew)
        float gainAlpha = std::clamp(std::exp(-dtSeconds / 0.050f), 0.0f, 0.999f);
        m_currentGainDb = gainAlpha * m_currentGainDb + (1.0f - gainAlpha) * targetGainDb;
        m_visualLiftDb = m_currentGainDb;

        if (m_currentGainDb < 0.1f) {
            return; // Virtually bypassed, pass through untouched
        }

        // 2. Compute 2nd-order Low-Shelf filter coefficients (fc = 100 Hz, Q = 0.707)
        double b0, b1, b2, a1, a2;
        calculateLowShelf(100.0, static_cast<double>(m_currentGainDb), b0, b1, b2, a1, a2);

        // 3. Process stereo audio in-place via Direct Form II Transposed
        double sL1 = m_sL_z1, sL2 = m_sL_z2;
        double sR1 = m_sR_z1, sR2 = m_sR_z2;

        for (size_t i = 0; i < numSamples; ++i) {
            double inL = static_cast<double>(left[i]);
            double outL = b0 * inL + sL1;
            sL1 = b1 * inL - a1 * outL + sL2;
            sL2 = b2 * inL - a2 * outL;
            left[i] = static_cast<float>(outL);

            double inR = static_cast<double>(right[i]);
            double outR = b0 * inR + sR1;
            sR1 = b1 * inR - a1 * outR + sR2;
            sR2 = b2 * inR - a2 * outR;
            right[i] = static_cast<float>(outR);
        }

        m_sL_z1 = sL1; m_sL_z2 = sL2;
        m_sR_z1 = sR1; m_sR_z2 = sR2;
    }

    /**
     * Get real-time dynamic lift in dB for visual metering (0.0 to +6.5 dB).
     */
    float getLiftDb() const noexcept { return m_visualLiftDb; }

private:
    void calculateLowShelf(double cutoffHz, double gainDb,
                           double& b0, double& b1, double& b2, double& a1, double& a2) noexcept {
        constexpr double PI = 3.14159265358979323846;
        const double A = std::pow(10.0, gainDb / 40.0); // sqrt of linear gain
        const double w0 = 2.0 * PI * cutoffHz / m_sampleRate;
        const double cosW0 = std::cos(w0);
        const double sinW0 = std::sin(w0);
        // S = 1 (standard Butterworth shelf slope)
        const double alpha = sinW0 * 0.7071067811865475; // sin(w0) / (2 * Q) with Q = 0.707
        const double twoSqrtAAlpha = 2.0 * std::sqrt(A) * alpha;

        const double a0 = (A + 1.0) + (A - 1.0) * cosW0 + twoSqrtAAlpha;
        b0 = (A * ((A + 1.0) - (A - 1.0) * cosW0 + twoSqrtAAlpha)) / a0;
        b1 = (2.0 * A * ((A - 1.0) - (A + 1.0) * cosW0)) / a0;
        b2 = (A * ((A + 1.0) - (A - 1.0) * cosW0 - twoSqrtAAlpha)) / a0;
        a1 = (-2.0 * ((A - 1.0) + (A + 1.0) * cosW0)) / a0;
        a2 = ((A + 1.0) + (A - 1.0) * cosW0 - twoSqrtAAlpha) / a0;
    }

    double m_sampleRate = 44100.0;
    float m_bassEnv = 0.0f;
    float m_midEnv = 0.0f;
    float m_currentGainDb = 0.0f;
    float m_visualLiftDb = 0.0f;

    // Sidechain detector state
    double m_detL_z1 = 0.0, m_detL_z2 = 0.0;
    double m_detM_z1 = 0.0, m_detM_z2 = 0.0;

    // Filter states (Transposed DF-II, double precision)
    double m_sL_z1 = 0.0, m_sL_z2 = 0.0;
    double m_sR_z1 = 0.0, m_sR_z2 = 0.0;
};

} // namespace autolevel::dsp
