#pragma once

#include "LoudnessMeter.h"
#include "Leveler.h"
#include "HighPassFilter.h"
#include "MultibandCompressor.h"
#include "BandEQ.h"
#include "SafetyLimiter.h"
#include <atomic>
#include <array>
#include <cstring>
#include <cmath>
#include <algorithm>

namespace autolevel::dsp {

struct EngineParameters {
    float targetLUFS = -14.0f;
    float maxBoostDb = 12.0f;
    float maxCutDb = 12.0f;
    float levelResponse = 0.85f; // 0..1 slider, maps to memory half-life (LoudnessMeter side)
    LevelerSpeed slewSpeed = LevelerSpeed::NORMAL; // Steady-state gain slew rate (Leveler side)
    bool freezeBreakdowns = true;
    float breakdownThresholdLU = 7.0f;
    float toneSlopeDbPerOctave = -1.5f; // -3.0 to 0.0 dB/oct
    TargetProfile targetProfile = TargetProfile::MODERN_MIX;
    std::array<float, Bands::COUNT> customOffsetsDb = Bands::MODERN_CONTOUR_DB;
    float compressionAmount = 0.5f; // 0..1 slider
    float mbcAttackMs = 15.0f;      // 1 to 100 ms (Sub band runs at 2x)
    float mbcReleaseMs = 200.0f;    // 20 to 1000 ms (Sub band runs at 2x)
    float mbcDetectorRms = 0.0f;    // 0 = peak detector, 1 = RMS detector, between = blend
    /** Ratio at compressionAmount = 1.0. Default 4:1 — the plugin's long-standing behaviour. */
    float maxCompressionRatio = Bands::MAX_RATIO;
    float postMbcGainDb = 0.0f;     // -12 to +12 dB
    /** Band EQ gains in dB (+/-12), in Bands order: Sub, Bass, Low-Mid, High-Mid, Presence, Air. */
    std::array<float, Bands::COUNT> eqGainsDb{};
    EqPosition eqPosition = EqPosition::POST_MBC;
    float hpfCutoffHz = 30.0f;      // 20 to 50 Hz low-cut filter
    bool hpfEnabled = true;
    float ceilingDb = -0.3f;        // -0.3 dBFS default master ceiling
    /** Safety Limiter lookahead. OFF = the original zero-latency limiter; 1/2 ms add that much latency. */
    LimiterLookahead limiterLookahead = LimiterLookahead::OFF;
    bool bypass = false;
};

struct EngineVisualState {
    LoudnessReadings loudness;
    float appliedGainDb = 0.0f;
    float targetGainDb = 0.0f;
    bool isFrozen = false;
    std::array<float, Bands::COUNT> mbcGainReductionsDb{};
    std::array<float, Bands::COUNT> mbcThresholdsDb{};
    std::array<float, Bands::COUNT> customOffsetsDb{};
    float limiterGainReductionDb = 0.0f;
    float outputPeakDbL = -60.0f;
    float outputPeakDbR = -60.0f;
    float activeHalfLifeSeconds = 0.0f;
    TargetProfile activeProfile = TargetProfile::MODERN_MIX;
    LevelerSpeed activeSlewSpeed = LevelerSpeed::NORMAL;
    float activeToneSlope = -2.0f;
    float postMbcGainDb = 0.0f;
    float hpfCutoffHz = 30.0f;
    bool hpfEnabled = true;
};

class AutoLevelEngine {
public:
    AutoLevelEngine() = default;

    void prepare(double sampleRate) {
        m_sampleRate = sampleRate;
        m_loudnessMeter.prepare(sampleRate);
        m_leveler.prepare(sampleRate);
        m_mbc.prepare(sampleRate);
        m_eq.prepare(sampleRate);
        m_hpf.prepare(sampleRate);
        m_limiter.prepare(sampleRate);
        resetImmediate();
    }

