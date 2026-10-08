#pragma once

#include <cmath>
#include <cstdint>
#include <algorithm>
#include <array>
#include <vector>

namespace autolevel::dsp {

/** How far ahead the Safety Limiter looks. OFF is the original zero-latency limiter. */
enum class LimiterLookahead {
    OFF = 0,
    MS_1 = 1,
    MS_2 = 2
};

/**
 * Safety Limiter with two engines, chosen by setLookahead():
 *
 * OFF - the original limiter matching Android DynamicsProcessing.Limiter, zero latency:
 *   - Attack time = 1.0 ms, Release time = 60.0 ms, Ratio = 20:1
 *   - Threshold = the ceiling, plus a hard clamp at the ceiling as the last resort.
 *   When pushed, the 1 ms attack lets the front of each peak through and the clamp squares it
 *   off (measured 2-5% THD on a sine 3-12 dB over the ceiling).
 *
 * MS_1 / MS_2 - lookahead brickwall, latency 1 or 2 ms (the only latency it adds):
 *   1. Detector: stereo-linked sample peak plus a 4x-oversampled true-peak estimate
 *      (polyphase windowed-sinc, detector path only - the audio is never resampled).
 *   2. Required gain ceiling/peak, held at its minimum over the smoothing window plus a 20 ms
 *      hold, so successive half-cycles of a bass note (down to 25 Hz) do not let the gain bounce
 *      between them - that bounce is what distorts bass in a plain fast limiter.
 *   3. Instant-down / 60 ms one-pole release.
 *   4. Two cascaded moving averages spanning the window, so the gain glides down in an S-curve
 *      and reaches full reduction exactly when the peak leaves the delay line.
 *   5. Audio delayed by the lookahead and multiplied by that gain.
 *   Steps 2 and 4 guarantee the applied gain is at or below what every sample needs, so the
 *   output never exceeds the ceiling without anything being clipped. The hard clamp is kept as
 *   a last resort and counted (getClampCount()); on normal material it stays at zero.
 *
 * Switching mode while running fades out over 2 ms, swaps, and fades back in once audio
 * emerges from the new delay line, so the change never clicks.
 */
class SafetyLimiter {
public:
    SafetyLimiter() = default;

    /** Latency, in samples, of a lookahead mode at a sample rate (0 for OFF). */
    static int lookaheadSamples(LimiterLookahead mode, double sampleRate) noexcept {
        if (mode == LimiterLookahead::OFF) return 0;
        double ms = (mode == LimiterLookahead::MS_1) ? 1.0 : 2.0;
        return std::max(TP_DELAY + 2, static_cast<int>(std::lround(sampleRate * ms * 0.001)));
    }

    void prepare(double sampleRate) {
        m_sampleRate = sampleRate;
        // Exact 1.0 ms attack
        m_attackCoeff = std::exp(-1.0 / (sampleRate * 0.001));
        // Exact 60.0 ms release
        m_releaseCoeff = std::exp(-1.0 / (sampleRate * 0.060));

        // Lookahead engine: size everything for the longest mode so process() never allocates.
        const int maxLookahead = lookaheadSamples(LimiterLookahead::MS_2, sampleRate);
        m_holdSamples = static_cast<int>(std::lround(sampleRate * HOLD_SECONDS));
        m_delayL.assign(static_cast<size_t>(maxLookahead + 1), 0.0f);
        m_delayR.assign(static_cast<size_t>(maxLookahead + 1), 0.0f);
        m_minQueue.assign(static_cast<size_t>(maxLookahead + m_holdSamples + 2), MinEntry{});
        m_box1.assign(static_cast<size_t>(maxLookahead + 1), 1.0);
        m_box2.assign(static_cast<size_t>(maxLookahead + 1), 1.0);
        m_laReleaseCoeff = std::exp(-1.0 / (sampleRate * LA_RELEASE_SECONDS));
        m_fadeLen = std::max(1, static_cast<int>(std::lround(sampleRate * SWITCH_FADE_SECONDS)));
        designTruePeakFilter();

        // A (re)prepare is a fresh start: take the requested mode directly, no fade.
        m_activeMode = m_requestedMode;
        m_fadeState = FadeState::NONE;
        m_hasProcessed = false;
        reset();
        resetLookahead();
    }

