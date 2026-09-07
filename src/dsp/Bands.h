#pragma once

#include <array>
#include <cmath>
#include <string_view>
#include <numeric>

namespace autolevel::dsp {

enum class TargetProfile {
    PINK_NOISE,
    MODERN_MIX
};

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

    /** log2 of the centre frequency */
    static inline float getOctave(size_t band) noexcept {
        return static_cast<float>(std::log2(getCenterFreq(band)));
    }

    // Precomputed octaves and spans matching Android exactly:
    // centres = [48.9898, 219.089, 692.82, 2049.39, 5291.5, 12649.1]
    // octaves = [5.61438, 7.77537, 9.43632, 11.0009, 12.3694, 13.6267]
    // spans   = [2.58496, 1.73696, 1.58496, 1.54432, 1.19264, 1.32192]
    static constexpr std::array<float, COUNT> OCTAVES = {
        5.614384f, 7.775374f, 9.436320f, 11.000922f, 12.369400f, 13.626772f
    };

    static constexpr std::array<float, COUNT> OCTAVE_SPANS = {
        2.584963f, 1.736966f, 1.584963f, 1.544321f, 1.192645f, 1.321928f
    };

    static constexpr float MEAN_OCTAVE = 9.970562f;

    static constexpr float MIN_TONE_SLOPE = -6.0f;
    static constexpr float MAX_TONE_SLOPE = 0.0f;
    static constexpr float DEFAULT_TONE_SLOPE = -2.0f;

    static constexpr float MBC_THRESHOLD_DB = -24.0f;
    static constexpr float MAX_RATIO = 4.0f;
    static constexpr float MIN_COMPRESSION = 0.02f;

    /**
     * Relative contour offsets (dB) for the MODERN_MIX profile from Android Shaper.kt:
     * - Band 0 (20–120 Hz): +1.5 dB higher threshold -> punchy, full-bodied bass with headroom
     * - Band 1 (120–400 Hz): -2.5 dB lower threshold -> clamps down on muddy/boxy build-up
     * - Band 2 (400–1200 Hz): +0.5 dB neutral midrange body
     * - Band 3 (1200–3500 Hz): +1.0 dB vocal presence and clarity
     * - Band 4 (3500–8000 Hz): -2.0 dB lower threshold -> controls harshness, bite, and sibilance
     * - Band 5 (8000–20000 Hz): +1.5 dB higher threshold -> open, airy sparkle
     * Zero-sum contour (average offset is 0.0 dB) to preserve overall calibration.
     */
    static constexpr std::array<float, COUNT> MODERN_CONTOUR_DB = {
        +1.5f, -2.5f, +0.5f, +1.0f, -2.0f, +1.5f
    };

    /**
     * Per-band compressor thresholds in dBFS, exactly matching Android Shaper.kt.
     * Tilted puts each threshold on the tone curve, converted from power-per-octave
     * into the share-of-total-power that band levels are measured in (bandwidth term),
     * then re-centred so the average threshold is unchanged (-24 dB).
     */
    static inline std::array<float, COUNT> thresholdsFor(
        bool tilted,
        float toneSlopeDbPerOctave,
        TargetProfile profile = TargetProfile::PINK_NOISE,
        float baseThresholdDb = MBC_THRESHOLD_DB
    ) {
        std::array<float, COUNT> out{};
        if (!tilted) {
            out.fill(baseThresholdDb);
            return out;
        }

        const float* contour = (profile == TargetProfile::MODERN_MIX) ? MODERN_CONTOUR_DB.data() : nullptr;
        float mean = 0.0f;
        for (size_t b = 0; b < COUNT; ++b) {
            float density = toneSlopeDbPerOctave * (OCTAVES[b] - MEAN_OCTAVE);
            float bandwidth = static_cast<float>(10.0 * std::log10(OCTAVE_SPANS[b]));
            float profileOffset = contour ? contour[b] : 0.0f;
            out[b] = density + bandwidth + profileOffset;
            mean += out[b];
        }
        mean /= static_cast<float>(COUNT);
        for (size_t b = 0; b < COUNT; ++b) {
            out[b] = baseThresholdDb + (out[b] - mean);
        }
        return out;
    }
};

} // namespace autolevel::dsp
