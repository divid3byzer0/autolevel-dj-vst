#pragma once

#include <cmath>
#include <algorithm>

namespace autolevel::dsp {

/**
 * Safety Limiter matching Android DynamicsProcessing.Limiter:
 * - Attack time = 1.0 ms
 * - Release time = 60.0 ms
 * - Ratio = 20:1
 * - Threshold = -1.5 dBFS (or user ceiling knob)
 */
class SafetyLimiter {
public:
    SafetyLimiter() = default;

    void prepare(double sampleRate) {
        m_sampleRate = sampleRate;
        // Exact 1.0 ms attack
        m_attackCoeff = std::exp(-1.0 / (sampleRate * 0.001));
        // Exact 60.0 ms release
        m_releaseCoeff = std::exp(-1.0 / (sampleRate * 0.060));
        reset();
    }

    void reset() {
        m_envelope = 0.0;
        m_gainReductionDb = 0.0f;
    }

    void setCeilingDb(float ceilingDb) {
        m_ceilingDb = ceilingDb;
        m_ceilingLin = std::pow(10.0f, ceilingDb / 20.0f);
    }

    void process(float* left, float* right, size_t numSamples) {
        constexpr double ratio = 20.0; // 20:1 limiter ratio from Android
        const double thresholdLin = m_ceilingLin;
        const double thresholdDb = m_ceilingDb;

        float blockMinGrDb = 0.0f;

        for (size_t i = 0; i < numSamples; ++i) {
            float inL = left[i];
            float inR = right[i];

            double peak = std::max(std::abs(inL), std::abs(inR));
            if (peak > m_envelope) {
                m_envelope = peak + m_attackCoeff * (m_envelope - peak);
            } else {
                m_envelope = peak + m_releaseCoeff * (m_envelope - peak);
            }

            double grDb = 0.0;
            if (m_envelope > thresholdLin) {
                double envDb = 20.0 * std::log10(m_envelope);
                double overDb = envDb - thresholdDb;
                // 20:1 compression ratio
                grDb = -overDb * (1.0 - 1.0 / ratio);
            }

            float gainLin = (grDb < 0.0) ? static_cast<float>(std::pow(10.0, grDb / 20.0)) : 1.0f;

            float outL = inL * gainLin;
            float outR = inR * gainLin;

            // Strict ceiling clamp for absolute DAC/amp clip safety
            float preClampMax = std::max(std::abs(outL), std::abs(outR));
            outL = std::clamp(outL, -m_ceilingLin, m_ceilingLin);
            outR = std::clamp(outR, -m_ceilingLin, m_ceilingLin);
            float postClampMax = std::max(std::abs(outL), std::abs(outR));

            // If ceiling clamp engaged, account for it in total gain reduction
            if (preClampMax > m_ceilingLin && postClampMax > 0.0f) {
                double clampGrDb = 20.0 * std::log10(static_cast<double>(postClampMax) / static_cast<double>(preClampMax));
                if (clampGrDb < grDb) {
                    grDb = clampGrDb;
                }
            }

            left[i] = outL;
            right[i] = outR;

            float sampleGrDb = static_cast<float>(grDb);
            if (sampleGrDb < blockMinGrDb) {
                blockMinGrDb = sampleGrDb;
            }
        }

        // Instant attack to catch all micro-transients; smooth ~16 dB/sec release for fluid visual metering
        if (blockMinGrDb < m_gainReductionDb) {
            m_gainReductionDb = blockMinGrDb;
        } else {
            float dtSeconds = static_cast<float>(numSamples) / static_cast<float>(m_sampleRate);
            m_gainReductionDb = std::min(0.0f, m_gainReductionDb + dtSeconds * 16.0f);
        }
    }

    float getGainReductionDb() const noexcept {
        return m_gainReductionDb;
    }

private:
    double m_sampleRate = 48000.0;
    double m_attackCoeff = 0.0;
    double m_releaseCoeff = 0.0;
    double m_envelope = 0.0;
    float m_ceilingDb = -0.3f; // -0.3 dBFS default master ceiling
    float m_ceilingLin = 0.96605088f;
    float m_gainReductionDb = 0.0f;
};

} // namespace autolevel::dsp
