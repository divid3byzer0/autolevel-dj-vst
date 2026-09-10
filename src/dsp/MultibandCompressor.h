#pragma once

#include "Bands.h"
#include "Biquad.h"
#include <array>
#include <cmath>
#include <algorithm>

namespace autolevel::dsp {

enum class MBCSpeed {
    SLOW,
    NORMAL,
    FAST
};

struct MBCParams {
    bool enabled = true;
    bool autoMakeup = true;
    float compressionAmount = 0.5f;     // 0.0 (bypass) to 1.0 (heavy)
    float toneSlopeDbPerOctave = -2.0f; // Tonal target tilt (-6.0 to 0.0 dB/oct, default -2.0)
    TargetProfile profile = TargetProfile::MODERN_MIX;
    std::array<float, Bands::COUNT> customOffsetsDb = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    float baseThresholdDb = Bands::MBC_THRESHOLD_DB;
    MBCSpeed speed = MBCSpeed::NORMAL;
};

class BandCompressor {
public:
    BandCompressor() = default;

    void setup(double sampleRate, float attackMs, float releaseMs) {
        m_sampleRate = sampleRate;
        updateBallistics(attackMs, releaseMs);
        reset();
    }

    void updateBallistics(float attackMs, float releaseMs) noexcept {
        m_attackCoeff = std::exp(-1.0 / (m_sampleRate * (attackMs * 0.001)));
        m_releaseCoeff = std::exp(-1.0 / (m_sampleRate * (releaseMs * 0.001)));
    }

    void reset() {
        m_envelope = 0.0;
        m_gainReductionDb = 0.0f;
    }

