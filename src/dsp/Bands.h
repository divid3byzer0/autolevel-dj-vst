#pragma once

#include <array>
#include <cmath>
#include <string_view>

namespace autolevel::dsp {

class Bands {
public:
    static constexpr size_t COUNT = 6;

    static constexpr std::array<float, 5> CROSSOVERS = {
        120.0f, 400.0f, 1200.0f, 3500.0f, 8000.0f
    };

    static constexpr std::array<float, COUNT> LOWER_EDGES = {
        20.0f, 120.0f, 400.0f, 1200.0f, 3500.0f, 8000.0f
    };

    static constexpr std::array<float, COUNT> UPPER_EDGES = {
        120.0f, 400.0f, 1200.0f, 3500.0f, 8000.0f, 20000.0f
    };

    static constexpr std::array<std::string_view, COUNT> NAMES = {
        "Sub", "Bass", "Low-Mid", "High-Mid", "Presence", "Air"
    };

    /** Geometric centre frequency of each band */
    static inline float getCenterFreq(size_t band) noexcept {
        return std::sqrt(LOWER_EDGES[band] * UPPER_EDGES[band]);
    }

    /** Width of each band in octaves */
    static inline float getOctaveSpan(size_t band) noexcept {
        return static_cast<float>(std::log2(UPPER_EDGES[band] / LOWER_EDGES[band]));
    }

    /**
     * Compute baseline threshold offsets per band according to target slope in dB/octave.
     * Normalized around 1 kHz (0 dB reference offset).
     */
    static void computeThresholdOffsets(float slopeDbPerOctave, std::array<float, COUNT>& outOffsets) {
        constexpr float refFreq = 1000.0f;
        for (size_t b = 0; b < COUNT; ++b) {
            float center = getCenterFreq(b);
            float octavesFromRef = static_cast<float>(std::log2(center / refFreq));
            // e.g. slope = -3.5 dB/oct: bass (below 1k) has positive offset, treble (above 1k) has negative offset
            outOffsets[b] = octavesFromRef * slopeDbPerOctave;
        }
    }
};

} // namespace autolevel::dsp