    /**
     * Clears the OFF engine's envelope and the meter, as it always has. Deliberately leaves the
     * lookahead delay line and gain alone: emptying them mid-song would drop a millisecond or
     * two of audio, an audible click for no benefit.
     */
    void reset() {
        m_envelope = 0.0;
        m_gainReductionDb = 0.0f;
    }

    void setCeilingDb(float ceilingDb) {
        m_ceilingDb = ceilingDb;
        m_ceilingLin = std::pow(10.0f, ceilingDb / 20.0f);
    }

    /** Takes effect at once if nothing is playing yet, otherwise through a 2 ms fade. */
    void setLookahead(LimiterLookahead mode) noexcept { m_requestedMode = mode; }

    LimiterLookahead getActiveLookahead() const noexcept { return m_activeMode; }

    void process(float* left, float* right, size_t numSamples) {
        float blockMinGrDb = 0.0f;

        // Before the first block after prepare() nothing has been heard yet, so a mode change
        // (the host's saved setting arriving) applies immediately instead of fading.
        if (!m_hasProcessed && m_requestedMode != m_activeMode) {
            m_activeMode = m_requestedMode;
            resetLookahead();
        }
        if (numSamples > 0) m_hasProcessed = true;

        for (size_t i = 0; i < numSamples; ++i) {
            if (m_fadeState == FadeState::NONE && m_requestedMode != m_activeMode) {
                m_fadeState = FadeState::OUT;
                m_fadePos = 0;
            }

            float outL = left[i];
            float outR = right[i];
            float sampleGrDb = (m_activeMode == LimiterLookahead::OFF)
                ? processSampleOff(outL, outR)
                : processSampleLookahead(outL, outR);

            if (m_fadeState != FadeState::NONE) {
                float fade = advanceFade();
                outL *= fade;
                outR *= fade;
            }

            left[i] = outL;
            right[i] = outR;
            if (sampleGrDb < blockMinGrDb) {
                blockMinGrDb = sampleGrDb;
            }
        }

        // Instant attack to catch all micro-transients; smooth ~16 dB/sec release for fluid visual metering
        if (blockMinGrDb < m_gainReductionDb) {
            m_gainReductionDb = blockMinGrDb;
        } else {
            float dtSeconds = static_cast<float>(numSamples) / static_cast<float>(m_sampleRate);
            m_gainReductionDb = std::min(0.0f, m_gainReductionDb + dtSeconds * 16.0f);
        }
    }

    /**
     * Bypass path: delays the audio by the active lookahead and nothing else, so a bypassed
     * plugin keeps the latency it reports to the host. A no-op in OFF.
     */
    void processDelayOnly(float* left, float* right, size_t numSamples) {
        if (m_requestedMode != m_activeMode) {
            // Nothing audible to protect while bypassed - switch straight away.
            m_activeMode = m_requestedMode;
            m_fadeState = FadeState::NONE;
            resetLookahead();
        }
        if (m_activeMode == LimiterLookahead::OFF) return;

        // The gain pipeline is not fed while bypassed; restart it at unity for when we come back.
        resetGainPipeline();
        for (size_t i = 0; i < numSamples; ++i) {
            float dL, dR;
            pushDelay(left[i], right[i], dL, dR);
            left[i] = dL;
            right[i] = dR;
        }
        m_gainReductionDb = 0.0f;
    }

    float getGainReductionDb() const noexcept {
        return m_gainReductionDb;
    }

