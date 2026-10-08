#pragma once

#include "Bands.h"
#include "BandSplitter.h"
#include <array>
#include <cmath>
#include <algorithm>

namespace autolevel::dsp {

struct MBCParams {
    bool enabled = true;
    float compressionAmount = 0.5f;     // 0.0 (bypass) to 1.0 (heavy)
    /**
     * Ratio at compressionAmount = 1.0. Defaults to Bands::MAX_RATIO so existing hosts are
     * bit-identical; a host wanting a firmer top end can raise it without affecting others.
     */
    float maxRatio = Bands::MAX_RATIO;
    float toneSlopeDbPerOctave = -2.0f; // Tonal target tilt (-3.0 to 0.0 dB/oct, default -2.0)
    TargetProfile profile = TargetProfile::MODERN_MIX;
    std::array<float, Bands::COUNT> customOffsetsDb = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    float baseThresholdDb = Bands::MBC_THRESHOLD_DB;
    /**
     * Attack / release of bands 1-5 in ms. The Sub band runs at twice both (see SUB_TIME_SCALE),
     * its release capped at MAX_RELEASE_MS.
     */
    float attackMs = 15.0f;
    float releaseMs = 200.0f;
    /**
     * Program-dependent release. Off = one envelope at releaseMs (the classic behaviour). On = a
     * second, slow envelope runs alongside: short hits recover at releaseMs, sustained compression
     * recovers up to AUTO_SLOW_FACTOR times slower. See BandCompressor::processStereo.
     */
    bool autoRelease = false;
    /** Level detector: 0.0 = pure peak, 1.0 = pure RMS, in between blends the two. */
    float detectorRmsMix = 0.0f;
};

class BandCompressor {
public:
    BandCompressor() = default;

    void setup(double sampleRate, float attackMs, float releaseMs) {
        m_sampleRate = sampleRate;
        m_rmsCoeff = std::exp(-1.0 / (m_sampleRate * RMS_WINDOW_SECONDS));
        updateBallistics(attackMs, releaseMs);
        reset();
    }

    void updateBallistics(float attackMs, float releaseMs) noexcept {
        m_attackCoeff = std::exp(-1.0 / (m_sampleRate * (attackMs * 0.001)));
        m_releaseCoeff = std::exp(-1.0 / (m_sampleRate * (releaseMs * 0.001)));
    }

    /** Ballistics of the auto-release slow envelope. */
    void updateSlowBallistics(float slowAttackMs, float slowReleaseMs) noexcept {
        m_slowAttackCoeff = std::exp(-1.0 / (m_sampleRate * (slowAttackMs * 0.001)));
        m_slowReleaseCoeff = std::exp(-1.0 / (m_sampleRate * (slowReleaseMs * 0.001)));
    }

    void reset() {
        m_envelope = 0.0;
        m_slowEnvelope = 0.0;
        m_meanSquare = 0.0;
        m_gainReductionDb = 0.0f;
    }

