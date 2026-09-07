#pragma once

#include "LoudnessMeter.h"
#include "Leveler.h"
#include "SubHarmonicSynthesizer.h"
#include "AirHarmonicExciter.h"
#include "MultibandCompressor.h"
#include "SafetyLimiter.h"
#include <atomic>
#include <array>

namespace autolevel::dsp {

struct EngineParameters {
    float targetLUFS = -9.0f;
    float maxBoostDb = 12.0f;
    float maxCutDb = 12.0f;
    float levelResponse = 0.25f; // 0..1 slider, maps to memory half-life
    bool freezeBreakdowns = true;
    float toneSlopeDbPerOctave = -2.0f; // -6.0 to 0.0 dB/oct
    TargetProfile targetProfile = TargetProfile::MODERN_MIX;
    MBCSpeed mbcSpeed = MBCSpeed::NORMAL;
    SubWeight subWeight = SubWeight::OFF;
    AirWeight airWeight = AirWeight::OFF;
    float compressionAmount = 0.5f; // 0..1 slider
    float postMbcGainDb = 0.0f;     // -12 to +12 dB
    float ceilingDb = -1.5f;
    bool bypass = false;
};

struct EngineVisualState {
    LoudnessReadings loudness;
    float appliedGainDb = 0.0f;
    float targetGainDb = 0.0f;
    bool isFrozen = false;
    std::array<float, Bands::COUNT> mbcGainReductionsDb{};
    std::array<float, Bands::COUNT> mbcThresholdsDb{};
    float limiterGainReductionDb = 0.0f;
    float activeHalfLifeSeconds = 0.0f;
    TargetProfile activeProfile = TargetProfile::MODERN_MIX;
    MBCSpeed activeMbcSpeed = MBCSpeed::NORMAL;
    SubWeight activeSubWeight = SubWeight::OFF;
    float subInjectedLevel = 0.0f;
    AirWeight activeAirWeight = AirWeight::OFF;
    float airInjectedLevel = 0.0f;
    float activeToneSlope = -2.0f;
    float postMbcGainDb = 0.0f;
};

class AutoLevelEngine {
public:
    AutoLevelEngine() = default;

    void prepare(double sampleRate) {
        m_sampleRate = sampleRate;
        m_loudnessMeter.prepare(sampleRate);
        m_leveler.prepare(sampleRate);
        m_subHarmonics.prepare(sampleRate);
        m_airExciter.prepare(sampleRate);
        m_mbc.prepare(sampleRate);
        m_limiter.prepare(sampleRate);
        reset();
    }

    void reset() {
        m_loudnessMeter.reset();
        m_leveler.reset();
        m_subHarmonics.reset();
        m_airExciter.reset();
        m_mbc.reset();
        m_limiter.reset();
    }

    /**
     * Exact chain: AGC -> Sub-Harmonics -> Air Exciter -> Multiband Compressor (MBC) -> Post-Gain -> Limiter
     */
    void process(float* left, float* right, size_t numSamples, const EngineParameters& params) {
        if (params.bypass || numSamples == 0) {
            return;
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

        float dtSeconds = static_cast<float>(numSamples) / static_cast<float>(m_sampleRate);
        m_leveler.update(levelerParams, readings, m_loudnessMeter.getBlocksIntegrated(), dtSeconds);

        // 3. Stage 1: AGC Makeup Gain applied to audio
        m_leveler.processBlock(left, right, numSamples);

        // 3.5. Stage 1.5: Sub-Harmonic Weight Injector (clean mono sub-octave)
        m_subHarmonics.process(left, right, numSamples, params.subWeight);

        // 3.6. Stage 1.6: High-Frequency Air Exciter (silky top-end sheen)
        m_airExciter.process(left, right, numSamples, params.airWeight);

        // 4. Stage 2: 6-band Multiband Dynamic Tone Shaper (thresholds linked to tone curve & profile)
        MBCParams mbcParams;
        mbcParams.enabled = (params.compressionAmount >= Bands::MIN_COMPRESSION);
        mbcParams.compressionAmount = params.compressionAmount;
        mbcParams.toneSlopeDbPerOctave = params.toneSlopeDbPerOctave;
        mbcParams.profile = params.targetProfile;
        mbcParams.baseThresholdDb = params.targetLUFS - 15.0f; // Exact -24 dBFS at default -9 LUFS
        mbcParams.speed = params.mbcSpeed;
        m_mbc.process(left, right, numSamples, mbcParams);

        // 5. Stage 3: Post-MBC Gain stage (makeup/trim before safety limiter)
        if (std::abs(params.postMbcGainDb) > 0.01f) {
            float postGainLin = std::pow(10.0f, params.postMbcGainDb / 20.0f);
            for (size_t s = 0; s < numSamples; ++s) {
                left[s] *= postGainLin;
                right[s] *= postGainLin;
            }
        }

        // 6. Stage 4: Safety Limiter (1ms attack, 60ms release, 20:1 ratio)
        m_limiter.setCeilingDb(params.ceilingDb);
        m_limiter.process(left, right, numSamples);

        // 7. Cache state for UI
        m_visualState.loudness = readings;
        m_visualState.appliedGainDb = m_leveler.getCurrentGainDb();
        m_visualState.targetGainDb = m_leveler.getTargetGainDb();
        m_visualState.isFrozen = m_leveler.isFrozen();
        m_visualState.mbcGainReductionsDb = m_mbc.getGainReductionsDb();
        m_visualState.mbcThresholdsDb = Bands::thresholdsFor(
            true, params.toneSlopeDbPerOctave, params.targetProfile, mbcParams.baseThresholdDb
        );
        m_visualState.limiterGainReductionDb = m_limiter.getGainReductionDb();
        m_visualState.activeHalfLifeSeconds = m_loudnessMeter.getHalfLifeSeconds();
        m_visualState.activeProfile = params.targetProfile;
        m_visualState.activeMbcSpeed = params.mbcSpeed;
        m_visualState.activeSubWeight = params.subWeight;
        m_visualState.subInjectedLevel = m_subHarmonics.getInjectedLevel();
        m_visualState.activeAirWeight = params.airWeight;
        m_visualState.airInjectedLevel = m_airExciter.getInjectedLevel();
        m_visualState.activeToneSlope = params.toneSlopeDbPerOctave;
        m_visualState.postMbcGainDb = params.postMbcGainDb;
    }

    EngineVisualState getVisualState() const noexcept {
        return m_visualState;
    }

private:
    double m_sampleRate = 48000.0;
    LoudnessMeter m_loudnessMeter;
    Leveler m_leveler;
    SubHarmonicSynthesizer m_subHarmonics;
    AirHarmonicExciter m_airExciter;
    MultibandCompressor m_mbc;
    SafetyLimiter m_limiter;

    EngineVisualState m_visualState;
};

} // namespace autolevel::dsp
