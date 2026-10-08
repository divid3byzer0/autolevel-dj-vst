#pragma once

#include "Bands.h"
#include "Biquad.h"
#include <array>
#include <cmath>
#include <algorithm>

namespace autolevel::dsp {

/** Where the Band EQ sits relative to the Multiband Compressor. It is always after the AGC. */
enum class EqPosition {
    PRE_MBC,
    POST_MBC
};

/**
 * 6-band EQ on the same bands as the Multiband Compressor (Sub, Bass, Low-Mid, High-Mid,
 * Presence, Air).
 *
 * The four middle bands are peaking filters centred on the band's geometric centre frequency
 * and as wide, in octaves, as the band itself. Sub is a low shelf and Air a high shelf, cornered
 * on the band's outer crossover (120 Hz / 8 kHz), so they cover everything beyond their edge the
 * way the compressor's outer bands do. Each band's gain is therefore (close to) what the control
 * says at that band's centre, and neighbouring bands blend smoothly instead of leaving steps.
 *
 * At 0 dB a band is an exact passthrough, so an untouched EQ is bit-transparent. Gains are
 * smoothed in dB and the filters retuned every SUB_BLOCK samples, so moving a band never zippers.
 */
class BandEQ {
public:
    static constexpr float MAX_GAIN_DB = 12.0f;

    BandEQ() = default;

    void prepare(double sampleRate) {
        m_sampleRate = sampleRate;
        m_smoothCoeff = std::exp(-static_cast<double>(SUB_BLOCK) / (sampleRate * SMOOTH_SECONDS));

        // Band edges 20 Hz | 120 | 400 | 1200 | 3500 | 8000 | 20 kHz; centre and width follow.
        const double edges[Bands::COUNT + 1] = {
            20.0, Bands::CROSSOVERS[0], Bands::CROSSOVERS[1], Bands::CROSSOVERS[2],
            Bands::CROSSOVERS[3], Bands::CROSSOVERS[4], 20000.0
        };
        for (size_t b = 0; b < Bands::COUNT; ++b) {
            m_centreHz[b] = std::sqrt(edges[b] * edges[b + 1]);
            double octaves = std::log2(edges[b + 1] / edges[b]);
            // RBJ bandwidth-in-octaves -> Q
            m_q[b] = 1.0 / (2.0 * std::sinh(std::log(2.0) / 2.0 * octaves));
        }
        reset();
    }

    void reset() {
        for (auto& ch : m_filters) {
            for (auto& f : ch) f.reset();
        }
        m_gainDb.fill(0.0);
        for (size_t b = 0; b < Bands::COUNT; ++b) design(b);
    }

    void process(float* left, float* right, size_t numSamples, const std::array<float, Bands::COUNT>& gainsDb) {
        std::array<double, Bands::COUNT> target;
        for (size_t b = 0; b < Bands::COUNT; ++b) {
            target[b] = static_cast<double>(std::clamp(gainsDb[b], -MAX_GAIN_DB, MAX_GAIN_DB));
        }

        for (size_t pos = 0; pos < numSamples; pos += SUB_BLOCK) {
            const size_t end = std::min(pos + SUB_BLOCK, numSamples);

            // Glide each band's gain toward its target and retune only the bands that moved.
            for (size_t b = 0; b < Bands::COUNT; ++b) {
                double diff = target[b] - m_gainDb[b];
                if (std::abs(diff) < 1e-9) continue;   // settled (the snap below makes this exact)
                // Snap once inside 0.005 dB: it finishes the glide, and a band returned to 0 dB
                // lands on the exact passthrough rather than hovering near it.
                m_gainDb[b] = (std::abs(diff) < 0.005) ? target[b] : target[b] - m_smoothCoeff * diff;
                design(b);
            }

            for (size_t s = pos; s < end; ++s) {
                double l = left[s];
                double r = right[s];
                for (size_t b = 0; b < Bands::COUNT; ++b) {
                    l = m_filters[0][b].process(l);
                    r = m_filters[1][b].process(r);
                }
                left[s] = static_cast<float>(l);
                right[s] = static_cast<float>(r);
            }
        }
    }

private:
    static constexpr size_t SUB_BLOCK = 16;
    static constexpr double SMOOTH_SECONDS = 0.015;
    static constexpr double SHELF_Q = 0.7071067811865476;

    /** Recompute band b's coefficients from its current (smoothed) gain; RBJ cookbook forms. */
    void design(size_t b) {
        double gainDb = m_gainDb[b];
        double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;

        if (std::abs(gainDb) > 1e-9) {
            const double A = std::pow(10.0, gainDb / 40.0);
            if (b == 0 || b == Bands::COUNT - 1) {
                const bool low = (b == 0);
                const double w0 = 2.0 * PI * (low ? Bands::CROSSOVERS.front() : Bands::CROSSOVERS.back()) / m_sampleRate;
                const double cs = std::cos(w0);
                const double beta = 2.0 * std::sqrt(A) * (std::sin(w0) / (2.0 * SHELF_Q));
                double a0;
                if (low) {
                    b0 = A * ((A + 1) - (A - 1) * cs + beta);
                    b1 = 2 * A * ((A - 1) - (A + 1) * cs);
                    b2 = A * ((A + 1) - (A - 1) * cs - beta);
                    a0 = (A + 1) + (A - 1) * cs + beta;
                    a1 = -2 * ((A - 1) + (A + 1) * cs);
                    a2 = (A + 1) + (A - 1) * cs - beta;
                } else {
                    b0 = A * ((A + 1) + (A - 1) * cs + beta);
                    b1 = -2 * A * ((A - 1) + (A + 1) * cs);
                    b2 = A * ((A + 1) + (A - 1) * cs - beta);
                    a0 = (A + 1) - (A - 1) * cs + beta;
                    a1 = 2 * ((A - 1) - (A + 1) * cs);
                    a2 = (A + 1) - (A - 1) * cs - beta;
                }
                b0 /= a0; b1 /= a0; b2 /= a0; a1 /= a0; a2 /= a0;
            } else {
                const double w0 = 2.0 * PI * m_centreHz[b] / m_sampleRate;
                const double cs = std::cos(w0);
                const double alpha = std::sin(w0) / (2.0 * m_q[b]);
                const double a0 = 1.0 + alpha / A;
                b0 = (1.0 + alpha * A) / a0;
                b1 = (-2.0 * cs) / a0;
                b2 = (1.0 - alpha * A) / a0;
                a1 = b1;
                a2 = (1.0 - alpha / A) / a0;
            }
        }
        // gainDb == 0 keeps the defaults above: b0 = 1, everything else 0 - a pure passthrough.

        for (auto& ch : m_filters) ch[b].setCoefficients(b0, b1, b2, a1, a2);
    }

    double m_sampleRate = 48000.0;
    double m_smoothCoeff = 0.0;
    std::array<double, Bands::COUNT> m_centreHz{};
    std::array<double, Bands::COUNT> m_q{};
    std::array<double, Bands::COUNT> m_gainDb{};
    std::array<std::array<Biquad, Bands::COUNT>, 2> m_filters;
};

} // namespace autolevel::dsp
