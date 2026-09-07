#pragma once

#include <cmath>
#include <array>

namespace autolevel::dsp {

/**
 * 2nd-order Biquad filter (Direct Form II Transposed)
 * Provides Butterworth Low-pass and High-pass filters with Q = 0.70710678 (1/sqrt(2)).
 * Cascading two identical Butterworth filters yields a 4th-order Linkwitz-Riley (LR4) filter.
 */
class Biquad {
public:
    enum class Type { LowPass, HighPass };

    Biquad() = default;

    void setup(Type type, double cutoffHz, double sampleRate) {
        double omega = 2.0 * M_PI * cutoffHz / sampleRate;
        double sn = std::sin(omega);
        double cs = std::cos(omega);
        double alpha = sn / (2.0 * 0.7071067811865475); // Q = 1/sqrt(2)

        double a0 = 1.0 + alpha;

        if (type == Type::LowPass) {
            m_b[0] = ((1.0 - cs) / 2.0) / a0;
            m_b[1] = (1.0 - cs) / a0;
            m_b[2] = ((1.0 - cs) / 2.0) / a0;
        } else {
            m_b[0] = ((1.0 + cs) / 2.0) / a0;
            m_b[1] = -(1.0 + cs) / a0;
            m_b[2] = ((1.0 + cs) / 2.0) / a0;
        }

        m_a[1] = (-2.0 * cs) / a0;
        m_a[2] = (1.0 - alpha) / a0;

        reset();
    }

    void reset() {
        m_z1 = 0.0;
        m_z2 = 0.0;
    }

    inline double process(double in) noexcept {
        double out = m_b[0] * in + m_z1;
        m_z1 = m_b[1] * in - m_a[1] * out + m_z2;
        m_z2 = m_b[2] * in - m_a[2] * out;
        return out;
    }

private:
    std::array<double, 3> m_b{1.0, 0.0, 0.0};
    std::array<double, 3> m_a{1.0, 0.0, 0.0};
    double m_z1 = 0.0;
    double m_z2 = 0.0;
};

/**
 * 4th-order Linkwitz-Riley filter (LR4)
 * Cascades two 2nd-order Butterworth filters.
 */
class LR4Filter {
public:
    LR4Filter() = default;

    void setup(Biquad::Type type, double cutoffHz, double sampleRate) {
        m_biquad1.setup(type, cutoffHz, sampleRate);
        m_biquad2.setup(type, cutoffHz, sampleRate);
    }

    void reset() {
        m_biquad1.reset();
        m_biquad2.reset();
    }

    inline double process(double in) noexcept {
        return m_biquad2.process(m_biquad1.process(in));
    }

private:
    Biquad m_biquad1;
    Biquad m_biquad2;
};

} // namespace autolevel::dsp
