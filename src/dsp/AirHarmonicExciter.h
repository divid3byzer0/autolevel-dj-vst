#pragma once

#include "Biquad.h"
#include <cmath>
#include <algorithm>

namespace autolevel::dsp {

enum class AirWeight {
    OFF = 0,
    LOW = 1,
    MED = 2,
    HIGH = 3
};

/**
 * High-Frequency Air Exciter for AutoLevel DJ.
 * Synthesizes silky, open high-frequency "air" and sheen (10-18 kHz) from upper-mid
 * content (4-8 kHz) to give vintage/classic 70s tracks, cassette transfers, and lo-fi audio
 * the sparkling presence and open top-end of modern club masters.
 *
 * Features:
 * - Stereo 4th-order Linkwitz-Riley extraction bandpass (4.0 - 8.0 kHz)
 * - Asymmetric polynomial harmonic exciter (2nd & 3rd order harmonics for octave doubling)
 * - 4th-order HighPass filter (9.0 kHz) for pristine air isolation (zero mid clutter)
 * - 19.5 kHz anti-aliasing low-pass filter
 * - Adaptive native-air energy detector (automatically prevents harshness on modern bright tracks)
 * - True stereo processing preserving spatial imaging and width
 */
class AirHarmonicExciter {
public:
    AirHarmonicExciter() = default;

    void prepare(double sampleRate) {
        m_sampleRate = sampleRate;

        // Bandpass isolation for upper-mid excitation source (4.0 - 8.0 kHz)
        m_extractHp[0].setup(Biquad::Type::HighPass, 4000.0, sampleRate);
        m_extractHp[1].setup(Biquad::Type::HighPass, 4000.0, sampleRate);
        m_extractLp[0].setup(Biquad::Type::LowPass, 8000.0, sampleRate);
        m_extractLp[1].setup(Biquad::Type::LowPass, 8000.0, sampleRate);

        // Native air energy detector (> 10.0 kHz)
        m_nativeAirHp[0].setup(Biquad::Type::HighPass, 10000.0, sampleRate);
        m_nativeAirHp[1].setup(Biquad::Type::HighPass, 10000.0, sampleRate);

        // Air sculpting: 9.0 kHz 4th-order HPF strips mid-range distortion
        m_airHp[0].setup(Biquad::Type::HighPass, 9000.0, sampleRate);
        m_airHp[1].setup(Biquad::Type::HighPass, 9000.0, sampleRate);

        // 19.5 kHz gentle low-pass to avoid Nyquist cramping
        double lpFreq = std::min(19500.0, sampleRate * 0.45);
        m_airLp[0].setup(Biquad::Type::LowPass, lpFreq, sampleRate);
        m_airLp[1].setup(Biquad::Type::LowPass, lpFreq, sampleRate);

        // Running energy smoothing (~200ms)
        m_energyCoeff = std::exp(-1.0 / (sampleRate * 0.200));

        reset();
    }

    void reset() {
        for (int ch = 0; ch < 2; ++ch) {
            m_extractHp[ch].reset();
            m_extractLp[ch].reset();
            m_nativeAirHp[ch].reset();
            m_airHp[ch].reset();
            m_airLp[ch].reset();
        }

        m_nativeAirEnergy = 0.0;
        m_midEnergy = 0.0;
        m_injectedLevel = 0.0f;
    }

    float getInjectedLevel() const noexcept {
        return m_injectedLevel;
    }

    void process(float* left, float* right, size_t numSamples, AirWeight weight) {
        if (weight == AirWeight::OFF || numSamples == 0) {
            m_injectedLevel = 0.0f;
            return;
        }

        float targetGain = 0.0f;
        if (weight == AirWeight::LOW) targetGain = 0.38f;
        else if (weight == AirWeight::MED) targetGain = 0.75f;
        else if (weight == AirWeight::HIGH) targetGain = 1.30f;

        float blockPeak = 0.0f;

        for (size_t s = 0; s < numSamples; ++s) {
            double inL = static_cast<double>(left[s]);
            double inR = static_cast<double>(right[s]);

            // 1. Extract 4.0 - 8.0 kHz upper-mid band
            double midL = m_extractLp[0].process(m_extractHp[0].process(inL));
            double midR = m_extractLp[1].process(m_extractHp[1].process(inR));

            // 2. Measure native high "air" energy (> 10 kHz) vs upper-mid energy
            double nativeAirL = m_nativeAirHp[0].process(inL);
            double nativeAirR = m_nativeAirHp[1].process(inR);

            double airPower = 0.5 * (nativeAirL * nativeAirL + nativeAirR * nativeAirR);
            double midPower = 0.5 * (midL * midL + midR * midR);

            m_nativeAirEnergy = m_energyCoeff * m_nativeAirEnergy + (1.0 - m_energyCoeff) * airPower;
            m_midEnergy = m_energyCoeff * m_midEnergy + (1.0 - m_energyCoeff) * midPower;

            // 3. Nonlinear harmonic generator:
            // Asymmetric soft polynomial saturation generates 2nd harmonic (octave doubling 4-8 kHz -> 8-16 kHz)
            // and 3rd harmonic sheen with robust drive.
            auto generateHarmonics = [](double x) noexcept -> double {
                double xNorm = std::clamp(x * 3.5, -2.0, 2.0);
                // Even (2nd order) + Odd (3rd order) harmonic expansion
                double evenHarmonic = 0.85 * (xNorm * xNorm);
                double oddHarmonic = 0.40 * (xNorm - (xNorm * xNorm * xNorm) / 3.0);
                return evenHarmonic + oddHarmonic;
            };

            double rawAirL = generateHarmonics(midL);
            double rawAirR = generateHarmonics(midR);

            // 4. Air sculpting: 9 kHz HP4 strips original mid-range & DC, leaves pure crystalline sheen
            double cleanAirL = m_airLp[0].process(m_airHp[0].process(rawAirL));
            double cleanAirR = m_airLp[1].process(m_airHp[1].process(rawAirR));

            // 5. Adaptive weight suppression:
            // If track already has strong top-octave air (> 10 kHz), attenuate injected harmonics
            // to keep modern bright mixes from becoming piercing or harsh.
            double adaptiveScale = 1.0;
            if (m_midEnergy > 1e-8) {
                double airToMidRatio = std::sqrt(m_nativeAirEnergy / m_midEnergy);
                // If airToMidRatio >= 0.65 (bright modern track), attenuate down to 0.08
                // If airToMidRatio <= 0.25 (dark vintage track), full scale 1.0
                adaptiveScale = std::clamp(1.0 - (airToMidRatio - 0.25) / 0.40, 0.08, 1.0);
            }

            // 6. Inject pristine air into L & R
            float injectedL = static_cast<float>(cleanAirL * targetGain * adaptiveScale);
            float injectedR = static_cast<float>(cleanAirR * targetGain * adaptiveScale);

            left[s] += injectedL;
            right[s] += injectedR;

            float samplePeak = std::max(std::abs(injectedL), std::abs(injectedR));
            if (samplePeak > blockPeak) {
                blockPeak = samplePeak;
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

    // Filters for L & R
    LR4Filter m_extractHp[2];
    LR4Filter m_extractLp[2];
    LR4Filter m_nativeAirHp[2];

    LR4Filter m_airHp[2];
    Biquad m_airLp[2];

    // State
    double m_nativeAirEnergy = 0.0;
    double m_midEnergy = 0.0;

    double m_energyCoeff = 0.0;
    float m_injectedLevel = 0.0f;
};

} // namespace autolevel::dsp
