#pragma once

#include "LoudnessMeter.h"
#include "Leveler.h"
#include "DynamicBassLift.h"
#include "DynamicAirLift.h"
#include "HighPassFilter.h"
#include "MultibandCompressor.h"
#include "SafetyLimiter.h"
#include <atomic>
#include <array>
#include <cstring>
#include <cmath>
#include <algorithm>

namespace autolevel::dsp {

// Backward-compatible type aliases for existing codebase & tests
using SubWeight = BassLiftMode;
using AirWeight = AirLiftMode;

struct EngineParameters {
    float targetLUFS = -14.0f;
    float maxBoostDb = 12.0f;
    float maxCutDb = 12.0f;
    float levelResponse = 0.85f; // 0..1 slider, maps to memory half-life
    bool freezeBreakdowns = true;
    float breakdownThresholdLU = 7.0f;
    float toneSlopeDbPerOctave = -1.5f; // -3.0 to 0.0 dB/oct
    TargetProfile targetProfile = TargetProfile::MODERN_MIX;
    std::array<float, Bands::COUNT> customOffsetsDb = Bands::MODERN_CONTOUR_DB;
    MBCSpeed mbcSpeed = MBCSpeed::NORMAL;
    bool mbcAutoMakeup = true;
    BassLiftMode bassLift = BassLiftMode::OFF;
    AirLiftMode airLift = AirLiftMode::OFF;
    // Backward compatibility aliases
    SubWeight subWeight = SubWeight::OFF;
    AirWeight airWeight = AirWeight::OFF;
    float compressionAmount = 0.5f; // 0..1 slider
    float postMbcGainDb = 0.0f;     // -12 to +12 dB
    float hpfCutoffHz = 30.0f;      // 20 to 50 Hz low-cut filter
    bool hpfEnabled = true;
    float ceilingDb = -0.3f;        // -0.3 dBFS default master ceiling
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
    MBCSpeed activeMbcSpeed = MBCSpeed::NORMAL;
    bool activeMbcAutoMakeup = true;
    float mbcAutoMakeupGainDb = 0.0f;
    BassLiftMode activeBassLift = BassLiftMode::OFF;
    float bassLiftDb = 0.0f;
    AirLiftMode activeAirLift = AirLiftMode::OFF;
    float airLiftDb = 0.0f;
    // Compatibility fields for UI meters
    SubWeight activeSubWeight = SubWeight::OFF;
    float subInjectedLevel = 0.0f;
    AirWeight activeAirWeight = AirWeight::OFF;
    float airInjectedLevel = 0.0f;
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
        m_bassLift.prepare(sampleRate);
        m_airLift.prepare(sampleRate);
        m_mbc.prepare(sampleRate);
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
     * Exact chain: AGC -> Bass Lift -> Air Lift -> Multiband Compressor (MBC) -> Post-Gain -> HPF (Low Cut) -> Limiter
     */
    void process(float* left, float* right, size_t numSamples, const EngineParameters& params) {
        // Serviced before the bypass early-out so a reset requested while bypassed
        // is not left pending indefinitely.
        if (m_resetRequested.exchange(false, std::memory_order_acquire)) {
            doReset();
        }

        if (params.bypass || numSamples == 0) {
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

        float dtSeconds = static_cast<float>(numSamples) / static_cast<float>(m_sampleRate);
        m_leveler.update(levelerParams, readings, m_loudnessMeter.getBlocksIntegrated(), dtSeconds);

        // 3. Stage 1: AGC Makeup Gain applied to audio
        m_leveler.processBlock(left, right, numSamples);

        // 3.5. Stage 1.5: Dynamic Bass Lift (< 100 Hz, 100% distortion-free)
        BassLiftMode effBass = (params.bassLift != BassLiftMode::OFF) ? params.bassLift : params.subWeight;
        m_bassLift.process(left, right, numSamples, effBass);

        // 3.6. Stage 1.6: Dynamic Air Lift (> 9.5 kHz, 100% distortion-free)
        AirLiftMode effAir = (params.airLift != AirLiftMode::OFF) ? params.airLift : params.airWeight;
        m_airLift.process(left, right, numSamples, effAir);

        // 4. Stage 2: 6-band Multiband Dynamic Tone Shaper (thresholds linked to tone curve & profile)
        MBCParams mbcParams;
        mbcParams.enabled = (params.compressionAmount >= Bands::MIN_COMPRESSION);
        mbcParams.compressionAmount = params.compressionAmount;
        mbcParams.toneSlopeDbPerOctave = params.toneSlopeDbPerOctave;
        mbcParams.profile = params.targetProfile;
        mbcParams.customOffsetsDb = params.customOffsetsDb;
        // At the plugin's actual default (targetLUFS = -14 dBFS, see EngineParameters
        // below and PluginProcessor::createParameterLayout), this comes out to -29 dBFS.
        // -24 dBFS (Bands::MBC_THRESHOLD_DB, "exactly matching Android Shaper.kt") is only
        // reached if the user manually sets Target LUFS back to the old -9 dBFS default.
        mbcParams.baseThresholdDb = params.targetLUFS - 15.0f;
        mbcParams.speed = params.mbcSpeed;
        mbcParams.autoMakeup = params.mbcAutoMakeup;
        m_mbc.process(left, right, numSamples, mbcParams);

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

        // 6. Stage 4: Safety Limiter (1ms attack, 60ms release, 20:1 ratio)
        m_limiter.setCeilingDb(params.ceilingDb);
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
        vs.activeMbcSpeed = params.mbcSpeed;
        vs.activeMbcAutoMakeup = params.mbcAutoMakeup;
        vs.mbcAutoMakeupGainDb = m_mbc.getAutoMakeupGainDb();
        vs.activeBassLift = effBass;
        vs.bassLiftDb = m_bassLift.getLiftDb();
        vs.activeAirLift = effAir;
        vs.airLiftDb = m_airLift.getLiftDb();
        // UI compatibility
        vs.activeSubWeight = effBass;
        vs.subInjectedLevel = m_bassLift.getLiftDb() / 6.5f * 0.25f; // scale to 0..0.25 for LED rack
        vs.activeAirWeight = effAir;
        vs.airInjectedLevel = m_airLift.getLiftDb() / 6.5f * 0.25f; // scale to 0..0.25 for LED rack
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
        m_bassLift.reset();
        m_airLift.reset();
        m_mbc.reset();
        m_hpf.reset();
        m_limiter.reset();
        m_outPeakLinL = 0.0f;
        m_outPeakLinR = 0.0f;
    }

    std::atomic<bool> m_resetRequested{false};
    double m_sampleRate = 48000.0;
    LoudnessMeter m_loudnessMeter;
    Leveler m_leveler;
    DynamicBassLift m_bassLift;
    DynamicAirLift m_airLift;
    MultibandCompressor m_mbc;
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