    /** How many samples the lookahead engine's last-resort clamp has had to touch since reset. */
    uint64_t getClampCount() const noexcept { return m_clampCount; }

private:
    // ---- OFF: the original limiter, unchanged sample for sample -------------------------------
    float processSampleOff(float& outL, float& outR) noexcept {
        constexpr double ratio = 20.0; // 20:1 limiter ratio from Android
        const double thresholdLin = m_ceilingLin;
        const double thresholdDb = m_ceilingDb;

        float inL = outL;
        float inR = outR;

        double peak = std::max(std::abs(inL), std::abs(inR));
        if (peak > m_envelope) {
            m_envelope = peak + m_attackCoeff * (m_envelope - peak);
        } else {
            m_envelope = peak + m_releaseCoeff * (m_envelope - peak);
        }

        double grDb = 0.0;
        if (m_envelope > thresholdLin) {
            double envDb = 20.0 * std::log10(m_envelope);
            double overDb = envDb - thresholdDb;
            // 20:1 compression ratio
            grDb = -overDb * (1.0 - 1.0 / ratio);
        }

        float gainLin = (grDb < 0.0) ? static_cast<float>(std::pow(10.0, grDb / 20.0)) : 1.0f;

        outL = inL * gainLin;
        outR = inR * gainLin;

        // Strict ceiling clamp for absolute DAC/amp clip safety
        float preClampMax = std::max(std::abs(outL), std::abs(outR));
        outL = std::clamp(outL, -m_ceilingLin, m_ceilingLin);
        outR = std::clamp(outR, -m_ceilingLin, m_ceilingLin);
        float postClampMax = std::max(std::abs(outL), std::abs(outR));

        // If ceiling clamp engaged, account for it in total gain reduction
        if (preClampMax > m_ceilingLin && postClampMax > 0.0f) {
            double clampGrDb = 20.0 * std::log10(static_cast<double>(postClampMax) / static_cast<double>(preClampMax));
            if (clampGrDb < grDb) {
                grDb = clampGrDb;
            }
        }

        return static_cast<float>(grDb);
    }

    // ---- MS_1 / MS_2: lookahead brickwall -----------------------------------------------------
    float processSampleLookahead(float& outL, float& outR) noexcept {
        const float inL = outL;
        const float inR = outR;

        // 1. Detector, referenced to the sample TP_DELAY ago: its own peak, and the true peak
        //    estimated between it and the next sample.
        m_tpPos = (m_tpPos + 1) % TP_TAPS;
        m_tpHistL[static_cast<size_t>(m_tpPos)] = inL;
        m_tpHistR[static_cast<size_t>(m_tpPos)] = inR;
        auto hist = [this](const std::array<float, TP_TAPS>& h, int k) {
            return h[static_cast<size_t>((m_tpPos - k + TP_TAPS) % TP_TAPS)];
        };
        double peak = std::max(std::abs(hist(m_tpHistL, TP_DELAY)), std::abs(hist(m_tpHistR, TP_DELAY)));
        for (int p = 0; p < TP_PHASES; ++p) {
            double yL = 0.0, yR = 0.0;
            for (int k = 0; k < TP_TAPS; ++k) {
                const double c = m_tpCoeffs[static_cast<size_t>(p)][static_cast<size_t>(k)];
                yL += c * hist(m_tpHistL, k);
                yR += c * hist(m_tpHistR, k);
            }
            peak = std::max(peak, std::max(std::abs(yL), std::abs(yR)));
        }
        const double ceiling = m_ceilingLin;
        const double required = (peak > ceiling) ? ceiling / peak : 1.0;

        // 2. Minimum over smoothing window + hold (monotonic queue, O(1) amortised).
        const double held = slidingMin(required);

        // 3. Instant down, slow up.
        m_laRelease = (held < m_laRelease) ? held : held + m_laReleaseCoeff * (m_laRelease - held);

        // 4. Two moving averages, total span = the smoothing window.
        const double s1 = boxcar(m_box1, m_box1Pos, m_box1Sum, m_box1Len, m_laRelease);
        const double gain = boxcar(m_box2, m_box2Pos, m_box2Sum, m_box2Len, s1);

        // 5. Delayed audio x gain.
        float dL, dR;
        pushDelay(inL, inR, dL, dR);
        outL = static_cast<float>(dL * gain);
        outR = static_cast<float>(dR * gain);

        double grDb = (gain < 1.0) ? 20.0 * std::log10(gain) : 0.0;

        // Last resort. With the window covering the full lookahead this should never fire; it
        // can only be reached by rounding in the running sums.
        const float preClampMax = std::max(std::abs(outL), std::abs(outR));
        if (preClampMax > m_ceilingLin) {
            ++m_clampCount;
            outL = std::clamp(outL, -m_ceilingLin, m_ceilingLin);
            outR = std::clamp(outR, -m_ceilingLin, m_ceilingLin);
            grDb = std::min(grDb, 20.0 * std::log10(static_cast<double>(m_ceilingLin) / preClampMax) + grDb);
        }
        return static_cast<float>(grDb);
    }