    /**
     * Request an asynchronous reset of all measurement and filter history.
     * The work itself is deferred to the next process() call so that filter state
     * is only ever touched by the audio thread - doing it inline from the caller
     * raced with the audio callback across every stage of the chain.
     */
    void reset() {
        m_resetRequested.store(true, std::memory_order_release);
    }

    /**
     * Perform the reset synchronously. Only safe while the audio thread is not
     * running (construction / prepare()).
     */
    void resetImmediate() {
        doReset();
    }

    /**
     * Preload the leveller's gain from an external loudness estimate.
     *
     * Lets a host that already knows how loud a track is -- from a ReplayGain tag, say --
     * start the correction at the right value instead of converging to it over the first
     * several seconds. The measurement still wins: the next update() recomputes from what is
     * actually heard, so a wrong estimate costs a brief settling rather than being trusted.
     *
     * Deferred to the audio thread, like reset(), because it snaps the smoothed gain and
     * touching that from the caller's thread would race the callback.
     */
    void seedGain(float db) {
        m_seedGainDb.store(db, std::memory_order_relaxed);
        m_seedRequested.store(true, std::memory_order_release);
    }

    /**
     * Exact chain: AGC -> [EQ] -> Multiband Compressor (MBC) -> [EQ] -> Post-Gain -> HPF (Low Cut) -> Limiter
     * The Band EQ runs at exactly one of the two bracketed spots, chosen by params.eqPosition.
     */
    /** Latency the engine adds for a given lookahead setting, in samples (all of it from the limiter). */
    static int latencySamples(LimiterLookahead lookahead, double sampleRate) noexcept {
        return SafetyLimiter::lookaheadSamples(lookahead, sampleRate);
    }

    /**
     * Host-bypass path: audio is untouched except for the limiter's lookahead delay, so a
     * bypassed plugin stays time-aligned with the latency it reports.
     */
    void processBypassed(float* left, float* right, size_t numSamples, LimiterLookahead lookahead) {
        m_limiter.setLookahead(lookahead);
        m_limiter.processDelayOnly(left, right, numSamples);
    }