    /**
     * Exact 6 dB soft-knee dynamics compression matching Android DynamicsProcessing MBC.
     *
     * rmsMix selects what the level detector sees: 0 = the stereo-linked peak, 1 = the
     * stereo-linked RMS, anything between is a linear blend of the two. The attack / release
     * ballistics are applied to the blended level either way.
     *
     * autoRelease: the gain follows the larger of two envelopes on the same level - the normal
     * one (user attack / release) and a slow one (slow attack, slow release). A short hit barely
     * charges the slow one, so it recovers at the user release; sustained material charges both,
     * so when it stops the gain comes back on the slow release instead of pumping. On steady
     * material both read the same level, so the amount of compression does not change.
     */
    inline void processStereo(float& left, float& right, float thresholdDb, float ratio, float amount,
                              float rmsMix, bool autoRelease = false) noexcept {
        if (amount < Bands::MIN_COMPRESSION || ratio <= 1.001f) {
            m_gainReductionDb = 0.0f;
            return;
        }

        // Stereo-linked peak
        double level = std::max(std::abs(left), std::abs(right));

        // Stereo-linked RMS: one-pole mean of the squared peak. Always tracked, so moving the
        // slider off 0 never starts from a stale average; only the sqrt is skipped at 0.
        const double peak = level;
        m_meanSquare = peak * peak + m_rmsCoeff * (m_meanSquare - peak * peak);
        if (rmsMix > 0.0f) {
            // sqrt(2 * mean-square): a sine reads its peak amplitude, so the two detectors
            // agree on a steady tone and differ only by how peaky the material is.
            const double rms = std::sqrt(2.0 * m_meanSquare);
            level = (1.0 - rmsMix) * peak + rmsMix * rms;
        }

        if (level > m_envelope) {
            m_envelope = level + m_attackCoeff * (m_envelope - level);
        } else {
            m_envelope = level + m_releaseCoeff * (m_envelope - level);
        }

        // Slow envelope, tracked in Manual too so switching to Auto never starts from a stale value.
        if (level > m_slowEnvelope) {
            m_slowEnvelope = level + m_slowAttackCoeff * (m_slowEnvelope - level);
        } else {
            m_slowEnvelope = level + m_slowReleaseCoeff * (m_slowEnvelope - level);
        }
        // Leaving Auto: carry the combined level into the normal envelope, so the gain releases
        // from where it was instead of jumping up in one sample.
        if (m_wasAutoRelease && !autoRelease) {
            m_envelope = std::max(m_envelope, m_slowEnvelope);
        }
        m_wasAutoRelease = autoRelease;
        const double envelope = autoRelease ? std::max(m_envelope, m_slowEnvelope) : m_envelope;

        double envDb = (envelope > 1e-6) ? 20.0 * std::log10(envelope) : -120.0;
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
    /** Averaging window of the RMS detector. */
    static constexpr double RMS_WINDOW_SECONDS = 0.030;

    double m_sampleRate = 48000.0;
    double m_attackCoeff = 0.0;
    double m_releaseCoeff = 0.0;
    double m_rmsCoeff = 0.0;
    double m_envelope = 0.0;
    double m_slowAttackCoeff = 0.0;
    double m_slowReleaseCoeff = 0.0;
    double m_slowEnvelope = 0.0;
    bool m_wasAutoRelease = false;
    double m_meanSquare = 0.0;
    float m_gainReductionDb = 0.0f;
};

/**
 * 6-band Linkwitz-Riley (LR4) crossover filterbank and dynamic compressor.
 * Attack/Release are user-set; the defaults mirror Android GainProcessor:
 * - Band 0 (Sub, < 120 Hz): twice the chosen times (default 30 ms / 400 ms), release capped at 5 s
 * - Bands 1-5: the chosen times (default 15 ms / 200 ms)
 * - Knee width: 6 dB
 */
class MultibandCompressor {
public:
    MultibandCompressor() = default;

    /** Sub band is slower than the rest by this factor, as broadcast chains do. */
    static constexpr float SUB_TIME_SCALE = 2.0f;

    static constexpr float MIN_ATTACK_MS = 1.0f;
    static constexpr float MAX_ATTACK_MS = 100.0f;
    static constexpr float MIN_RELEASE_MS = 20.0f;
    static constexpr float MAX_RELEASE_MS = 5000.0f;

    /** Auto release: the slow envelope releases this many times slower than the Release knob... */
    static constexpr float AUTO_SLOW_FACTOR = 10.0f;
    /** ...and charges this slowly (ms, bands 1-5; Sub x2), so only sustained material reaches it. */
    static constexpr float AUTO_SLOW_ATTACK_MS = 300.0f;

    void prepare(double sampleRate) {
        m_sampleRate = sampleRate;
        m_splitter.prepare(sampleRate);

        // setup() installs the sample rate and default ballistics; the cached times must
        // match it, or a later process() with the same times would skip a needed update.
        m_attackMs = DEFAULT_ATTACK_MS;
        m_releaseMs = DEFAULT_RELEASE_MS;
        m_bands[0].setup(sampleRate, m_attackMs * SUB_TIME_SCALE, subRelease(m_releaseMs));
        for (size_t b = 1; b < Bands::COUNT; ++b) {
            m_bands[b].setup(sampleRate, m_attackMs, m_releaseMs);
        }
        applySlowBallistics();

        reset();
    }