    double slidingMin(double v) noexcept {
        const int64_t now = m_sampleIndex++;
        const size_t cap = m_minQueue.size();
        // Drop entries the new value makes irrelevant.
        while (m_minCount > 0) {
            size_t back = (m_minHead + m_minCount - 1) % cap;
            if (m_minQueue[back].value >= v) --m_minCount;
            else break;
        }
        m_minQueue[(m_minHead + m_minCount) % cap] = MinEntry{v, now};
        ++m_minCount;
        // Drop the front once it falls out of the window.
        while (m_minQueue[m_minHead].index <= now - m_minWindow) {
            m_minHead = (m_minHead + 1) % cap;
            --m_minCount;
        }
        return m_minQueue[m_minHead].value;
    }

    static double boxcar(std::vector<double>& buf, int& pos, double& sum, int len, double x) noexcept {
        sum += x - buf[static_cast<size_t>(pos)];
        buf[static_cast<size_t>(pos)] = x;
        pos = (pos + 1) % len;
        return sum / static_cast<double>(len);
    }

    void pushDelay(float inL, float inR, float& outL, float& outR) noexcept {
        const int size = m_lookahead + 1;
        m_delayL[static_cast<size_t>(m_delayPos)] = inL;
        m_delayR[static_cast<size_t>(m_delayPos)] = inR;
        const int readPos = (m_delayPos + 1) % size;   // oldest = written m_lookahead samples ago
        outL = m_delayL[static_cast<size_t>(readPos)];
        outR = m_delayR[static_cast<size_t>(readPos)];
        m_delayPos = readPos;
    }

    /** Fade gain for this sample, advancing the switch-over state machine. */
    float advanceFade() noexcept {
        if (m_fadeState == FadeState::OUT) {
            float g = 1.0f - static_cast<float>(m_fadePos) / static_cast<float>(m_fadeLen);
            if (++m_fadePos >= m_fadeLen) {
                m_activeMode = m_requestedMode;
                resetLookahead();
                m_envelope = 0.0;
                m_fadeState = FadeState::IN;
                m_fadePos = 0;
            }
            return g;
        }
        // IN: the new delay line outputs silence for m_lookahead samples; ramp only after that,
        // so the audio does not jump in at full level when it emerges.
        int p = m_fadePos++ - m_lookahead;
        if (p >= m_fadeLen) {
            m_fadeState = FadeState::NONE;
            return 1.0f;
        }
        return (p <= 0) ? 0.0f : static_cast<float>(p) / static_cast<float>(m_fadeLen);
    }

    void resetLookahead() {
        if (m_delayL.empty()) return;   // not prepared yet
        m_lookahead = lookaheadSamples(m_activeMode, m_sampleRate);
        // Smoothing window: the lookahead minus the true-peak detector's own delay, so
        // detector delay + window - 1 = latency, and the gain is fully down on time.
        // (OFF never runs this engine; give it a harmless 1-sample window.)
        const int window = (m_lookahead > 0) ? m_lookahead - TP_DELAY + 1 : 1;
        m_box1Len = (window + 1) / 2;
        m_box2Len = window + 1 - m_box1Len;      // total span m_box1Len + m_box2Len - 1 = window
        m_minWindow = window + m_holdSamples;
        std::fill(m_delayL.begin(), m_delayL.end(), 0.0f);
        std::fill(m_delayR.begin(), m_delayR.end(), 0.0f);
        m_delayPos = 0;
        m_tpHistL.fill(0.0f);
        m_tpHistR.fill(0.0f);
        m_tpPos = 0;
        m_clampCount = 0;
        resetGainPipeline();
    }