    void process(float* left, float* right, size_t numSamples, const EngineParameters& params) {
        // Serviced before the bypass early-out so a reset requested while bypassed
        // is not left pending indefinitely.
        if (m_resetRequested.exchange(false, std::memory_order_acquire)) {
            doReset();
        }

        // After the reset, never before: a reset and a seed requested together must leave the
        // seed standing, since the seed is the more specific instruction.
        if (m_seedRequested.exchange(false, std::memory_order_acquire)) {
            m_leveler.seedGain(m_seedGainDb.load(std::memory_order_relaxed));
        }

        if (params.bypass || numSamples == 0) {
            // Still delayed by the lookahead (a no-op when it is OFF), so toggling bypass never
            // shifts the audio in time against the latency reported to the host.
            processBypassed(left, right, numSamples, params.limiterLookahead);
            return;
        }

        // Copy and sanitize input audio: guarantee clean finite samples (discard any NaN / Inf)
        for (size_t s = 0; s < numSamples; ++s) {
            float l = left[s];
            float r = right[s];
            uint32_t ul, ur;
            std::memcpy(&ul, &l, sizeof(ul));
            std::memcpy(&ur, &r, sizeof(ur));
            left[s] = ((ul & 0x7f800000U) != 0x7f800000U) ? std::clamp(l, -8.0f, 8.0f) : 0.0f;
            right[s] = ((ur & 0x7f800000U) != 0x7f800000U) ? std::clamp(r, -8.0f, 8.0f) : 0.0f;
        }

        // 1. Measure incoming raw perceived loudness (ITU-R BS.1770-4 K-weighting + dual-gating)
        m_loudnessMeter.setLevelResponse(params.levelResponse);
        m_loudnessMeter.process(left, right, numSamples);
        LoudnessReadings readings = m_loudnessMeter.getReadings();

        // 2. Compute Leveler / AGC target gain & slew
        LevelerParams levelerParams;
        levelerParams.targetLUFS = params.targetLUFS;
        levelerParams.maxBoostDb = params.maxBoostDb;
        levelerParams.maxCutDb = params.maxCutDb;
        levelerParams.freezeBreakdowns = params.freezeBreakdowns;
        levelerParams.breakdownThresholdLU = params.breakdownThresholdLU;
        levelerParams.speed = params.slewSpeed;

        float dtSeconds = static_cast<float>(numSamples) / static_cast<float>(m_sampleRate);
        m_leveler.update(levelerParams, readings, m_loudnessMeter.getBlocksIntegrated(), dtSeconds);

        // 3. Stage 1: AGC Makeup Gain applied to audio
        m_leveler.processBlock(left, right, numSamples);

        // 4. Stage 2: Band EQ (pre-MBC position) -> 6-band Multiband Dynamic Tone Shaper
        //    (thresholds linked to tone curve & profile) -> Band EQ (post-MBC position)
        if (params.eqPosition == EqPosition::PRE_MBC) {
            m_eq.process(left, right, numSamples, params.eqGainsDb);
        }

        MBCParams mbcParams;
        mbcParams.enabled = (params.compressionAmount >= Bands::MIN_COMPRESSION);
        mbcParams.compressionAmount = params.compressionAmount;
        mbcParams.maxRatio = params.maxCompressionRatio;
        mbcParams.attackMs = params.mbcAttackMs;
        mbcParams.releaseMs = params.mbcReleaseMs;
        mbcParams.detectorRmsMix = params.mbcDetectorRms;
        mbcParams.toneSlopeDbPerOctave = params.toneSlopeDbPerOctave;
        mbcParams.profile = params.targetProfile;
        mbcParams.customOffsetsDb = params.customOffsetsDb;
        // At the plugin's actual default (targetLUFS = -14 dBFS, see EngineParameters
        // below and PluginProcessor::createParameterLayout), this comes out to -29 dBFS.
        // -24 dBFS (Bands::MBC_THRESHOLD_DB, "exactly matching Android Shaper.kt") is only
        // reached if the user manually sets Target LUFS back to the old -9 dBFS default.
        mbcParams.baseThresholdDb = params.targetLUFS - 15.0f;
        m_mbc.process(left, right, numSamples, mbcParams);

        if (params.eqPosition == EqPosition::POST_MBC) {
            m_eq.process(left, right, numSamples, params.eqGainsDb);
        }

        // 5. Stage 3: Post-MBC Gain stage (makeup/trim before safety limiter)
        if (std::abs(params.postMbcGainDb) > 0.01f) {
            float postGainLin = std::pow(10.0f, params.postMbcGainDb / 20.0f);
            for (size_t s = 0; s < numSamples; ++s) {
                left[s] *= postGainLin;
                right[s] *= postGainLin;
            }
        }

        // 5.5. Stage 3.5: 4th-Order Butterworth High-Pass Filter (Low Cut 20-50 Hz)
        // Cleanly strips inaudible subsonic rumble right before the safety limiter
        m_hpf.setCutoff(params.hpfCutoffHz, params.hpfEnabled);
        m_hpf.process(left, right, numSamples);

        // 6. Stage 4: Safety Limiter (OFF: 1ms attack, 60ms release, 20:1 ratio; or 1/2 ms lookahead brickwall)
        m_limiter.setCeilingDb(params.ceilingDb);
        m_limiter.setLookahead(params.limiterLookahead);
        m_limiter.process(left, right, numSamples);

        // 6.5. Track master output peak (linear with decay for smooth visual metering)
        float blockPeakL = 0.0f;
        float blockPeakR = 0.0f;
        for (size_t s = 0; s < numSamples; ++s) {
            blockPeakL = std::max(blockPeakL, std::abs(left[s]));
            blockPeakR = std::max(blockPeakR, std::abs(right[s]));
        }
        float decayLin = dtSeconds * 2.0f; // ~20 dB/sec decay
        m_outPeakLinL = (blockPeakL > m_outPeakLinL) ? blockPeakL : std::max(0.0f, m_outPeakLinL - decayLin);
        m_outPeakLinR = (blockPeakR > m_outPeakLinR) ? blockPeakR : std::max(0.0f, m_outPeakLinR - decayLin);

        // 7. Cache state for UI into a buffer no reader can currently be holding.
        // With only two buffers the writer reclaims the published buffer on the
        // very next block, so a reader preempted mid-copy could be overwritten.
        // Three buffers plus a cursor that never targets the published one give
        // the reader a full extra block of grace.
        size_t published = m_activeVisualIndex.load(std::memory_order_relaxed);
        size_t writeIdx = m_nextVisualIndex;
        if (writeIdx == published) writeIdx = (writeIdx + 1) % VISUAL_BUFFERS;
        auto& vs = m_visualStates[writeIdx];

        vs.loudness = readings;
        vs.appliedGainDb = m_leveler.getCurrentGainDb();
        vs.targetGainDb = m_leveler.getTargetGainDb();
        vs.isFrozen = params.freezeBreakdowns && m_leveler.isFrozen();
        vs.mbcGainReductionsDb = m_mbc.getGainReductionsDb();
        vs.customOffsetsDb = params.customOffsetsDb;
        vs.mbcThresholdsDb = Bands::thresholdsFor(
            true, params.toneSlopeDbPerOctave, params.targetProfile, params.customOffsetsDb, mbcParams.baseThresholdDb
        );
        vs.limiterGainReductionDb = m_limiter.getGainReductionDb();
        vs.outputPeakDbL = (m_outPeakLinL > 1e-4f) ? (20.0f * std::log10(m_outPeakLinL)) : -60.0f;
        vs.outputPeakDbR = (m_outPeakLinR > 1e-4f) ? (20.0f * std::log10(m_outPeakLinR)) : -60.0f;
        vs.activeHalfLifeSeconds = m_loudnessMeter.getHalfLifeSeconds();
        vs.activeProfile = params.targetProfile;
        vs.activeSlewSpeed = params.slewSpeed;
        vs.activeToneSlope = params.toneSlopeDbPerOctave;
        vs.postMbcGainDb = params.postMbcGainDb;
        vs.hpfCutoffHz = m_hpf.getCutoffHz();
        vs.hpfEnabled = m_hpf.isEnabled();

        // Atomically publish new visual state buffer
        m_activeVisualIndex.store(writeIdx, std::memory_order_release);
        m_nextVisualIndex = (writeIdx + 1) % VISUAL_BUFFERS;
    }

