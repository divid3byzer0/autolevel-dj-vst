#pragma once

#include "Biquad.h"
#include <cmath>
#include <algorithm>

namespace autolevel::dsp {

enum class SubWeight {
    OFF = 0,
    LOW = 1,
    MED = 2,
    HIGH = 3
};

/**
 * Sub-Harmonic Weight Injector for AutoLevel DJ.
 * Synthesizes a clean, pitch-locked sub-octave (25-55 Hz) from low-bass content (50-110 Hz)
 * to give vintage/classic tracks and vinyl rips modern club subwoofer weight.
 * Features:
 * - 4th-order Linkwitz-Riley extraction bandpass (50-110 Hz)
 * - Zero-crossing polarity flip-flop with envelope tracking (f/2 fundamental)
 * - 25 Hz 2nd-order HighPass + 55 Hz 4th-order LowPass cleanup
 * - Adaptive native-sub energy detector (automatically prevents mud on modern tracks)
 * - Pure mono subwoofer summing (zero phase cancellation)
 */
class SubHarmonicSynthesizer {
public:
    SubHarmonicSynthesizer() = default;

    void prepare(double sampleRate) {
        m_sampleRate = sampleRate;

        // Bandpass isolation for source bass fundamental (50 - 110 Hz)
        m_extractHp.setup(Biquad::Type::HighPass, 50.0, sampleRate);
        m_extractLp.setup(Biquad::Type::LowPass, 110.0, sampleRate);

        // Native sub energy detector (< 50 Hz)
        m_nativeSubLp.setup(Biquad::Type::LowPass, 50.0, sampleRate);

        // Sub-octave post-conditioning
        m_subHp.setup(Biquad::Type::HighPass, 25.0, sampleRate);
        m_subLp.setup(Biquad::Type::LowPass, 55.0, sampleRate);

        // Envelope smoothing coefficients (~10ms attack, ~50ms release)
        m_attackCoeff = std::exp(-1.0 / (sampleRate * 0.010));
        m_releaseCoeff = std::exp(-1.0 / (sampleRate * 0.050));

        // Long-term energy smoothing (~200ms)
        m_energyCoeff = std::exp(-1.0 / (sampleRate * 0.200));

        reset();
    }

    void reset() {
        m_extractHp.reset();
        m_extractLp.reset();
        m_nativeSubLp.reset();
        m_subHp.reset();
        m_subLp.reset();

        m_prevSample = 0.0;
        m_flipFlop = 1.0;
        m_envelope = 0.0;
        m_nativeSubEnergy = 0.0;
        m_bassEnergy = 0.0;
        m_injectedLevel = 0.0f;
    }

    float getInjectedLevel() const noexcept {
        return m_injectedLevel;
    }

    void process(float* left, float* right, size_t numSamples, SubWeight weight) {
        if (weight == SubWeight::OFF || numSamples == 0) {
            m_injectedLevel = 0.0f;
            return;
        }

        float targetGain = 0.0f;
        if (weight == SubWeight::LOW) targetGain = 0.40f;
        else if (weight == SubWeight::MED) targetGain = 0.80f;
        else if (weight == SubWeight::HIGH) targetGain = 1.35f;

        float blockPeak = 0.0f;

        for (size_t s = 0; s < numSamples; ++s) {
            double mono = 0.5 * (static_cast<double>(left[s]) + static_cast<double>(right[s]));

            // 1. Extract 50-110 Hz bass band
            double bass = m_extractLp.process(m_extractHp.process(mono));

            // 2. Measure native sub energy (< 50 Hz) vs bass energy
            double nativeSub = m_nativeSubLp.process(mono);
            double subPower = nativeSub * nativeSub;
            double bassPower = bass * bass;

            m_nativeSubEnergy = m_energyCoeff * m_nativeSubEnergy + (1.0 - m_energyCoeff) * subPower;
            m_bassEnergy = m_energyCoeff * m_bassEnergy + (1.0 - m_energyCoeff) * bassPower;

            // 3. Track bass envelope
            double absBass = std::abs(bass);
            if (absBass > m_envelope) {
                m_envelope = m_attackCoeff * m_envelope + (1.0 - m_attackCoeff) * absBass;
            } else {
                m_envelope = m_releaseCoeff * m_envelope + (1.0 - m_releaseCoeff) * absBass;
            }

            // 4. Zero-crossing detector (frequency division f -> f/2)
            if (m_prevSample <= 0.0 && bass > 0.0) {
                m_flipFlop = -m_flipFlop;
            }
            m_prevSample = bass;

            // Raw sub-octave: bass signal inverted every other cycle
            double rawSub = bass * m_flipFlop;

            // 5. Cleanup filters: 25 Hz HPF (sub-sonic rumble) + 55 Hz LP4 (harmonic purity)
            double cleanSub = m_subLp.process(m_subHp.process(rawSub));

            // 6. Adaptive weight suppression: if track already has strong sub-bass (< 50 Hz),
            // attenuate injected sub so it doesn't overload or muddy the mix.
            double adaptiveScale = 1.0;
            if (m_bassEnergy > 1e-6) {
                double subToBassRatio = std::sqrt(m_nativeSubEnergy / m_bassEnergy);
                // If subToBassRatio >= 0.85 (heavy modern sub), scale down to 0.15
                // If subToBassRatio <= 0.25 (thin vintage track), full scale 1.0
                adaptiveScale = std::clamp(1.0 - (subToBassRatio - 0.25) / 0.60, 0.15, 1.0);
            }

            // 7. Inject mono sub into L & R
            float injected = static_cast<float>(cleanSub * targetGain * adaptiveScale);
            left[s] += injected;
            right[s] += injected;

            float absInj = std::abs(injected);
            if (absInj > blockPeak) {
                blockPeak = absInj;
            }
        }

        // Real-time peak decay for visual metering (~20 dB/s decay)
        if (blockPeak > m_injectedLevel) {
            m_injectedLevel = blockPeak;
        } else {
            float dt = static_cast<float>(numSamples) / static_cast<float>(m_sampleRate);
            m_injectedLevel = std::max(0.0f, m_injectedLevel - dt * 1.5f);
        }
    }

private:
    double m_sampleRate = 48000.0;

    // Filters
    LR4Filter m_extractHp;
    LR4Filter m_extractLp;
    LR4Filter m_nativeSubLp;

    Biquad m_subHp;
    LR4Filter m_subLp;

    // State
    double m_prevSample = 0.0;
    double m_flipFlop = 1.0;
    double m_envelope = 0.0;
    double m_nativeSubEnergy = 0.0;
    double m_bassEnergy = 0.0;

    double m_attackCoeff = 0.0;
    double m_releaseCoeff = 0.0;
    double m_energyCoeff = 0.0;
    float m_injectedLevel = 0.0f;
};

} // namespace autolevel::dsp