    void reset() {
        m_splitter.reset();
        for (size_t b = 0; b < Bands::COUNT; ++b) {
            m_bands[b].reset();
        }
    }

    void process(float* left, float* right, size_t numSamples, const MBCParams& params) {
        if (!params.enabled || params.compressionAmount < Bands::MIN_COMPRESSION) {
            for (size_t b = 0; b < Bands::COUNT; ++b) {
                m_bands[b].reset();
            }
            return;
        }

        updateBallistics(params.attackMs, params.releaseMs);

        // Calculate thresholds per band using exact Android Shaper formula
        std::array<float, Bands::COUNT> thresholds = Bands::thresholdsFor(
            true, params.toneSlopeDbPerOctave, params.profile, params.customOffsetsDb, params.baseThresholdDb
        );

        // Compression ratio: 1.0 up to params.maxRatio (Bands::MAX_RATIO = 4.0 by default,
        // matching Android Shaper.kt).
        float ratio = 1.0f + params.compressionAmount * (params.maxRatio - 1.0f);
        float rmsMix = std::clamp(params.detectorRmsMix, 0.0f, 1.0f);

        // No makeup gain here: the MBC's output level is set by hand with the Post-MBC Gain
        // stage that follows it (auto-makeup was removed 2026-09-29, as in autolevel-box).
        for (size_t s = 0; s < numSamples; ++s) {
            std::array<float, Bands::COUNT> bandL;
            std::array<float, Bands::COUNT> bandR;

            m_splitter.split(0, left[s], bandL);
            m_splitter.split(1, right[s], bandR);

            float outL = 0.0f;
            float outR = 0.0f;

            for (size_t b = 0; b < Bands::COUNT; ++b) {
                m_bands[b].processStereo(bandL[b], bandR[b], thresholds[b], ratio,
                                         params.compressionAmount, rmsMix, params.autoRelease);
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
    static constexpr float DEFAULT_ATTACK_MS = 15.0f;
    static constexpr float DEFAULT_RELEASE_MS = 200.0f;

    /**
     * Install new times only when they changed - this runs once per block. Swaps the
     * coefficients only: the envelopes keep running, so turning the knob during playback
     * does not snap the gain reduction back to zero.
     */
    void updateBallistics(float attackMs, float releaseMs) {
        attackMs = std::clamp(attackMs, MIN_ATTACK_MS, MAX_ATTACK_MS);
        releaseMs = std::clamp(releaseMs, MIN_RELEASE_MS, MAX_RELEASE_MS);
        if (std::abs(attackMs - m_attackMs) < 1e-6f && std::abs(releaseMs - m_releaseMs) < 1e-6f) return;
        m_attackMs = attackMs;
        m_releaseMs = releaseMs;
        m_bands[0].updateBallistics(attackMs * SUB_TIME_SCALE, subRelease(releaseMs));
        for (size_t b = 1; b < Bands::COUNT; ++b) {
            m_bands[b].updateBallistics(attackMs, releaseMs);
        }
        applySlowBallistics();
    }

    /** Sub runs at 2x, but never slower than the longest release the knob offers. */
    static float subRelease(float releaseMs) noexcept {
        return std::min(releaseMs * SUB_TIME_SCALE, MAX_RELEASE_MS);
    }

    /** Slow envelope of auto release: 10x the band's release, capped at MAX_RELEASE_MS. */
    void applySlowBallistics() noexcept {
        m_bands[0].updateSlowBallistics(AUTO_SLOW_ATTACK_MS * SUB_TIME_SCALE,
                                        std::min(subRelease(m_releaseMs) * AUTO_SLOW_FACTOR, MAX_RELEASE_MS));
        for (size_t b = 1; b < Bands::COUNT; ++b) {
            m_bands[b].updateSlowBallistics(AUTO_SLOW_ATTACK_MS,
                                            std::min(m_releaseMs * AUTO_SLOW_FACTOR, MAX_RELEASE_MS));
        }
    }

    double m_sampleRate = 48000.0;
    float m_attackMs = DEFAULT_ATTACK_MS;
    float m_releaseMs = DEFAULT_RELEASE_MS;

    BandSplitter m_splitter;
    std::array<BandCompressor, Bands::COUNT> m_bands;
};

} // namespace autolevel::dsp