    void resetGainPipeline() {
        m_minHead = 0;
        m_minCount = 0;
        m_sampleIndex = 0;
        m_laRelease = 1.0;
        std::fill(m_box1.begin(), m_box1.end(), 1.0);
        std::fill(m_box2.begin(), m_box2.end(), 1.0);
        m_box1Pos = 0;
        m_box2Pos = 0;
        m_box1Sum = static_cast<double>(m_box1Len);
        m_box2Sum = static_cast<double>(m_box2Len);
    }

    /** 4x interpolator split into TP_PHASES phases of TP_TAPS taps; Blackman-windowed sinc. */
    void designTruePeakFilter() {
        constexpr int N = TP_PHASES * TP_TAPS;
        constexpr double PI_D = 3.14159265358979323846;
        const double centre = (N - 1) / 2.0;
        for (int p = 0; p < TP_PHASES; ++p) {
            double sum = 0.0;
            for (int k = 0; k < TP_TAPS; ++k) {
                int i = TP_PHASES * k + p;
                double x = (i - centre) / TP_PHASES;
                double sinc = (std::abs(x) < 1e-12) ? 1.0 : std::sin(PI_D * x) / (PI_D * x);
                double w = 0.42 - 0.5 * std::cos(2.0 * PI_D * i / (N - 1)) + 0.08 * std::cos(4.0 * PI_D * i / (N - 1));
                m_tpCoeffs[static_cast<size_t>(p)][static_cast<size_t>(k)] = sinc * w;
                sum += sinc * w;
            }
            // Each phase passes DC at exactly unity.
            for (int k = 0; k < TP_TAPS; ++k) m_tpCoeffs[static_cast<size_t>(p)][static_cast<size_t>(k)] /= sum;
        }
    }

    // Phase p of the interpolator estimates the signal at (n - 5.875 + p/4): between the samples
    // TP_DELAY = 6 and 5 ago.
    static constexpr int TP_PHASES = 4;
    static constexpr int TP_TAPS = 12;
    static constexpr int TP_DELAY = 6;
    static constexpr double HOLD_SECONDS = 0.020;
    static constexpr double LA_RELEASE_SECONDS = 0.060;
    static constexpr double SWITCH_FADE_SECONDS = 0.002;

    struct MinEntry { double value = 1.0; int64_t index = 0; };
    enum class FadeState { NONE, OUT, IN };

    double m_sampleRate = 48000.0;
    double m_attackCoeff = 0.0;
    double m_releaseCoeff = 0.0;
    double m_envelope = 0.0;
    float m_ceilingDb = -0.3f; // -0.3 dBFS default master ceiling
    float m_ceilingLin = 0.96605088f;
    float m_gainReductionDb = 0.0f;

    LimiterLookahead m_requestedMode = LimiterLookahead::OFF;
    LimiterLookahead m_activeMode = LimiterLookahead::OFF;
    FadeState m_fadeState = FadeState::NONE;
    int m_fadePos = 0;
    int m_fadeLen = 96;
    bool m_hasProcessed = false;

    int m_lookahead = 48;
    int m_holdSamples = 480;
    int m_minWindow = 0;
    std::vector<float> m_delayL, m_delayR;
    int m_delayPos = 0;
    std::array<float, TP_TAPS> m_tpHistL{}, m_tpHistR{};
    int m_tpPos = 0;
    std::array<std::array<double, TP_TAPS>, TP_PHASES> m_tpCoeffs{};
    std::vector<MinEntry> m_minQueue;
    size_t m_minHead = 0;
    size_t m_minCount = 0;
    int64_t m_sampleIndex = 0;
    double m_laRelease = 1.0;
    double m_laReleaseCoeff = 0.0;
    std::vector<double> m_box1, m_box2;
    int m_box1Pos = 0, m_box2Pos = 0;
    int m_box1Len = 1, m_box2Len = 1;
    double m_box1Sum = 1.0, m_box2Sum = 1.0;
    uint64_t m_clampCount = 0;
};

} // namespace autolevel::dsp
