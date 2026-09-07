#pragma once

#include "LoudnessMeter.h"
#include <cmath>
#include <algorithm>

namespace autolevel::dsp {

struct LevelerParams {
    float targetLUFS = -9.0f;       // DJ club standard level
    float maxBoostDb = 6.0f;        // Maximum boost allowed
    float maxCutDb = 12.0f;         // Maximum attenuation allowed
    float baseSlewDbPerSec = 0.75f; // Steady-state upward slew rate (dB/sec)
    bool freezeBreakdowns = true;   // Don't boost into breakdowns
    float breakdownThresholdLU = 7.0f; // LU drop below integrated to trigger freeze
    bool enabled = true;
};

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
        if (!params.enabled || readings.integratedLUFS <= ABSOLUTE_GATE_LUFS || blocksIntegrated < 5) {
            // Not enough signal to adapt yet, hold current gain
            m_targetGainDb = m_currentGainDb;
            return;
        }

        // Check breakdown freeze: if momentary is significantly quieter than integrated,
        // we are in a breakdown, buildup, or quiet intro/outro. Freeze gain boost!
        bool breakdown = false;
        if (params.freezeBreakdowns) {
            float delta = readings.integratedLUFS - readings.momentaryLUFS;
            if (delta > params.breakdownThresholdLU) {
                breakdown = true;
            }
        }
        m_isFrozen = breakdown;

        // Desired correction = Target - Integrated
        float rawDesired = params.targetLUFS - readings.integratedLUFS;
        float clampedDesired = std::clamp(rawDesired, -params.maxCutDb, params.maxBoostDb);

        // If frozen in a breakdown, only allow cut (downwards), never boost (upwards)
        if (m_isFrozen && clampedDesired > m_currentGainDb) {
            m_targetGainDb = m_currentGainDb;
        } else {
            m_targetGainDb = clampedDesired;
        }

        // Asymmetric slew rate computation
        bool isFastLock = (m_fastLockTimerMs < 8000); // First 8 seconds of track
        if (isFastLock) {
            m_fastLockTimerMs += static_cast<long>(dtSeconds * 1000.0f);
        }

        float baseRate = isFastLock ? 4.0f : params.baseSlewDbPerSec;
        float slewRate = baseRate;

        if (m_targetGainDb < m_currentGainDb) {
            // Gain reduction is faster to prevent blasting sound systems
            float downMultiplier = isFastLock ? 4.0f : 2.5f;
            slewRate = baseRate * downMultiplier;
        }

        float maxStep = slewRate * dtSeconds;
        if (m_targetGainDb > m_currentGainDb) {
            m_currentGainDb = std::min(m_currentGainDb + maxStep, m_targetGainDb);
        } else if (m_targetGainDb < m_currentGainDb) {
            m_currentGainDb = std::max(m_currentGainDb - maxStep, m_targetGainDb);
        }
    }

    /**
     * Apply the slew-limited gain smoothly across an audio block to avoid clicks.
     */
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
