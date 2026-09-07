#pragma once

#include "Bands.h"
#include "Biquad.h"
#include <array>
#include <cmath>
#include <algorithm>

namespace autolevel::dsp {

struct MBCParams {
    bool enabled = true;
    float compressionAmount = 0.5f;     // 0.0 (bypass) to 1.0 (heavy)
    float toneSlopeDbPerOctave = -3.75f; // Tonal target tilt (-4.5 to -3.0 dB/oct)
    float targetLUFS = -9.0f;
};

class BandCompressor {
public:
    BandCompressor() = default;

    void setup(double sampleRate, float attackMs, float releaseMs) {
        m_sampleRate = sampleRate;
        m_attackCoeff = std::exp(-1.0 / (sampleRate * (attackMs * 0.001)));
        m_releaseCoeff = std::exp(-1.0 / (sampleRate * (releaseMs * 0.001)));
        reset();
    }

    void reset() {
        m_envelope = 0.0;
        m_gainReductionDb = 0.0f;
    }

    inline void processStereo(float& left, float& right, float thresholdDb, float ratio, float amount) noexcept {
        if (amount <= 0.001f || ratio <= 1.001f) {
            m_gainReductionDb = 0.0f;
            return;
        }

        // Stereo envelope tracking (peak of L and R)
        double absMax = std::max(std::abs(left), std::abs(right));
        if (absMax > m_envelope) {
            m_envelope = absMax + m_attackCoeff * (m_envelope - absMax);
        } else {
            m_envelope = absMax + m_releaseCoeff * (m_envelope - absMax);
        }

        double envDb = (m_envelope > 1e-6) ? 20.0 * std::log10(m_envelope) : -120.0;
        double overDb = envDb - thresholdDb;

        if (overDb > 0.0) {
            // Soft-knee compression
            double targetGrDb = -(overDb * (1.0 - 1.0 / ratio)) * amount;
            m_gainReductionDb = static_cast<float>(targetGrDb);
            float gainLin = static_cast<float>(std::pow(10.0, targetGrDb / 20.0));
            left *= gainLin;
            right *= gainLin;
        } else {
            m_gainReductionDb = 0.0f;
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
    float m_gainReductionDb = 0.0f;
};

/**
 * 6-band Linkwitz-Riley (LR4) crossover filterbank and dynamic compressor.
 */
class MultibandCompressor {
public:
    MultibandCompressor() = default;

    void prepare(double sampleRate) {
        m_sampleRate = sampleRate;

        // Setup crossovers at: 120, 400, 1200, 3500, 8000 Hz
        for (size_t ch = 0; ch < 2; ++ch) {
            for (size_t i = 0; i < 5; ++i) {
                m_lp[ch][i].setup(Biquad::Type::LowPass, Bands::CROSSOVERS[i], sampleRate);
                m_hp[ch][i].setup(Biquad::Type::HighPass, Bands::CROSSOVERS[i], sampleRate);
            }
        }

        // Setup per-band dynamic compressor ballistics
        // Sub (25ms/120ms), Bass (20ms/100ms), LowMid (15ms/80ms), HighMid (10ms/60ms), Presence (6ms/40ms), Air (4ms/30ms)
        const std::array<float, Bands::COUNT> attackTimes = { 25.0f, 20.0f, 15.0f, 10.0f, 6.0f, 4.0f };
        const std::array<float, Bands::COUNT> releaseTimes = { 120.0f, 100.0f, 80.0f, 60.0f, 40.0f, 30.0f };

        for (size_t b = 0; b < Bands::COUNT; ++b) {
            m_bands[b].setup(sampleRate, attackTimes[b], releaseTimes[b]);
        }

        reset();
    }

    void reset() {
        for (size_t ch = 0; ch < 2; ++ch) {
            for (size_t i = 0; i < 5; ++i) {
                m_lp[ch][i].reset();
                m_hp[ch][i].reset();
            }
        }
        for (size_t b = 0; b < Bands::COUNT; ++b) {
            m_bands[b].reset();
        }
    }

    void process(float* left, float* right, size_t numSamples, const MBCParams& params) {
        if (!params.enabled || params.compressionAmount <= 0.001f) {
            return;
        }

        // Calculate thresholds per band from tone slope and target LUFS
        std::array<float, Bands::COUNT> slopeOffsets;
        Bands::computeThresholdOffsets(params.toneSlopeDbPerOctave, slopeOffsets);

        // Power per octave normalization offsets (relative to broad spectrum distribution)
        // Sub has higher RMS density in dance music, air has lower
        const std::array<float, Bands::COUNT> bandDensityOffsets = {
            +3.0f, +1.5f, 0.0f, -1.0f, -2.5f, -4.0f
        };

        std::array<float, Bands::COUNT> thresholds;
        float ratio = 1.0f + params.compressionAmount * 3.0f; // 1:1 to 4:1 ratio

        for (size_t b = 0; b < Bands::COUNT; ++b) {
            // Threshold sits relative to target loudness + slope tilt
            thresholds[b] = params.targetLUFS + slopeOffsets[b] + bandDensityOffsets[b] + (1.0f - params.compressionAmount) * 4.0f;
        }

        for (size_t s = 0; s < numSamples; ++s) {
            float inL = left[s];
            float inR = right[s];

            std::array<float, Bands::COUNT> bandL;
            std::array<float, Bands::COUNT> bandR;

            split6Bands(0, inL, bandL);
            split6Bands(1, inR, bandR);

            float outL = 0.0f;
            float outR = 0.0f;

            for (size_t b = 0; b < Bands::COUNT; ++b) {
                m_bands[b].processStereo(bandL[b], bandR[b], thresholds[b], ratio, params.compressionAmount);
                outL += bandL[b];
                outR += bandR[b];
            }

            left[s] = outL;
            right[s] = outR;
        }
    }

    std::array<float, Bands::COUNT> getGainReductionsDb() const noexcept {
        std::array<float, Bands::COUNT> gr;
        for (size_t b = 0; b < Bands::COUNT; ++b) {
            gr[b] = m_bands[b].getGainReductionDb();
        }
        return gr;
    }

private:
    /**
     * Splits incoming sample into 6 frequency bands using a tree of LR4 crossovers.
     * Crossovers: c0=120, c1=400, c2=1200, c3=3500, c4=8000
     */
    inline void split6Bands(size_t ch, float in, std::array<float, Bands::COUNT>& outBands) noexcept {
        // Crossover 2 (1200 Hz): Split into Low (< 1200 Hz) and High (> 1200 Hz)
        double low1200 = m_lp[ch][2].process(in);
        double high1200 = m_hp[ch][2].process(in);

        // Low branch: Split at Crossover 1 (400 Hz)
        double low400 = m_lp[ch][1].process(low1200);
        double high400 = m_hp[ch][1].process(low1200);

        // Sub branch: Split low400 at Crossover 0 (120 Hz)
        outBands[0] = static_cast<float>(m_lp[ch][0].process(low400)); // Sub (< 120)
        outBands[1] = static_cast<float>(m_hp[ch][0].process(low400)); // Bass (120 - 400)
        outBands[2] = static_cast<float>(high400);                     // Low-Mid (400 - 1200)

        // High branch: Split at Crossover 3 (3500 Hz)
        double low3500 = m_lp[ch][3].process(high1200);
        double high3500 = m_hp[ch][3].process(high1200);

        outBands[3] = static_cast<float>(low3500);                     // High-Mid (1200 - 3500)

        // HighHigh branch: Split at Crossover 4 (8000 Hz)
        outBands[4] = static_cast<float>(m_lp[ch][4].process(high3500)); // Presence (3500 - 8000)
        outBands[5] = static_cast<float>(m_hp[ch][4].process(high3500)); // Air (> 8000)
    }

    double m_sampleRate = 48000.0;

    // 2 channels x 5 crossovers: LP and HP
    std::array<std::array<LR4Filter, 5>, 2> m_lp;
    std::array<std::array<LR4Filter, 5>, 2> m_hp;

    std::array<BandCompressor, Bands::COUNT> m_bands;
};

} // namespace autolevel::dsp