    /**
     * Exact 6 dB soft-knee dynamics compression matching Android DynamicsProcessing MBC.
     */
    inline void processStereo(float& left, float& right, float thresholdDb, float ratio, float amount) noexcept {
        if (amount < Bands::MIN_COMPRESSION || ratio <= 1.001f) {
            m_gainReductionDb = 0.0f;
            return;
        }

        // Stereo peak envelope detector
        double absMax = std::max(std::abs(left), std::abs(right));
        if (absMax > m_envelope) {
            m_envelope = absMax + m_attackCoeff * (m_envelope - absMax);
        } else {
            m_envelope = absMax + m_releaseCoeff * (m_envelope - absMax);
        }

        double envDb = (m_envelope > 1e-6) ? 20.0 * std::log10(m_envelope) : -120.0;
        double overDb = envDb - thresholdDb;

        // Soft-knee calculation (knee width = 6.0 dB, half-knee = 3.0 dB)
        constexpr double halfKnee = 3.0;
        double grDb = 0.0;
        double slope = 1.0 - 1.0 / static_cast<double>(ratio);

        if (overDb <= -halfKnee) {
            grDb = 0.0;
        } else if (overDb < halfKnee) {
            // Inside the 6 dB soft-knee transition
            double x = overDb + halfKnee;
            grDb = -(slope * x * x) / (4.0 * halfKnee);
        } else {
            // Fully above the knee
            grDb = -slope * overDb;
        }

        m_gainReductionDb = static_cast<float>(grDb);
        if (grDb < 0.0) {
            float gainLin = static_cast<float>(std::pow(10.0, grDb / 20.0));
            left *= gainLin;
            right *= gainLin;
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
 * Attack/Release times and thresholds exactly mirror Android GainProcessor & Shaper:
 * - Band 0 (Sub, < 120 Hz): Attack = 30 ms, Release = 400 ms
 * - Bands 1-5: Attack = 15 ms, Release = 200 ms
 * - Knee width: 6 dB
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

        // Phase-compensation allpasses (see AP_COMPENSATION below)
        for (size_t ch = 0; ch < 2; ++ch) {
            for (size_t b = 0; b < Bands::COUNT; ++b) {
                for (size_t k = 0; k < AP_MAX; ++k) {
                    int xover = AP_COMPENSATION[b][k];
                    if (xover >= 0) {
                        m_ap[ch][b][k].setup(Bands::CROSSOVERS[static_cast<size_t>(xover)], sampleRate);
                    }
                }
            }
        }

        // Exact ballistics from Android GainProcessor.kt:
        // Band 0: attack 30ms, release 400ms (slower in bass as broadcast chains do)
        // Bands 1-5: attack 15ms, release 200ms
        m_bands[0].setup(sampleRate, 30.0f, 400.0f);
        for (size_t b = 1; b < Bands::COUNT; ++b) {
            m_bands[b].setup(sampleRate, 15.0f, 200.0f);
        }
        // setup() above reinstalls NORMAL ballistics, so forget the cached speed;
        // otherwise updateSpeed() early-returns and a selected Slow/Fast silently
        // reverts to Normal after any prepare() (e.g. a JACK sample-rate change).
        m_currentSpeed = MBCSpeed::NORMAL;

        reset();
    }

    void reset() {
        for (size_t ch = 0; ch < 2; ++ch) {
            for (size_t i = 0; i < 5; ++i) {
                m_lp[ch][i].reset();
                m_hp[ch][i].reset();
            }
            for (size_t b = 0; b < Bands::COUNT; ++b) {
                for (size_t k = 0; k < AP_MAX; ++k) m_ap[ch][b][k].reset();
            }
        }
        for (size_t b = 0; b < Bands::COUNT; ++b) {
            m_bands[b].reset();
        }
        m_autoMakeupGainDb = 0.0f;
        m_smoothMakeupLin = 1.0f;
    }

    void updateSpeed(MBCSpeed speed) {
        if (speed == m_currentSpeed) return;
        m_currentSpeed = speed;

        float subAttack = 30.0f;
        float subRelease = 400.0f;
        float otherAttack = 15.0f;
        float otherRelease = 200.0f;

        if (speed == MBCSpeed::SLOW) {
            subAttack = 60.0f;
            subRelease = 800.0f;
            otherAttack = 30.0f;
            otherRelease = 400.0f;
        } else if (speed == MBCSpeed::FAST) {
            subAttack = 15.0f;
            subRelease = 200.0f;
            otherAttack = 7.5f;
            otherRelease = 100.0f;
        }

        m_bands[0].updateBallistics(subAttack, subRelease);
        for (size_t b = 1; b < Bands::COUNT; ++b) {
            m_bands[b].updateBallistics(otherAttack, otherRelease);
        }
    }

    void process(float* left, float* right, size_t numSamples, const MBCParams& params) {
        if (!params.enabled || params.compressionAmount < Bands::MIN_COMPRESSION) {
            for (size_t b = 0; b < Bands::COUNT; ++b) {
                m_bands[b].reset();
            }
            m_autoMakeupGainDb = 0.0f;
            m_smoothMakeupLin = 1.0f;
            return;
        }

        updateSpeed(params.speed);

        // Calculate thresholds per band using exact Android Shaper formula
        std::array<float, Bands::COUNT> thresholds = Bands::thresholdsFor(
            true, params.toneSlopeDbPerOctave, params.profile, params.customOffsetsDb, params.baseThresholdDb
        );

        // Compression ratio: 1.0 to 4.0 matching Android Shaper.kt
        float ratio = 1.0f + params.compressionAmount * (Bands::MAX_RATIO - 1.0f);

        // Process in sub-blocks of 32 samples for smooth, sub-millisecond adaptive makeup tracking
        constexpr size_t SUB_BLOCK = 32;
        size_t s = 0;
        while (s < numSamples) {
            size_t chunk = std::min(SUB_BLOCK, numSamples - s);

            float targetMakeupDb = 0.0f;
            if (params.autoMakeup) {
                constexpr std::array<float, Bands::COUNT> weights = { 0.10f, 0.20f, 0.25f, 0.25f, 0.15f, 0.05f };
                float effGrDb = 0.0f;
                for (size_t b = 0; b < Bands::COUNT; ++b) {
                    effGrDb += weights[b] * m_bands[b].getGainReductionDb();
                }
                targetMakeupDb = std::clamp(-effGrDb, 0.0f, 12.0f);
            }
            m_autoMakeupGainDb = targetMakeupDb;

            float targetLin = std::pow(10.0f, targetMakeupDb / 20.0f);
            float step = (targetLin - m_smoothMakeupLin) / static_cast<float>(chunk);

            for (size_t i = 0; i < chunk; ++i, ++s) {
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

                m_smoothMakeupLin += step;
                left[s] = outL * m_smoothMakeupLin;
                right[s] = outR * m_smoothMakeupLin;
            }
            m_smoothMakeupLin = targetLin;
        }

        // Final update of reported makeup gain at end of block
        if (params.autoMakeup) {
            constexpr std::array<float, Bands::COUNT> weights = { 0.10f, 0.20f, 0.25f, 0.25f, 0.15f, 0.05f };
            float effGrDb = 0.0f;
            for (size_t b = 0; b < Bands::COUNT; ++b) {
                effGrDb += weights[b] * m_bands[b].getGainReductionDb();
            }
            m_autoMakeupGainDb = std::clamp(-effGrDb, 0.0f, 12.0f);
        } else {
            m_autoMakeupGainDb = 0.0f;
        }
    }

    std::array<float, Bands::COUNT> getGainReductionsDb() const noexcept {
        std::array<float, Bands::COUNT> gr;
        for (size_t b = 0; b < Bands::COUNT; ++b) {
            gr[b] = m_bands[b].getGainReductionDb();
        }
        return gr;
    }

    float getAutoMakeupGainDb() const noexcept {
        return m_autoMakeupGainDb;
    }

private:
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

        // Phase-align the bands so they reconstruct flat when summed. Each band
        // is passed through the allpasses of the splits its own path skipped;
        // this leaves every band's magnitude response untouched (so the per-band
        // thresholds keep their calibration) and only corrects the summation.
        for (size_t b = 0; b < Bands::COUNT; ++b) {
            double v = static_cast<double>(outBands[b]);
            for (size_t k = 0; k < AP_MAX; ++k) {
                if (AP_COMPENSATION[b][k] < 0) break;
                v = m_ap[ch][b][k].process(v);
            }
            outBands[b] = static_cast<float>(v);
        }
    }

