#pragma once

#include <cmath>
#include <algorithm>

namespace autolevel::dsp {

class SafetyLimiter {
public:
    SafetyLimiter() = default;

    void prepare(double sampleRate) {
        m_sampleRate = sampleRate;
        // Fast attack ~ 0.2 ms
        m_attackCoeff = std::exp(-1.0 / (sampleRate * 0.0002));
        // Musical release ~ 60 ms
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
        for (size_t i = 0; i < numSamples; ++i) {
            float inL = left[i];
            float inR = right[i];

            double peak = std::max(std::abs(inL), std::abs(inR));
            if (peak > m_envelope) {
                m_envelope = peak + m_attackCoeff * (m_envelope - peak);
            } else {
                m_envelope = peak + m_releaseCoeff * (m_envelope - peak);
            }

            double gainLin = 1.0;
            if (m_envelope > m_ceilingLin) {
                gainLin = m_ceilingLin / m_envelope;
            }

            // Soft-saturation safety clamp if a massive sudden transient passes before envelope settles
            float outL = static_cast<float>(inL * gainLin);
            float outR = static_cast<float>(inR * gainLin);

            // Hard clamp at ceiling to 100% guarantee no DAC/amp clipping
            outL = std::clamp(outL, -m_ceilingLin, m_ceilingLin);
            outR = std::clamp(outR, -m_ceilingLin, m_ceilingLin);

            left[i] = outL;
            right[i] = outR;

            float currentGrDb = (gainLin < 1.0) ? static_cast<float>(20.0 * std::log10(gainLin)) : 0.0f;
            if (currentGrDb < m_gainReductionDb) {
                m_gainReductionDb = currentGrDb;
            } else {
                m_gainReductionDb += (0.0f - m_gainReductionDb) * 0.001f;
            }
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
    float m_ceilingDb = -0.5f;
    float m_ceilingLin = 0.944060876f;
    float m_gainReductionDb = 0.0f;
};

} // namespace autolevel::dsp
