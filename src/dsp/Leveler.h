#pragma once

#include "LoudnessMeter.h"
#include <cmath>
#include <algorithm>

namespace autolevel::dsp {

struct LevelerParams {
    // Always overwritten by AutoLevelEngine::process() from EngineParameters::targetLUFS
    // before use; this default only matters for code constructing LevelerParams directly.
    float targetLUFS = -14.0f;      // Matches the plugin's actual default (see EngineParameters)
    float maxBoostDb = 12.0f;       // Maximum boost (12 dB default from Android)
    float maxCutDb = 12.0f;         // Maximum attenuation (12 dB default from Android)
    bool freezeBreakdowns = true;   // Don't boost into breakdowns
    float breakdownThresholdLU = 7.0f; // LU drop below integrated to trigger freeze
    bool enabled = true;
};

/**
 * Leveler matching Android Leveler.kt:
 * - Asymmetric slew:
 *     Fast lock (first 8 s): 4.0 dB/s base, 4x downward multiplier (16 dB/s)
 *     Steady state: 0.75 dB/s base, 2x downward multiplier (1.5 dB/s)
 * - Online gated mean from 1 dB histogram
 * - Smooth block interpolation
 */
class Leveler {
public:
    Leveler() = default;

    void prepare(double sampleRate) {
        m_sampleRate = sampleRate;
        reset();
    }

    void reset() {
        m_currentGainDb = 0.0f;
        m_targetGainDb = 0.0f;
        m_smoothGainLin = 1.0f;
        m_fastLockTimerMs = 0;
        m_isFrozen = false;
    }

    void update(const LevelerParams& params, const LoudnessReadings& readings, size_t blocksIntegrated, float dtSeconds) {
        if (!params.freezeBreakdowns) {
            m_isFrozen = false;
        }

        if (!params.enabled || readings.integratedLUFS <= ABSOLUTE_GATE_LUFS || blocksIntegrated < 5) {
            m_targetGainDb = m_currentGainDb;
            return;
        }

        // Breakdown detection: if momentary is significantly quieter than integrated, freeze upward boost
        bool breakdown = false;
        if (params.freezeBreakdowns) {
            float delta = readings.integratedLUFS - readings.momentaryLUFS;
            if (delta > params.breakdownThresholdLU) {
                breakdown = true;
            }
        }
        m_isFrozen = breakdown;

        // Desired correction = target - integrated
        float rawDesired = params.targetLUFS - readings.integratedLUFS;
        float clampedDesired = std::clamp(rawDesired, -params.maxCutDb, params.maxBoostDb);

        if (m_isFrozen && clampedDesired > m_currentGainDb) {
            m_targetGainDb = m_currentGainDb; // Hold gain during breakdown
        } else {
            m_targetGainDb = clampedDesired;
        }

        // Fast lock for initial 8 seconds of track (from Android Leveler.kt)
        constexpr long FAST_LOCK_MS = 8000;
        constexpr float FAST_SLEW_DB_S = 4.0f;
        constexpr float SLOW_SLEW_DB_S = 0.75f;
        constexpr float FAST_DOWN_MULTIPLIER = 4.0f;
        constexpr float SLOW_DOWN_MULTIPLIER = 2.0f;

        bool isFastLock = (m_fastLockTimerMs < FAST_LOCK_MS);
        if (isFastLock) {
            m_fastLockTimerMs += static_cast<long>(dtSeconds * 1000.0f);
        }

        float base = isFastLock ? FAST_SLEW_DB_S : SLOW_SLEW_DB_S;
        float rate = (m_targetGainDb < m_currentGainDb)
            ? base * (isFastLock ? FAST_DOWN_MULTIPLIER : SLOW_DOWN_MULTIPLIER)
            : base;

        float maxStep = rate * dtSeconds;
        if (m_targetGainDb > m_currentGainDb) {
            m_currentGainDb = std::min(m_currentGainDb + maxStep, m_targetGainDb);
        } else if (m_targetGainDb < m_currentGainDb) {
            m_currentGainDb = std::max(m_currentGainDb - maxStep, m_targetGainDb);
        }
    }

    void processBlock(float* left, float* right, size_t numSamples) {
        float targetLin = std::pow(10.0f, m_currentGainDb / 20.0f);
        float step = (targetLin - m_smoothGainLin) / static_cast<float>(std::max<size_t>(1, numSamples));

        for (size_t i = 0; i < numSamples; ++i) {
            m_smoothGainLin += step;
            left[i] *= m_smoothGainLin;
            right[i] *= m_smoothGainLin;
        }
        m_smoothGainLin = targetLin;
    }

    float getCurrentGainDb() const noexcept { return m_currentGainDb; }
    float getTargetGainDb() const noexcept { return m_targetGainDb; }
    bool isFrozen() const noexcept { return m_isFrozen; }

private:
    double m_sampleRate = 48000.0;
    float m_currentGainDb = 0.0f;
    float m_targetGainDb = 0.0f;
    float m_smoothGainLin = 1.0f;
    long m_fastLockTimerMs = 0;
    bool m_isFrozen = false;
};

} // namespace autolevel::dsp
