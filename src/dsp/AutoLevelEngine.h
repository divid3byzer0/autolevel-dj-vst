#pragma once

#include "LoudnessMeter.h"
#include "Leveler.h"
#include "MultibandCompressor.h"
#include "SafetyLimiter.h"
#include <atomic>
#include <array>

namespace autolevel::dsp {

struct EngineParameters {
    float targetLUFS = -9.0f;
    float maxBoostDb = 6.0f;
    float maxCutDb = 12.0f;
    float slewSpeedDbPerSec = 0.75f;
    bool freezeBreakdowns = true;
    float toneSlopeDbPerOctave = -3.75f;
    float compressionAmount = 0.5f;
    float ceilingDb = -0.5f;
    bool bypass = false;
};

struct EngineVisualState {
    LoudnessReadings loudness;
    float appliedGainDb = 0.0f;
    float targetGainDb = 0.0f;
    bool isFrozen = false;
    std::array<float, Bands::COUNT> mbcGainReductionsDb{};
    float limiterGainReductionDb = 0.0f;
};

class AutoLevelEngine {
public:
    AutoLevelEngine() = default;

    void prepare(double sampleRate) {
        m_sampleRate = sampleRate;
        m_loudnessMeter.prepare(sampleRate);
        m_leveler.prepare(sampleRate);
        m_mbc.prepare(sampleRate);
        m_limiter.prepare(sampleRate);
        reset();
    }

    void reset() {
        m_loudnessMeter.reset();
        m_leveler.reset();
        m_mbc.reset();
        m_limiter.reset();
    }

    void process(float* left, float* right, size_t numSamples, const EngineParameters& params) {
        if (params.bypass || numSamples == 0) {
            return;
        }

        // 1. Measure incoming raw loudness
        m_loudnessMeter.process(left, right, numSamples);
        LoudnessReadings readings = m_loudnessMeter.getReadings();

        // 2. Update leveler target & slew
        LevelerParams levelerParams;
        levelerParams.targetLUFS = params.targetLUFS;
        levelerParams.maxBoostDb = params.maxBoostDb;
        levelerParams.maxCutDb = params.maxCutDb;
        levelerParams.baseSlewDbPerSec = params.slewSpeedDbPerSec;
        levelerParams.freezeBreakdowns = params.freezeBreakdowns;

        float dtSeconds = static_cast<float>(numSamples) / static_cast<float>(m_sampleRate);
        m_leveler.update(levelerParams, readings, m_loudnessMeter.getBlocksIntegrated(), dtSeconds);

        // 3. Apply leveling gain
        m_leveler.processBlock(left, right, numSamples);

        // 4. Multiband Compressor (tone shaping & spectral management)
        MBCParams mbcParams;
        mbcParams.enabled = (params.compressionAmount > 0.001f);
        mbcParams.compressionAmount = params.compressionAmount;
        mbcParams.toneSlopeDbPerOctave = params.toneSlopeDbPerOctave;
        mbcParams.targetLUFS = params.targetLUFS;
        m_mbc.process(left, right, numSamples, mbcParams);

        // 5. Safety Limiter (protects amps and speakers from any transient spike or clipping)
        m_limiter.setCeilingDb(params.ceilingDb);
        m_limiter.process(left, right, numSamples);

        // 6. Cache state for UI
        m_visualState.loudness = readings;
        m_visualState.appliedGainDb = m_leveler.getCurrentGainDb();
        m_visualState.targetGainDb = m_leveler.getTargetGainDb();
        m_visualState.isFrozen = m_leveler.isFrozen();
        m_visualState.mbcGainReductionsDb = m_mbc.getGainReductionsDb();
        m_visualState.limiterGainReductionDb = m_limiter.getGainReductionDb();
    }

    EngineVisualState getVisualState() const noexcept {
        return m_visualState;
    }

private:
    double m_sampleRate = 48000.0;
    LoudnessMeter m_loudnessMeter;
    Leveler m_leveler;
    MultibandCompressor m_mbc;
    SafetyLimiter m_limiter;

    EngineVisualState m_visualState;
};

} // namespace autolevel::dsp
