#pragma once

#include "Bands.h"
#include "Biquad.h"
#include <array>

namespace autolevel::dsp {

/**
 * 6-band Linkwitz-Riley (LR4) crossover tree, phase-compensated so the bands sum flat.
 *
 * Crossovers sit at Bands::CROSSOVERS = 120, 400, 1200, 3500, 8000 Hz. Both the
 * Multiband Compressor and the Band EQ split their signal through one of these, so the
 * "Sub / Bass / Low-Mid / High-Mid / Presence / Air" bands mean exactly the same thing in each.
 * Each user owns its own instance - the filters are stateful.
 */
class BandSplitter {
public:
    BandSplitter() = default;

    void prepare(double sampleRate) {
        // Setup crossovers at: 120, 400, 1200, 3500, 8000 Hz
        for (size_t ch = 0; ch < 2; ++ch) {
            for (size_t i = 0; i < 5; ++i) {
                m_lp[ch][i].setup(Biquad::Type::LowPass, Bands::CROSSOVERS[i], sampleRate);
                m_hp[ch][i].setup(Biquad::Type::HighPass, Bands::CROSSOVERS[i], sampleRate);
            }
        }

        // Phase-compensation allpasses (see AP_COMPENSATION below)
        for (size_t ch = 0; ch < 2; ++ch) {
            for (size_t b = 0; b < Bands::COUNT; ++b) {
                for (size_t k = 0; k < AP_MAX; ++k) {
                    int xover = AP_COMPENSATION[b][k];
                    if (xover >= 0) {
                        m_ap[ch][b][k].setup(Bands::CROSSOVERS[static_cast<size_t>(xover)], sampleRate);
                    }
                }
            }
        }
    }

    void reset() {
        for (size_t ch = 0; ch < 2; ++ch) {
            for (size_t i = 0; i < 5; ++i) {
                m_lp[ch][i].reset();
                m_hp[ch][i].reset();
            }
            for (size_t b = 0; b < Bands::COUNT; ++b) {
                for (size_t k = 0; k < AP_MAX; ++k) m_ap[ch][b][k].reset();
            }
        }
    }

    /** Split one sample of channel `ch` (0 = left, 1 = right) into the six bands. */
    inline void split(size_t ch, float in, std::array<float, Bands::COUNT>& outBands) noexcept {
        // Crossover 2 (1200 Hz): Split into Low (< 1200 Hz) and High (> 1200 Hz)
        double low1200 = m_lp[ch][2].process(in);
        double high1200 = m_hp[ch][2].process(in);

        // Low branch: Split at Crossover 1 (400 Hz)
        double low400 = m_lp[ch][1].process(low1200);
        double high400 = m_hp[ch][1].process(low1200);

        // Sub branch: Split low400 at Crossover 0 (120 Hz)
        outBands[0] = static_cast<float>(m_lp[ch][0].process(low400)); // Sub (< 120)
        outBands[1] = static_cast<float>(m_hp[ch][0].process(low400)); // Bass (120 - 400)
        outBands[2] = static_cast<float>(high400);                     // Low-Mid (400 - 1200)

        // High branch: Split at Crossover 3 (3500 Hz)
        double low3500 = m_lp[ch][3].process(high1200);
        double high3500 = m_hp[ch][3].process(high1200);

        outBands[3] = static_cast<float>(low3500);                     // High-Mid (1200 - 3500)

        // HighHigh branch: Split at Crossover 4 (8000 Hz)
        outBands[4] = static_cast<float>(m_lp[ch][4].process(high3500)); // Presence (3500 - 8000)
        outBands[5] = static_cast<float>(m_hp[ch][4].process(high3500)); // Air (> 8000)

        // Phase-align the bands so they reconstruct flat when summed. Each band
        // is passed through the allpasses of the splits its own path skipped;
        // this leaves every band's magnitude response untouched (so per-band
        // thresholds keep their calibration) and only corrects the summation.
        for (size_t b = 0; b < Bands::COUNT; ++b) {
            double v = static_cast<double>(outBands[b]);
            for (size_t k = 0; k < AP_MAX; ++k) {
                if (AP_COMPENSATION[b][k] < 0) break;
                v = m_ap[ch][b][k].process(v);
            }
            outBands[b] = static_cast<float>(v);
        }
    }

private:
    /**
     * Which crossovers each band must be allpass-corrected by, as indices into
     * Bands::CROSSOVERS = {120, 400, 1200, 3500, 8000}; -1 terminates the list.
     *
     * The tree splits at 1200 first, then 400 and 120 down the low branch and
     * 3500 and 8000 down the high branch. Writing L for the 1200 low branch and
     * H for the high one, and using LP+HP = AP at each split:
     *
     *   bands 0+1 already sum to LP1200*LP400*AP120, so band 2 (LP1200*HP400)
     *   needs AP120 to let the 400 split close:  L = LP1200*AP120*AP400
     *   bands 4+5 already sum to HP1200*HP3500*AP8000, so band 3 needs AP8000:
     *                                            H = HP1200*AP8000*AP3500
     *   L and H now carry different allpasses, so the 1200 split cannot close.
     *   Give L the high branch's pair and H the low branch's pair, and the whole
     *   sum collapses to AP120*AP400*AP3500*AP8000*AP1200 - flat magnitude.
     */
    static constexpr size_t AP_MAX = 3;
    static constexpr int AP_COMPENSATION[Bands::COUNT][AP_MAX] = {
        { 3,  4, -1 },   // Sub       : cross-branch AP3500, AP8000
        { 3,  4, -1 },   // Bass      : cross-branch AP3500, AP8000
        { 0,  3,  4 },   // Low-Mid   : intra AP120 + cross-branch AP3500, AP8000
        { 4,  0,  1 },   // High-Mid  : intra AP8000 + cross-branch AP120, AP400
        { 0,  1, -1 },   // Presence  : cross-branch AP120, AP400
        { 0,  1, -1 }    // Air       : cross-branch AP120, AP400
    };

    std::array<std::array<LR4Filter, 5>, 2> m_lp;
    std::array<std::array<LR4Filter, 5>, 2> m_hp;
    std::array<std::array<std::array<AllpassLR4, AP_MAX>, Bands::COUNT>, 2> m_ap;
};

} // namespace autolevel::dsp