    /**
     * Which crossovers each band must be allpass-corrected by, as indices into
     * Bands::CROSSOVERS = {120, 400, 1200, 3500, 8000}; -1 terminates the list.
     *
     * The tree splits at 1200 first, then 400 and 120 down the low branch and
     * 3500 and 8000 down the high branch. Writing L for the 1200 low branch and
     * H for the high one, and using LP+HP = AP at each split:
     *
     *   bands 0+1 already sum to LP1200*LP400*AP120, so band 2 (LP1200*HP400)
     *   needs AP120 to let the 400 split close:  L = LP1200*AP120*AP400
     *   bands 4+5 already sum to HP1200*HP3500*AP8000, so band 3 needs AP8000:
     *                                            H = HP1200*AP8000*AP3500
     *   L and H now carry different allpasses, so the 1200 split cannot close.
     *   Give L the high branch's pair and H the low branch's pair, and the whole
     *   sum collapses to AP120*AP400*AP3500*AP8000*AP1200 - flat magnitude.
     */
    static constexpr size_t AP_MAX = 3;
    static constexpr int AP_COMPENSATION[Bands::COUNT][AP_MAX] = {
        { 3,  4, -1 },   // Sub       : cross-branch AP3500, AP8000
        { 3,  4, -1 },   // Bass      : cross-branch AP3500, AP8000
        { 0,  3,  4 },   // Low-Mid   : intra AP120 + cross-branch AP3500, AP8000
        { 4,  0,  1 },   // High-Mid  : intra AP8000 + cross-branch AP120, AP400
        { 0,  1, -1 },   // Presence  : cross-branch AP120, AP400
        { 0,  1, -1 }    // Air       : cross-branch AP120, AP400
    };

    double m_sampleRate = 48000.0;

    std::array<std::array<LR4Filter, 5>, 2> m_lp;
    std::array<std::array<LR4Filter, 5>, 2> m_hp;
    std::array<std::array<std::array<AllpassLR4, AP_MAX>, Bands::COUNT>, 2> m_ap;

    std::array<BandCompressor, Bands::COUNT> m_bands;
    MBCSpeed m_currentSpeed = MBCSpeed::NORMAL;
    float m_autoMakeupGainDb = 0.0f;
    float m_smoothMakeupLin = 1.0f;
};

} // namespace autolevel::dsp