    EngineVisualState getVisualState() const noexcept {
        size_t readIdx = m_activeVisualIndex.load(std::memory_order_acquire);
        return m_visualStates[readIdx];
    }

private:
    void doReset() {
        m_loudnessMeter.reset();
        m_leveler.reset();
        m_mbc.reset();
        m_eq.reset();
        m_hpf.reset();
        m_limiter.reset();
        m_outPeakLinL = 0.0f;
        m_outPeakLinR = 0.0f;
    }

    std::atomic<bool> m_resetRequested{false};
    std::atomic<bool> m_seedRequested{false};
    std::atomic<float> m_seedGainDb{0.0f};
    double m_sampleRate = 48000.0;
    LoudnessMeter m_loudnessMeter;
    Leveler m_leveler;
    MultibandCompressor m_mbc;
    BandEQ m_eq;
    HighPassFilter m_hpf;
    SafetyLimiter m_limiter;

    float m_outPeakLinL = 0.0f;
    float m_outPeakLinR = 0.0f;

    static constexpr size_t VISUAL_BUFFERS = 3;
    std::array<EngineVisualState, VISUAL_BUFFERS> m_visualStates{};
    std::atomic<size_t> m_activeVisualIndex{0};
    size_t m_nextVisualIndex{1};   // audio thread only
};

} // namespace autolevel::dsp
