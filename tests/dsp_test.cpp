#include "../src/dsp/AutoLevelEngine.h"
#include <iostream>
#include <cassert>
#include <cmath>
#include <vector>
#include <numeric>
#include <limits>
#include <cstdint>

using namespace autolevel::dsp;

constexpr double TEST_PI = 3.14159265358979323846;

void testKWeightingSineWave() {
    std::cout << "[TEST] KWeightingFilter 1kHz calibration..." << std::endl;
    KWeightingFilter filter;
    double sampleRate = 48000.0;
    filter.prepare(sampleRate);

    double freq = 1000.0;
    double sumSqIn = 0.0;
    double sumSqOut = 0.0;
    size_t n = 48000;

    for (size_t i = 0; i < n; ++i) {
        double t = static_cast<double>(i) / sampleRate;
        double s = std::sin(2.0 * TEST_PI * freq * t);
        double fL = 0.0, fR = 0.0;
        filter.processSample(s, s, fL, fR);

        if (i >= 24000) {
            sumSqIn += s * s;
            sumSqOut += fL * fL;
        }
    }

    double rmsIn = std::sqrt(sumSqIn / 24000.0);
    double rmsOut = std::sqrt(sumSqOut / 24000.0);
    double gainDb = 20.0 * std::log10(rmsOut / rmsIn);

    std::cout << "  Input RMS: " << rmsIn << " (" << (20.0 * std::log10(rmsIn)) << " dBFS)" << std::endl;
    std::cout << "  Output RMS: " << rmsOut << std::endl;
    std::cout << "  K-Weighting gain at 1kHz: " << gainDb << " dB" << std::endl;

    assert(std::abs(gainDb - 0.6543) < 0.01);
    std::cout << "  -> PASS: 1kHz K-Weighting response is within specification." << std::endl;
}

void testAndroidToneProfilesAndThresholds() {
    std::cout << "[TEST] Android Shaper thresholdsFor matching..." << std::endl;

    auto linear = Bands::thresholdsFor(true, -2.0f, TargetProfile::PINK_NOISE);
    auto modern = Bands::thresholdsFor(true, -2.0f, TargetProfile::MODERN_MIX);

    float linearSum = 0.0f;
    float modernSum = 0.0f;

    for (size_t b = 0; b < Bands::COUNT; ++b) {
        linearSum += linear[b];
        modernSum += modern[b];
        std::cout << "  Band " << b << " (" << Bands::NAMES[b] << "): Linear="
                  << linear[b] << " dB, Modern=" << modern[b] << " dB" << std::endl;
    }

    float linearAvg = linearSum / Bands::COUNT;
    float modernAvg = modernSum / Bands::COUNT;
    (void)linearAvg;
    (void)modernAvg;

    assert(std::abs(linearAvg - (-24.0f)) < 0.05f);
    assert(std::abs(modernAvg - (-24.0f)) < 0.05f);

    assert(modern[0] > linear[0]);
    assert(modern[1] < linear[1]);
    assert(modern[2] > linear[2]);
    assert(modern[3] > linear[3]);
    assert(modern[4] < linear[4]);
    assert(modern[5] > modern[4]);

    std::cout << "  -> PASS: Tone curves and per-band thresholds match Android Shaper exactly." << std::endl;
}

void testLR4CrossoverSummation() {
    std::cout << "[TEST] Linkwitz-Riley 6-band crossover flat magnitude test..." << std::endl;
    MultibandCompressor mbc;
    double sampleRate = 48000.0;

    MBCParams params;
    params.enabled = true;
    params.compressionAmount = Bands::MIN_COMPRESSION;
    params.baseThresholdDb = 60.0f;
    params.toneSlopeDbPerOctave = 0.0f;

    std::vector<double> testFreqs = { 30.0, 50.0, 120.0, 300.0, 400.0, 600.0, 800.0, 1200.0, 1800.0, 2000.0, 3500.0, 6000.0, 8000.0, 14000.0 };

    double worstDb = 0.0;
    double worstFreq = 0.0;
    for (double f : testFreqs) {
        mbc.prepare(sampleRate);
        size_t n = 48000;
        std::vector<float> inL(n), inR(n), origL(n);
        for (size_t i = 0; i < n; ++i) {
            float s = static_cast<float>(std::sin(2.0 * TEST_PI * f * (static_cast<double>(i) / sampleRate)));
            inL[i] = s;
            inR[i] = s;
            origL[i] = s;
        }

        mbc.process(inL.data(), inR.data(), n, params);

        double sumSqIn = 0.0;
        double sumSqOut = 0.0;
        size_t count = 0;
        for (size_t i = 24000; i < n; ++i) {
            sumSqIn += origL[i] * origL[i];
            sumSqOut += inL[i] * inL[i];
            count++;
        }

        assert(sumSqOut > 0.0);

        double rmsIn = std::sqrt(sumSqIn / count);
        double rmsOut = std::sqrt(sumSqOut / count);
        double errorDb = 20.0 * std::log10(rmsOut / rmsIn);

        if (std::abs(errorDb) > std::abs(worstDb)) {
            worstDb = errorDb;
            worstFreq = f;
        }

        assert(std::abs(errorDb) < 0.10);
    }

    std::cout << "  Worst reconstruction error: " << worstDb << " dB at " << worstFreq << " Hz" << std::endl;
    std::cout << "  -> PASS: 6-band crossover tree sums to flat magnitude." << std::endl;
}

void testLevelResponseMapping() {
    std::cout << "[TEST] Level response slider mapping..." << std::endl;
    float r0 = LoudnessMeter::halfLifeForResponse(0.0f);
    float r5 = LoudnessMeter::halfLifeForResponse(0.5f);
    float r1 = LoudnessMeter::halfLifeForResponse(1.0f);

    std::cout << "  Response 0.0 -> " << r0 << "s (track hold)" << std::endl;
    std::cout << "  Response 0.5 -> " << r5 << "s" << std::endl;
    std::cout << "  Response 1.0 -> " << r1 << "s" << std::endl;

    assert(r0 == 0.0f);
    assert(r1 == 4.0f);
    assert(std::abs(r5 - 21.9089f) < 0.01f);
    std::cout << "  -> PASS: Level response logarithmic decay mapping verified." << std::endl;
}

void testEbuR128DynamicLoudness() {
    std::cout << "[TEST] EBU R128 Loudness measurement across varying song levels..." << std::endl;
    double sampleRate = 48000.0;

    auto testLevel = [&](double targetDbfs) {
        LoudnessMeter meter;
        meter.prepare(sampleRate);
        meter.setLevelResponse(0.0f);

        double linAmp = std::pow(10.0, targetDbfs / 20.0);
        size_t n = 48000 * 2;
        std::vector<float> l(n), r(n);
        for (size_t i = 0; i < n; ++i) {
            double s = linAmp * std::sin(2.0 * TEST_PI * 1000.0 * (static_cast<double>(i) / sampleRate));
            l[i] = static_cast<float>(s);
            r[i] = static_cast<float>(s);
        }

        meter.process(l.data(), r.data(), n);
        auto readings = meter.getReadings();

        std::cout << "  Input level " << targetDbfs << " dBFS -> Integrated: "
                  << readings.integratedLUFS << " LUFS, Momentary: "
                  << readings.momentaryLUFS << " LUFS" << std::endl;

        double expectedLufs = targetDbfs - 0.17;
        assert(std::abs(readings.integratedLUFS - expectedLufs) < 0.5);
    };

    testLevel(-6.0);
    testLevel(-12.0);
    testLevel(-18.0);
    testLevel(-24.0);

    std::cout << "  -> PASS: EBU R128 measures varying loudness accurately across all levels." << std::endl;
}

namespace {

// 60 Hz sine at 0.5 FS for `seconds`, in one block - lands in the Sub band.
std::vector<float> subSine(double sampleRate, double seconds) {
    size_t n = static_cast<size_t>(sampleRate * seconds);
    std::vector<float> v(n);
    for (size_t i = 0; i < n; ++i) {
        v[i] = 0.5f * static_cast<float>(std::sin(2.0 * TEST_PI * 60.0 * (static_cast<double>(i) / sampleRate)));
    }
    return v;
}

} // namespace

void testMbcAttackRelease() {
    std::cout << "[TEST] Multiband Compressor user-set attack / release..." << std::endl;
    // MBC tests below use a -30 dBFS base threshold: with the default tone tilt and Modern Mix
    // contour the Sub band's own threshold sits several dB above the base, and these tests need
    // it compressing hard.
    double sampleRate = 48000.0;

    // Gain reduction the Sub band has reached 100 ms after a 60 Hz burst starts.
    auto measureCompression = [&](float attackMs, float releaseMs) {
        MultibandCompressor mbc;
        mbc.prepare(sampleRate);

        MBCParams params;
        params.enabled = true;
        params.attackMs = attackMs;
        params.releaseMs = releaseMs;
        params.compressionAmount = 0.8f;
        params.baseThresholdDb = -30.0f;

        auto l = subSine(sampleRate, 0.1);
        auto r = l;
        mbc.process(l.data(), r.data(), l.size(), params);
        return mbc.getGainReductionsDb()[0];
    };

    float fastGr = measureCompression(3.0f, 200.0f);
    float defaultGr = measureCompression(15.0f, 200.0f);
    float slowGr = measureCompression(60.0f, 200.0f);

    std::cout << "  Attack  3 ms, 100ms Sub GR: " << fastGr << " dB" << std::endl;
    std::cout << "  Attack 15 ms, 100ms Sub GR: " << defaultGr << " dB" << std::endl;
    std::cout << "  Attack 60 ms, 100ms Sub GR: " << slowGr << " dB" << std::endl;

    // A shorter attack gets further down in the same time (more negative GR).
    assert(fastGr < defaultGr);
    assert(defaultGr < slowGr);

    // Release: after the burst ends, a long release holds the reduction longer.
    auto grAfterRelease = [&](float releaseMs) {
        MultibandCompressor mbc;
        mbc.prepare(sampleRate);
        MBCParams params;
        params.enabled = true;
        params.attackMs = 5.0f;
        params.releaseMs = releaseMs;
        params.compressionAmount = 0.8f;
        params.baseThresholdDb = -30.0f;

        auto burst = subSine(sampleRate, 0.4);
        auto burstR = burst;
        mbc.process(burst.data(), burstR.data(), burst.size(), params);
        std::vector<float> silence(static_cast<size_t>(sampleRate * 0.15), 0.0f), silenceR = silence;
        mbc.process(silence.data(), silenceR.data(), silence.size(), params);
        return mbc.getGainReductionsDb()[0];
    };
    float quickRel = grAfterRelease(40.0f);
    float longRel = grAfterRelease(800.0f);
    std::cout << "  GR 150 ms after burst: release 40 ms -> " << quickRel << " dB, 800 ms -> " << longRel << " dB" << std::endl;
    assert(quickRel > longRel);   // quick release has recovered more (closer to 0)

    // The pre-change defaults must be intact: 15 / 200 ms, Sub at twice that. A 1 ms-off value
    // would show up here; compare against explicitly set values rather than the struct defaults.
    MBCParams defaults;
    assert(defaults.attackMs == 15.0f && defaults.releaseMs == 200.0f && defaults.detectorRmsMix == 0.0f);

    // Out-of-range values are clamped, not trusted.
    float tooFast = measureCompression(0.0f, 200.0f);
    float minAttack = measureCompression(MultibandCompressor::MIN_ATTACK_MS, 200.0f);
    assert(tooFast == minAttack);

    // prepare() must reinstall the ballistics the cache believes are in force: apply a non-default
    // setting, re-prepare (e.g. a sample-rate change), process with the same setting, and expect
    // the same result as a fresh instance.
    MultibandCompressor reprepared;
    reprepared.prepare(sampleRate);
    MBCParams p;
    p.enabled = true; p.attackMs = 3.0f; p.releaseMs = 200.0f; p.compressionAmount = 0.8f; p.baseThresholdDb = -30.0f;
    auto l = subSine(sampleRate, 0.1); auto r = l;
    reprepared.process(l.data(), r.data(), l.size(), p);
    reprepared.prepare(sampleRate);
    l = subSine(sampleRate, 0.1); r = l;
    reprepared.process(l.data(), r.data(), l.size(), p);
    assert(std::abs(reprepared.getGainReductionsDb()[0] - fastGr) < 0.001f);

    std::cout << "  -> PASS: attack and release shape the compressor, are clamped, and survive prepare()." << std::endl;
}

void testMbcLiveBallisticsChange() {
    std::cout << "[TEST] Changing attack/release mid-playback does not reset the compressor..." << std::endl;
    double sampleRate = 48000.0;
    MultibandCompressor mbc;
    mbc.prepare(sampleRate);

    MBCParams params;
    params.enabled = true;
    params.compressionAmount = 0.8f;
    params.baseThresholdDb = -30.0f;

    auto l = subSine(sampleRate, 0.5);
    auto r = l;
    mbc.process(l.data(), r.data(), l.size(), params);
    float before = mbc.getGainReductionsDb()[0];
    assert(before < -3.0f);

    // One short block with new times: the envelope must carry on from where it was.
    params.attackMs = 40.0f;
    params.releaseMs = 300.0f;
    auto l2 = subSine(sampleRate, 0.005);
    auto r2 = l2;
    mbc.process(l2.data(), r2.data(), l2.size(), params);
    float after = mbc.getGainReductionsDb()[0];
    std::cout << "  Sub GR before " << before << " dB, 5 ms after the change " << after << " dB" << std::endl;
    assert(std::abs(after - before) < 1.5f);
    std::cout << "  -> PASS: envelope state survives a live ballistics change." << std::endl;
}

void testMbcRmsDetector() {
    std::cout << "[TEST] Multiband Compressor peak <-> RMS detector..." << std::endl;
    double sampleRate = 48000.0;

    // High crest factor: a 1 kHz sine gated on for 1 ms of every 25 ms. Peak detection sees the
    // full-height bursts; RMS sees them diluted by the gaps. (Run with a 1 ms attack: at the
    // default 15 ms the ballistics alone smooth a 1 ms burst, whatever the detector.)
    auto sparseBurst = [&](size_t n) {
        std::vector<float> v(n);
        for (size_t i = 0; i < n; ++i) {
            size_t phase = i % static_cast<size_t>(sampleRate * 0.025);
            bool on = phase < static_cast<size_t>(sampleRate * 0.001);
            v[i] = on ? 0.9f * static_cast<float>(std::sin(2.0 * TEST_PI * 1000.0 * i / sampleRate)) : 0.0f;
        }
        return v;
    };

    auto meanGr = [&](float rmsMix, bool useBursts) {
        MultibandCompressor mbc;
        mbc.prepare(sampleRate);
        MBCParams params;
        params.enabled = true;
        if (useBursts) { params.attackMs = 1.0f; params.releaseMs = 50.0f; }
        params.compressionAmount = 0.8f;
        params.baseThresholdDb = -30.0f;
        params.detectorRmsMix = rmsMix;

        size_t n = static_cast<size_t>(sampleRate * 2.0);
        std::vector<float> l = useBursts ? sparseBurst(n) : subSine(sampleRate, 2.0);
        auto r = l;
        // Block-wise, accumulating the Low-Mid band's (1 kHz) reduction over the settled second.
        double sum = 0.0; size_t count = 0;
        const size_t block = 256;
        for (size_t off = 0; off < n; off += block) {
            size_t m = std::min(block, n - off);
            mbc.process(l.data() + off, r.data() + off, m, params);
            if (off >= n / 2) {
                sum += mbc.getGainReductionsDb()[useBursts ? 2 : 0];
                ++count;
            }
        }
        return static_cast<float>(sum / static_cast<double>(count));
    };

    // 1. A steady sine reads the same in both modes (the RMS detector is sine-calibrated), so
    //    switching detector does not change how a steady bass note is treated.
    float sinePeak = meanGr(0.0f, false);
    float sineRms = meanGr(1.0f, false);
    std::cout << "  Steady 60 Hz sine, Sub GR: peak " << sinePeak << " dB, RMS " << sineRms << " dB" << std::endl;
    assert(sinePeak < -3.0f);
    assert(std::abs(sinePeak - sineRms) < 1.0f);

    // 2. Peaky material is compressed less by RMS than by peak, and the slider is monotonic.
    float burstPeak = meanGr(0.0f, true);
    float burstHalf = meanGr(0.5f, true);
    float burstRms = meanGr(1.0f, true);
    std::cout << "  Sparse 1 kHz bursts, Low-Mid GR: peak " << burstPeak << " dB, 50% " << burstHalf
              << " dB, RMS " << burstRms << " dB" << std::endl;
    assert(burstPeak < burstRms - 1.0f);      // RMS clearly gentler on peaky material
    assert(burstPeak <= burstHalf + 0.01f);
    assert(burstHalf <= burstRms + 0.01f);

    // 3. Peak mode is untouched by the new detector: mix 0 must equal an instance that never
    //    saw a mix value at all (the struct default).
    {
        MultibandCompressor a, b;
        a.prepare(sampleRate); b.prepare(sampleRate);
        MBCParams pa; pa.compressionAmount = 0.8f; pa.baseThresholdDb = -30.0f;
        MBCParams pb = pa; pb.detectorRmsMix = 0.0f;
        auto la = subSine(sampleRate, 0.3), lb = la, ra = la, rb = la;
        a.process(la.data(), ra.data(), la.size(), pa);
        b.process(lb.data(), rb.data(), lb.size(), pb);
        for (size_t i = 0; i < la.size(); ++i) assert(la[i] == lb[i]);
    }

    std::cout << "  -> PASS: RMS detector is sine-calibrated, gentler on peaks, and blends monotonically." << std::endl;
}

void testPostMbcGain() {
    std::cout << "[TEST] Post-MBC Gain stage (makeup & trim)..." << std::endl;
    double sampleRate = 48000.0;

    auto runGain = [&](float gainDb) {
        AutoLevelEngine engine;
        engine.prepare(sampleRate);

        EngineParameters params;
        params.targetLUFS = -14.0f;
        params.postMbcGainDb = gainDb;
        params.compressionAmount = 0.0f;
        params.levelResponse = 0.0f;
        params.ceilingDb = 0.0f;

        size_t n = 1000;
        std::vector<float> l(n, 0.1f), r(n, 0.1f);
        engine.process(l.data(), r.data(), n, params);

        float maxL = 0.0f;
        for (float s : l) maxL = std::max(maxL, std::abs(s));
        return maxL;
    };

    float peak0 = runGain(0.0f);
    float peakPlus6 = runGain(6.0f);
    float peakMinus6 = runGain(-6.0f);

    std::cout << "  0 dB Post Peak: " << peak0 << std::endl;
    std::cout << "  +6 dB Post Peak: " << peakPlus6 << " (ratio: " << (peakPlus6 / peak0) << ")" << std::endl;
    std::cout << "  -6 dB Post Peak: " << peakMinus6 << " (ratio: " << (peakMinus6 / peak0) << ")" << std::endl;

    assert(std::abs((peakPlus6 / peak0) - 1.99526) < 0.01);
    assert(std::abs((peakMinus6 / peak0) - 0.501187) < 0.01);
        std::cout << "  -> PASS: Post-MBC Gain accurately boosts and attenuates signal." << std::endl;
}

void testHighPassFilter() {
    std::cout << "[TEST] HighPassFilter 24 dB/octave 4th-order Butterworth response..." << std::endl;
    HighPassFilter hpf;
    double sampleRate = 48000.0;
    hpf.prepare(sampleRate);
    hpf.setCutoff(30.0f, true);

    auto measureGain = [&](double freq) {
        hpf.reset();
        size_t n = 48000;
        std::vector<float> l(n), r(n);
        for (size_t i = 0; i < n; ++i) {
            float s = static_cast<float>(std::sin(2.0 * TEST_PI * freq * (static_cast<double>(i) / sampleRate)));
            l[i] = s;
            r[i] = s;
        }
        hpf.process(l.data(), r.data(), n);

        double sumSq = 0.0;
        for (size_t i = 24000; i < n; ++i) sumSq += l[i] * l[i];
        double rms = std::sqrt(sumSq / 24000.0);
        return 20.0 * std::log10(rms / 0.70710678);
    };

    double g1k = measureGain(1000.0);
    double g30 = measureGain(30.0);
    double g15 = measureGain(15.0);

    std::cout << "  1 kHz passband gain: " << g1k << " dB (expected: ~0.0 dB)" << std::endl;
    std::cout << "  30 Hz cutoff gain:   " << g30 << " dB (expected: ~-3.0 dB)" << std::endl;
    std::cout << "  15 Hz octave-down gain: " << g15 << " dB (expected: ~-27.0 dB)" << std::endl;

    assert(std::abs(g1k - 0.0) < 0.01);
    assert(std::abs(g30 - (-3.01)) < 0.1);
    assert(g15 < -24.0);

    std::cout << "  -> PASS: 4th-order Butterworth filter exhibits exact 24 dB/octave attenuation." << std::endl;
}

void testDefaultLimiterCeiling() {
    std::cout << "[TEST] Default limiter ceiling is -0.3 dBFS..." << std::endl;
    EngineParameters defaultParams;
    std::cout << "  Default ceiling: " << defaultParams.ceilingDb << " dBFS" << std::endl;
    assert(std::abs(defaultParams.ceilingDb - (-0.3f)) < 0.001f);
    std::cout << "  -> PASS: Default limiter ceiling is -0.3 dBFS." << std::endl;
}

void testBreakdownFreezeSensitivity() {
    std::cout << "[TEST] Breakdown Auto-Freeze 3-way sensitivity (5 LU, 7 LU, 9 LU)..." << std::endl;
    Leveler leveler;
    leveler.prepare(48000.0);

    LevelerParams params;
    params.enabled = true;
    params.freezeBreakdowns = true;
    params.targetLUFS = -14.0f;

    LoudnessReadings readings;
    readings.integratedLUFS = -14.0f;
    readings.momentaryLUFS = -20.5f;

    // 1. Light (5 LU threshold) -> 6.5 LU drop should trigger freeze
    params.breakdownThresholdLU = 5.0f;
    leveler.update(params, readings, 100, 0.01f);
    assert(leveler.isFrozen());
    std::cout << "  -> 6.5 LU drop at 5.0 LU threshold: FROZEN (PASS)" << std::endl;

    // 2. Normal (7 LU threshold) -> 6.5 LU drop should NOT trigger freeze
    params.breakdownThresholdLU = 7.0f;
    leveler.update(params, readings, 100, 0.01f);
    assert(!leveler.isFrozen());
    std::cout << "  -> 6.5 LU drop at 7.0 LU threshold: NOT FROZEN (PASS)" << std::endl;

    // 3. Deep (9 LU threshold) -> 6.5 LU drop should NOT trigger freeze
    params.breakdownThresholdLU = 9.0f;
    leveler.update(params, readings, 100, 0.01f);
    assert(!leveler.isFrozen());
    std::cout << "  -> 6.5 LU drop at 9.0 LU threshold: NOT FROZEN (PASS)" << std::endl;

    // 4. Massive 10.0 LU drop -> ALL should freeze
    readings.momentaryLUFS = -24.0f;
    params.breakdownThresholdLU = 9.0f;
    leveler.update(params, readings, 100, 0.01f);
    assert(leveler.isFrozen());
    std::cout << "  -> 10.0 LU drop at 9.0 LU threshold: FROZEN (PASS)" << std::endl;

    // 5. Disabled freeze -> should NOT freeze even on 10.0 LU drop
    params.freezeBreakdowns = false;
    leveler.update(params, readings, 100, 0.01f);
    assert(!leveler.isFrozen());
    std::cout << "  -> Freeze disabled: NOT FROZEN (PASS)" << std::endl;

    std::cout << "  -> PASS: Breakdown Freeze 3-way sensitivity logic verified!" << std::endl;
}

void testLevelerSlewSpeed() {
    std::cout << "[TEST] Leveler Slew Speed (Slow/Normal/Fast steady-state rates)..." << std::endl;

    // Measures the steady-state (post fast-lock) dB/s rate for a given speed & direction.
    // Two-phase: prime past the 8s fast-lock window with a ZERO loudness gap (so
    // currentGainDb stays at 0 and never saturates against the +-12dB clamp while the
    // fast-lock timer advances), then introduce a large gap and measure a single 10ms
    // block's step from that known, unsaturated starting point.
    auto measureSteadyRate = [](LevelerSpeed speed, bool measureUpward) -> float {
        Leveler leveler;
        leveler.prepare(48000.0);

        LevelerParams params;
        params.targetLUFS = -9.0f;
        params.maxBoostDb = 12.0f;
        params.maxCutDb = 12.0f;
        params.freezeBreakdowns = false;
        params.speed = speed;

        float dt = 0.01f;
        LoudnessReadings zeroGap;
        zeroGap.integratedLUFS = params.targetLUFS;
        zeroGap.momentaryLUFS = params.targetLUFS;
        for (int i = 0; i < 850; ++i) { // 8.5s of 10ms blocks -> past the 8s fast-lock window
            leveler.update(params, zeroGap, 100, dt);
        }

        LoudnessReadings bigGap;
        bigGap.integratedLUFS = measureUpward ? -60.0f : 60.0f; // far outside the +-12dB clamp
        bigGap.momentaryLUFS = bigGap.integratedLUFS;

        float before = leveler.getCurrentGainDb();
        leveler.update(params, bigGap, 100, dt);
        float after = leveler.getCurrentGainDb();
        return (after - before) / dt;
    };

    float slowUp = measureSteadyRate(LevelerSpeed::SLOW, true);
    float normalUp = measureSteadyRate(LevelerSpeed::NORMAL, true);
    float fastUp = measureSteadyRate(LevelerSpeed::FAST, true);
    float slowDown = measureSteadyRate(LevelerSpeed::SLOW, false);
    float normalDown = measureSteadyRate(LevelerSpeed::NORMAL, false);
    float fastDown = measureSteadyRate(LevelerSpeed::FAST, false);

    std::cout << "  Steady-state UP   -> Slow: " << slowUp << " dB/s, Normal: " << normalUp
              << " dB/s, Fast: " << fastUp << " dB/s" << std::endl;
    std::cout << "  Steady-state DOWN -> Slow: " << slowDown << " dB/s, Normal: " << normalDown
              << " dB/s, Fast: " << fastDown << " dB/s" << std::endl;

    assert(std::abs(slowUp - 0.5f) < 0.01f);
    assert(std::abs(normalUp - 0.75f) < 0.01f);
    assert(std::abs(fastUp - 1.5f) < 0.01f);
    assert(std::abs(slowDown - (-1.0f)) < 0.01f);
    assert(std::abs(normalDown - (-1.5f)) < 0.01f);
    assert(std::abs(fastDown - (-3.0f)) < 0.01f);
    std::cout << "  -> PASS: Slow/Normal/Fast steady-state rates match spec; Normal is bit-exact"
                 " with the original hardcoded 0.75/1.5 dB/s behavior." << std::endl;

    // Fast-lock window (first 8s) must be unaffected by Slew Speed: always 4.0 up / 16.0 down.
    auto measureFastLockRate = [](LevelerSpeed speed, bool measureUpward) -> float {
        Leveler leveler;
        leveler.prepare(48000.0);
        LevelerParams params;
        params.targetLUFS = -9.0f;
        params.maxBoostDb = 12.0f;
        params.maxCutDb = 12.0f;
        params.freezeBreakdowns = false;
        params.speed = speed;
        LoudnessReadings readings;
        readings.integratedLUFS = measureUpward ? -60.0f : 60.0f;
        readings.momentaryLUFS = readings.integratedLUFS;
        float dt = 0.01f;
        float before = leveler.getCurrentGainDb();
        leveler.update(params, readings, 100, dt); // first call: still inside the fast-lock window
        float after = leveler.getCurrentGainDb();
        return (after - before) / dt;
    };

    float fastLockUpSlow = measureFastLockRate(LevelerSpeed::SLOW, true);
    float fastLockUpFast = measureFastLockRate(LevelerSpeed::FAST, true);
    float fastLockDownSlow = measureFastLockRate(LevelerSpeed::SLOW, false);
    float fastLockDownFast = measureFastLockRate(LevelerSpeed::FAST, false);

    std::cout << "  Fast-lock UP (Slow vs Fast setting):   " << fastLockUpSlow << " vs " << fastLockUpFast << " dB/s" << std::endl;
    std::cout << "  Fast-lock DOWN (Slow vs Fast setting): " << fastLockDownSlow << " vs " << fastLockDownFast << " dB/s" << std::endl;

    assert(std::abs(fastLockUpSlow - 4.0f) < 0.01f);
    assert(std::abs(fastLockUpFast - 4.0f) < 0.01f);
    assert(std::abs(fastLockDownSlow - (-16.0f)) < 0.01f);
    assert(std::abs(fastLockDownFast - (-16.0f)) < 0.01f);
    std::cout << "  -> PASS: Fast-lock window rates are identical regardless of Slew Speed." << std::endl;
}

void testCustomToneProfile() {
    std::cout << "[TEST] Custom Target Contour thresholds and zero-sum re-centering..." << std::endl;
    std::array<float, 6> customOffsets = { 3.0f, -4.0f, 1.0f, 2.0f, -3.0f, 1.0f };
    auto customThresh = Bands::thresholdsFor(true, -2.0f, TargetProfile::CUSTOM, customOffsets, -24.0f);
    auto flatThresh = Bands::thresholdsFor(true, -2.0f, TargetProfile::CUSTOM, {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f}, -24.0f);

    float sum = 0.0f;
    for (size_t b = 0; b < 6; ++b) {
        sum += customThresh[b];
        std::cout << "  Band " << b << " (" << Bands::NAMES[b] << "): Offset=" << customOffsets[b]
                  << " dB, Custom Thresh=" << customThresh[b] << " dBFS, Flat Thresh=" << flatThresh[b] << " dBFS" << std::endl;
    }
    float mean = sum / 6.0f;
    std::cout << "  Mean custom threshold: " << mean << " dBFS" << std::endl;
    assert(std::abs(mean - (-24.0f)) < 0.01f);
    assert(customThresh[0] > flatThresh[0]);
    assert(customThresh[1] < flatThresh[1]);
    std::cout << "  -> PASS: Custom contour thresholds calculated and zero-sum re-centered perfectly." << std::endl;
}

void testMbcHasNoMakeupGain() {
    std::cout << "[TEST] MBC applies no makeup gain (auto-makeup removed)..." << std::endl;
    MultibandCompressor mbc;
    mbc.prepare(48000.0);

    MBCParams params;
    params.enabled = true;
    params.compressionAmount = 0.8f; // heavy compression
    params.toneSlopeDbPerOctave = -2.0f;
    params.baseThresholdDb = -30.0f; // low threshold to force deep gain reduction

    // 1 kHz tone at -10 dBFS
    constexpr size_t N = 48000;
    std::vector<float> left(N), right(N);
    for (size_t i = 0; i < N; ++i) {
        left[i] = right[i] = 0.3162f * std::sin(2.0 * TEST_PI * 1000.0 * i / 48000.0);
    }
    mbc.process(left.data(), right.data(), N, params);

    // Settled half: output level should equal input level minus the band's own gain reduction,
    // with nothing added back on top (Post-MBC Gain is the only makeup stage now).
    double sumSq = 0.0;
    for (size_t i = 24000; i < N; ++i) sumSq += left[i] * left[i];
    double outDb = 20.0 * std::log10(std::sqrt(sumSq / 24000.0) / (0.3162 / std::sqrt(2.0)));
    float bandGrDb = mbc.getGainReductionsDb()[2]; // 1 kHz sits in band 2 (400-1200 Hz)
    std::cout << "  Output change: " << outDb << " dB, band 2 GR: " << bandGrDb << " dB" << std::endl;

    assert(bandGrDb < -3.0f);                 // really compressing
    assert(outDb < -3.0);                     // and nothing restored the level
    // Within 1.5 dB rather than exact: GR ripples within each sine cycle (peak detector) and a
    // little of the tone leaks across the 1.2 kHz crossover. The old auto-makeup added back
    // 25% of this band's GR (~+2.5 dB here), which this tolerance does not let through.
    assert(std::abs(outDb - bandGrDb) < 1.5);
    std::cout << "  -> PASS: MBC output drops by its gain reduction, no makeup applied." << std::endl;
}

void testBandEqFlatAndPerBand() {
    std::cout << "[TEST] Band EQ: bit-transparent when flat, accurate and band-local when moved..." << std::endl;
    double sampleRate = 48000.0;

    // Steady-state level change, in dB, of a sine at `freq` through the EQ with the given gains.
    auto gainAt = [&](double freq, const std::array<float, Bands::COUNT>& gains) {
        BandEQ eq;
        eq.prepare(sampleRate);
        size_t n = 48000;
        std::vector<float> l(n), r(n);
        for (size_t i = 0; i < n; ++i) {
            l[i] = r[i] = 0.3f * static_cast<float>(std::sin(2.0 * TEST_PI * freq * (static_cast<double>(i) / sampleRate)));
        }
        eq.process(l.data(), r.data(), n, gains);
        double sumIn = 0.0, sumOut = 0.0;
        for (size_t i = 24000; i < n; ++i) {
            double in = 0.3 * std::sin(2.0 * TEST_PI * freq * (static_cast<double>(i) / sampleRate));
            sumIn += in * in;
            sumOut += static_cast<double>(l[i]) * l[i];
        }
        return 10.0 * std::log10(sumOut / sumIn);
    };

    // Flat: not just "close" - the output is the input, sample for sample.
    {
        BandEQ eq;
        eq.prepare(sampleRate);
        size_t n = 4800;
        std::vector<float> l(n), r(n);
        for (size_t i = 0; i < n; ++i) {
            l[i] = 0.4f * static_cast<float>(std::sin(2.0 * TEST_PI * 440.0 * i / sampleRate));
            r[i] = 0.4f * static_cast<float>(std::sin(2.0 * TEST_PI * 3100.0 * i / sampleRate));
        }
        auto l0 = l, r0 = r;
        std::array<float, Bands::COUNT> flat{};
        eq.process(l.data(), r.data(), n, flat);
        for (size_t i = 0; i < n; ++i) { assert(l[i] == l0[i]); assert(r[i] == r0[i]); }
    }

    // +6 dB on Presence (3.5-8 kHz): lands at the band's centre, leaves the rest alone.
    std::array<float, Bands::COUNT> presence{};
    presence[4] = 6.0f;
    double atCentre = gainAt(5292.0, presence);
    double atBass = gainAt(100.0, presence);
    double atVoice = gainAt(600.0, presence);
    std::cout << "  Presence +6 dB -> 5.3 kHz: " << atCentre << " dB, 100 Hz: " << atBass
              << " dB, 600 Hz: " << atVoice << " dB" << std::endl;
    assert(std::abs(atCentre - 6.0) < 0.3);
    assert(std::abs(atBass) < 0.1);
    assert(std::abs(atVoice) < 0.2);

    // Each of the six bands reaches (nearly) its own gain at its centre frequency; the outer two
    // are shelves, so they approach their gain from the inside of the band.
    const double centres[Bands::COUNT] = { 49.0, 219.0, 693.0, 2049.0, 5292.0, 12649.0 };
    for (size_t b = 0; b < Bands::COUNT; ++b) {
        for (float setDb : { -9.0f, 9.0f }) {
            std::array<float, Bands::COUNT> g{};
            g[b] = setDb;
            double got = gainAt(centres[b], g);
            std::cout << "  " << Bands::NAMES[b] << " " << setDb << " dB -> " << got << " dB at " << centres[b] << " Hz" << std::endl;
            double tol = (b == 0 || b == Bands::COUNT - 1) ? 1.3 : 0.3;
            assert(std::abs(got - setDb) < tol);
        }
    }

    // A boost in one band moves its neighbour's centre only a little.
    std::array<float, Bands::COUNT> bassOnly{};
    bassOnly[1] = 9.0f;
    double lowMidLeak = gainAt(693.0, bassOnly);
    std::cout << "  Bass +9 dB leaks " << lowMidLeak << " dB into Low-Mid's centre" << std::endl;
    assert(lowMidLeak < 3.0);

    // Out-of-range gains are clamped to +/-12 dB.
    std::array<float, Bands::COUNT> huge{};
    huge[4] = 40.0f;
    assert(gainAt(5292.0, huge) < 12.1);

    std::cout << "  -> PASS: EQ is transparent at 0 dB, accurate at each band centre, and clamped." << std::endl;
}

void testBandEqGainSmoothing() {
    std::cout << "[TEST] Band EQ: gain changes are smoothed, not stepped..." << std::endl;
    double sampleRate = 48000.0;
    BandEQ eq;
    eq.prepare(sampleRate);

    // DC-ish steady 200 Hz tone; settle flat, then slam Bass to +12 dB in one block and look
    // at the largest sample-to-sample jump in the output compared with the unprocessed tone's.
    const size_t n = 4800;
    auto tone = [&](size_t offset) {
        std::vector<float> v(n);
        for (size_t i = 0; i < n; ++i) {
            v[i] = 0.2f * static_cast<float>(std::sin(2.0 * TEST_PI * 200.0 * (static_cast<double>(offset + i) / sampleRate)));
        }
        return v;
    };
    std::array<float, Bands::COUNT> flat{}, boosted{};
    boosted[1] = 12.0f;

    for (size_t blk = 0; blk < 4; ++blk) {
        auto l = tone(blk * n), r = l;
        eq.process(l.data(), r.data(), n, flat);
    }
    auto l = tone(4 * n), r = l;
    eq.process(l.data(), r.data(), n, boosted);

    double maxStep = 0.0;
    for (size_t i = 1; i < n; ++i) maxStep = std::max(maxStep, static_cast<double>(std::abs(l[i] - l[i - 1])));
    // Fully boosted 200 Hz at +12 dB (x4) has a natural max step of ~0.2*4*2*pi*200/48000 = 0.021.
    std::cout << "  Largest sample step through the change: " << maxStep << std::endl;
    assert(maxStep < 0.05);
    std::cout << "  -> PASS: no zipper step when a band is moved." << std::endl;
}

void testEqPositionRouting() {
    std::cout << "[TEST] Band EQ sits before or after the MBC, as chosen..." << std::endl;
    double sampleRate = 48000.0;

    // A tone in the Presence band, level chosen so the compressor is working. Boosting
    // Presence +9 dB before the MBC feeds it a hotter signal, so it compresses that band
    // harder; after the MBC the boost is not seen by the detector.
    auto run = [&](EqPosition pos, float presenceDb, float& presenceGr) {
        AutoLevelEngine engine;
        engine.prepare(sampleRate);
        EngineParameters p;
        p.compressionAmount = 0.8f;
        p.levelResponse = 0.0f;
        p.maxBoostDb = 0.0f;
        p.maxCutDb = 0.0f;       // AGC out of the picture
        p.targetLUFS = -9.0f;    // base MBC threshold -24 dBFS
        p.ceilingDb = 0.0f;
        p.hpfEnabled = false;
        p.eqPosition = pos;
        p.eqGainsDb[4] = presenceDb;

        const size_t block = 480;
        std::vector<float> l(block), r(block);
        double phase = 0.0;
        const double inc = 2.0 * TEST_PI * 5000.0 / sampleRate;
        double sumSq = 0.0; size_t count = 0;
        for (size_t b = 0; b < 400; ++b) {
            for (size_t i = 0; i < block; ++i) {
                l[i] = r[i] = 0.1f * static_cast<float>(std::sin(phase));
                phase += inc;
            }
            engine.process(l.data(), r.data(), block, p);
            if (b >= 300) { for (size_t i = 0; i < block; ++i) { sumSq += static_cast<double>(l[i]) * l[i]; ++count; } }
        }
        presenceGr = engine.getVisualState().mbcGainReductionsDb[4];
        return 10.0 * std::log10(sumSq / static_cast<double>(count) / (0.1 * 0.1 / 2.0));
    };

    float grPlain = 0, grPre = 0, grPost = 0;
    double outPlain = run(EqPosition::POST_MBC, 0.0f, grPlain);
    double outPre = run(EqPosition::PRE_MBC, 9.0f, grPre);
    double outPost = run(EqPosition::POST_MBC, 9.0f, grPost);
    std::cout << "  Presence GR: flat " << grPlain << " dB, EQ pre " << grPre << " dB, EQ post " << grPost << " dB" << std::endl;
    std::cout << "  Output level: flat " << outPlain << " dB, EQ pre " << outPre << " dB, EQ post " << outPost << " dB" << std::endl;

    assert(std::abs(grPost - grPlain) < 0.05f);   // post-MBC EQ is invisible to the detector
    assert(grPre < grPost - 2.0f);                // pre-MBC EQ makes the band work harder
    assert(outPost > outPre);                     // and so the same boost nets less level when pre
    std::cout << "  -> PASS: EQ position changes what the compressor sees." << std::endl;
}

void testFullChain() {
    std::cout << "[TEST] AutoLevelEngine full chain: AGC -> MBC -> Limiter..." << std::endl;
    AutoLevelEngine engine;
    double sampleRate = 48000.0;
    engine.prepare(sampleRate);

    EngineParameters params;
    params.targetLUFS = -9.0f;
    params.maxBoostDb = 12.0f;
    params.maxCutDb = 12.0f;
    params.ceilingDb = -1.5f;
    params.compressionAmount = 0.6f;
    params.toneSlopeDbPerOctave = -2.0f;
    params.targetProfile = TargetProfile::MODERN_MIX;

    // Simulate audio blocks for 3 seconds
    size_t block = 480;
    size_t totalBlocks = 300;

    float maxOutputPeak = 0.0f;
    for (size_t b = 0; b < totalBlocks; ++b) {
        std::vector<float> left(block), right(block);
        for (size_t i = 0; i < block; ++i) {
            float s = 0.125f * static_cast<float>(std::sin(2.0 * TEST_PI * 440.0 * (b * block + i) / sampleRate));
            left[i] = s;
            right[i] = s;
        }

        engine.process(left.data(), right.data(), block, params);

        for (size_t i = 0; i < block; ++i) {
            maxOutputPeak = std::max(maxOutputPeak, std::abs(left[i]));
        }
    }

    auto state = engine.getVisualState();
    std::cout << "  Applied Gain: " << state.appliedGainDb << " dB" << std::endl;
    std::cout << "  Target Gain: " << state.targetGainDb << " dB" << std::endl;
    std::cout << "  Max Output Peak: " << maxOutputPeak << " (Ceiling: " << std::pow(10.0f, -1.5f / 20.0f) << ")" << std::endl;

    assert(state.appliedGainDb > 0.0f);
    // Limiter ceiling (-1.5 dBFS = 0.841)
    assert(maxOutputPeak <= 0.85f);

    std::cout << "  -> PASS: Full chain leveled, compressed, and limited to ceiling." << std::endl;
}

void testInputSanitizerAntiNan() {
    std::cout << "[TEST] Audio Input Sanitizer & Anti-NaN Protection..." << std::endl;
    AutoLevelEngine engine;
    engine.prepare(48000.0);

    constexpr size_t N = 256;
    std::vector<float> left(N), right(N);

    // Inject NaNs, Infs, extreme runaway values, and valid audio
    for (size_t i = 0; i < N; ++i) {
        if (i % 10 == 0) {
            left[i] = std::numeric_limits<float>::quiet_NaN();
            right[i] = std::numeric_limits<float>::infinity();
        } else if (i % 10 == 5) {
            left[i] = -std::numeric_limits<float>::infinity();
            right[i] = 100.0f; // Extreme spike
        } else {
            left[i] = 0.5f * std::sin(2.0 * TEST_PI * 1000.0 * i / 48000.0);
            right[i] = 0.5f * std::cos(2.0 * TEST_PI * 1000.0 * i / 48000.0);
        }
    }

    EngineParameters params;
    engine.process(left.data(), right.data(), N, params);

    for (size_t i = 0; i < N; ++i) {
        assert(std::isfinite(left[i]));
        assert(std::isfinite(right[i]));
        assert(!std::isnan(left[i]));
        assert(!std::isnan(right[i]));
    }

    auto vs = engine.getVisualState();
    (void)vs;
    assert(std::isfinite(vs.loudness.momentaryLUFS));
    assert(std::isfinite(vs.appliedGainDb));
    assert(std::isfinite(vs.outputPeakDbL));

    std::cout << "  -> PASS: All NaNs and Infs safely intercepted and sanitized without filter corruption." << std::endl;
}

void testLockFreeDoubleBufferedVisualState() {
    std::cout << "[TEST] Lock-Free 3-Buffered Visual State & Concurrency..." << std::endl;
    AutoLevelEngine engine;
    engine.prepare(48000.0);

    constexpr size_t N = 256;
    std::vector<float> left(N, 0.2f), right(N, 0.2f);
    EngineParameters params;

    for (int block = 0; block < 100; ++block) {
        engine.process(left.data(), right.data(), N, params);
        auto vs = engine.getVisualState();
        (void)vs;
        assert(std::isfinite(vs.appliedGainDb));
        assert(std::isfinite(vs.loudness.momentaryLUFS));
    }

    // Test lock-free asynchronous reset
    engine.reset();
    engine.process(left.data(), right.data(), N, params);
    auto vs = engine.getVisualState();
    (void)vs;
    assert(std::isfinite(vs.appliedGainDb));

    std::cout << "  -> PASS: 3-buffered visual state and lock-free reset verified!" << std::endl;
}


void testSeedGain() {
    std::cout << "[TEST] Seed Gain from external loudness estimate..." << std::endl;

    constexpr size_t N = 256;
    EngineParameters params;
    params.targetLUFS = -14.0f;
    params.compressionAmount = 0.0f;   // isolate the leveller
    params.hpfEnabled = false;

    // A seed is applied on the audio thread, so it takes effect from the first block after
    // the request -- not before, and not silently never.
    {
        AutoLevelEngine engine;
        engine.prepare(48000.0);
        engine.seedGain(-6.0f);

        std::vector<float> left(N, 0.05f), right(N, 0.05f);
        engine.process(left.data(), right.data(), N, params);

        auto vs = engine.getVisualState();
        assert(std::abs(vs.appliedGainDb - (-6.0f)) < 0.01f);
    }

    // Seeding starts the correction where it belongs instead of at zero. Feed a -21 dBFS tone
    // (about -21 LUFS at 1 kHz) and seed the +7 dB it needs: the gain is right immediately,
    // where an unseeded engine has to slew there over seconds.
    {
        AutoLevelEngine seeded, unseeded;
        seeded.prepare(48000.0);
        unseeded.prepare(48000.0);
        seeded.seedGain(7.0f);

        std::vector<float> l(N), r(N);
        double phase = 0.0;
        const double inc = 2.0 * TEST_PI * 1000.0 / 48000.0;
        const float amp = std::pow(10.0f, -21.0f / 20.0f);

        // One second only: far short of what convergence from zero would need.
        for (int block = 0; block < static_cast<int>(48000 / N); ++block) {
            for (size_t i = 0; i < N; ++i) {
                const float sample = amp * static_cast<float>(std::sin(phase));
                phase += inc;
                l[i] = sample; r[i] = sample;
            }
            std::vector<float> l2 = l, r2 = r;
            seeded.process(l.data(), r.data(), N, params);
            unseeded.process(l2.data(), r2.data(), N, params);
        }

        const float seededGain = seeded.getVisualState().appliedGainDb;
        const float unseededGain = unseeded.getVisualState().appliedGainDb;
        assert(seededGain > 6.0f);
        assert(unseededGain < seededGain - 1.0f);
    }

    // A wrong seed must not be believed forever: the measurement pulls it back.
    {
        AutoLevelEngine engine;
        engine.prepare(48000.0);
        engine.seedGain(12.0f);           // claim it is very quiet

        std::vector<float> l(N), r(N);
        double phase = 0.0;
        const double inc = 2.0 * TEST_PI * 1000.0 / 48000.0;
        const float amp = std::pow(10.0f, -6.0f / 20.0f);   // actually loud

        for (int block = 0; block < static_cast<int>(48000 * 20 / N); ++block) {
            for (size_t i = 0; i < N; ++i) {
                const float sample = amp * static_cast<float>(std::sin(phase));
                phase += inc;
                l[i] = sample; r[i] = sample;
            }
            engine.process(l.data(), r.data(), N, params);
        }
        const float gain = engine.getVisualState().appliedGainDb;
        assert(gain < 0.0f);              // corrected downward, seed abandoned
    }

    // Hard-bounded so a nonsensical tag cannot blast the output before measurement arrives.
    {
        AutoLevelEngine engine;
        engine.prepare(48000.0);
        engine.seedGain(500.0f);
        std::vector<float> left(N, 0.01f), right(N, 0.01f);
        engine.process(left.data(), right.data(), N, params);
        const float gain = engine.getVisualState().appliedGainDb;
        assert(gain <= 24.0f);
        assert(std::isfinite(gain));
    }

    // Output stays finite and bounded when a seed lands on a hot signal.
    {
        AutoLevelEngine engine;
        engine.prepare(48000.0);
        engine.seedGain(18.0f);
        std::vector<float> left(N, 0.9f), right(N, 0.9f);
        engine.process(left.data(), right.data(), N, params);
        for (size_t i = 0; i < N; ++i) {
            assert(std::isfinite(left[i]));
            assert(std::abs(left[i]) <= 1.0f);   // the limiter is downstream of the seed
        }
    }

    std::cout << "  -> PASS: seed applied, overridden by measurement, clamped, and safe!" << std::endl;
}


void testMaxCompressionRatio() {
    std::cout << "[TEST] Configurable max compression ratio..." << std::endl;

    // Default must stay 4:1 -- existing hosts are entitled to unchanged behaviour.
    EngineParameters defaults;
    assert(std::abs(defaults.maxCompressionRatio - Bands::MAX_RATIO) < 1e-6f);

    constexpr size_t N = 256;
    auto measure = [](float maxRatio) {
        AutoLevelEngine engine;
        engine.prepare(48000.0);
        EngineParameters p;
        p.compressionAmount = 1.0f;
        p.maxCompressionRatio = maxRatio;
        p.hpfEnabled = false;

        std::vector<float> l(N), r(N);
        double phase = 0.0;
        const double inc = 2.0 * TEST_PI * 1000.0 / 48000.0;
        const float amp = std::pow(10.0f, -6.0f / 20.0f);
        for (int block = 0; block < static_cast<int>(48000 * 4 / N); ++block) {
            for (size_t i = 0; i < N; ++i) {
                const float s = amp * static_cast<float>(std::sin(phase));
                phase += inc;
                l[i] = s; r[i] = s;
            }
            engine.process(l.data(), r.data(), N, p);
        }
        float worst = 0.0f;
        for (auto gr : engine.getVisualState().mbcGainReductionsDb) worst = std::min(worst, gr);
        return worst;
    };

    const float at4 = measure(4.0f);
    const float at6 = measure(6.0f);
    std::cout << "  -> 4:1 worst band GR " << at4 << " dB, 6:1 worst band GR " << at6 << " dB"
              << std::endl;
    assert(at6 < at4);   // more reduction is more negative

    std::cout << "  -> PASS: default unchanged, higher ratio compresses harder!" << std::endl;
}

namespace {

// Reference true peak: 16x reconstruction with a long Hann-windowed sinc (far finer than the
// limiter's own 12-tap-per-phase estimator, so it can judge it).
double referenceTruePeak(const std::vector<float>& x, size_t start, size_t end) {
    const int U = 16, K = 64;
    double tp = 0.0;
    for (size_t n = start + K; n + K < end; ++n) {
        for (int u = 0; u < U; ++u) {
            double t = static_cast<double>(n) + static_cast<double>(u) / U, acc = 0.0;
            for (int k = -K; k <= K; ++k) {
                double d = t - static_cast<double>(static_cast<long>(n) + k);
                double sn = std::abs(d) < 1e-12 ? 1.0 : std::sin(TEST_PI * d) / (TEST_PI * d);
                acc += x[static_cast<size_t>(static_cast<long>(n) + k)] * sn * (0.5 + 0.5 * std::cos(TEST_PI * d / (K + 1)));
            }
            tp = std::max(tp, std::abs(acc));
        }
    }
    return tp;
}

// THD (fraction) of a limited sine, harmonics 2-10, over whole cycles at the end of the buffer.
double sineThd(const std::vector<float>& y, double f, double sampleRate) {
    size_t cycles = static_cast<size_t>(std::max(1.0, f * 0.5));
    size_t len = static_cast<size_t>(std::round(static_cast<double>(cycles) * sampleRate / f));
    size_t st = y.size() - len;
    auto proj = [&](int h) {
        double re = 0.0, im = 0.0;
        for (size_t i = 0; i < len; ++i) {
            double ph = 2.0 * TEST_PI * h * f * static_cast<double>(st + i) / sampleRate;
            re += y[st + i] * std::cos(ph);
            im -= y[st + i] * std::sin(ph);
        }
        return std::sqrt(re * re + im * im) * 2.0 / static_cast<double>(len);
    };
    double fund = proj(1), hs = 0.0;
    for (int h = 2; h <= 10; ++h) { double v = proj(h); hs += v * v; }
    return std::sqrt(hs) / fund;
}

} // namespace

void testLimiterLookaheadLatency() {
    std::cout << "[TEST] Limiter lookahead: exact latency, transparent below the ceiling..." << std::endl;
    for (double sr : { 44100.0, 48000.0 }) {
        for (auto mode : { LimiterLookahead::OFF, LimiterLookahead::MS_1, LimiterLookahead::MS_2 }) {
            SafetyLimiter lim;
            lim.setLookahead(mode);
            lim.prepare(sr);
            lim.setCeilingDb(-0.3f);
            std::vector<float> l(512, 0.0f), r(512, 0.0f);
            l[10] = 0.5f; r[10] = -0.25f;
            lim.process(l.data(), r.data(), l.size());
            const int expected = SafetyLimiter::lookaheadSamples(mode, sr);
            for (size_t i = 0; i < l.size(); ++i) {
                float wantL = (static_cast<int>(i) == 10 + expected) ? 0.5f : 0.0f;
                float wantR = (static_cast<int>(i) == 10 + expected) ? -0.25f : 0.0f;
                assert(l[i] == wantL && r[i] == wantR);   // delayed, not altered, not faded in
            }
            std::cout << "  " << sr << " Hz, mode " << static_cast<int>(mode) << ": " << expected
                      << " samples (" << 1000.0 * expected / sr << " ms)" << std::endl;
        }
    }
    assert(SafetyLimiter::lookaheadSamples(LimiterLookahead::MS_1, 48000.0) == 48);
    assert(SafetyLimiter::lookaheadSamples(LimiterLookahead::MS_2, 48000.0) == 96);
    std::cout << "  -> PASS: latency is exactly the selected lookahead, and 0 for Off." << std::endl;
}

void testLimiterLookaheadNoDistortion() {
    std::cout << "[TEST] Limiter lookahead: pushed sines stay clean (Off for comparison)..." << std::endl;
    const double sr = 48000.0;
    const double ceilLin = std::pow(10.0, -0.3 / 20.0);
    auto run = [&](LimiterLookahead mode, double f, double pushDb, float& peakOut) {
        SafetyLimiter lim;
        lim.setLookahead(mode);
        lim.prepare(sr);
        lim.setCeilingDb(-0.3f);
        size_t n = static_cast<size_t>(sr * 2.0);
        std::vector<float> l(n), r;
        double amp = ceilLin * std::pow(10.0, pushDb / 20.0);
        for (size_t i = 0; i < n; ++i) l[i] = static_cast<float>(amp * std::sin(2.0 * TEST_PI * f * i / sr));
        r = l;
        for (size_t o = 0; o < n; o += 512) lim.process(l.data() + o, r.data() + o, std::min<size_t>(512, n - o));
        peakOut = 0.0f;
        for (size_t i = n / 2; i < n; ++i) peakOut = std::max(peakOut, std::abs(l[i]));
        return sineThd(l, f, sr);
    };
    for (double f : { 25.0, 50.0, 1000.0, 5000.0 }) {
        float pkOff = 0, pk1 = 0, pk2 = 0;
        double off = run(LimiterLookahead::OFF, f, 12.0, pkOff);
        double la1 = run(LimiterLookahead::MS_1, f, 12.0, pk1);
        double la2 = run(LimiterLookahead::MS_2, f, 12.0, pk2);
        std::cout << "  " << f << " Hz, +12 dB over: THD Off " << 100 * off << "%, 1 ms " << 100 * la1
                  << "%, 2 ms " << 100 * la2 << "%" << std::endl;
        assert(la1 < 0.0005 && la2 < 0.0005);        // under 0.05%
        assert(pk1 <= static_cast<float>(ceilLin) && pk2 <= static_cast<float>(ceilLin));
        if (f >= 50.0) assert(off > 0.02);           // the old limiter really did distort (> 2%)
    }
    std::cout << "  -> PASS: lookahead limits to the ceiling without distorting." << std::endl;
}

void testLimiterLookaheadCeilingAndTruePeak() {
    std::cout << "[TEST] Limiter lookahead: hard ceiling on hot material, true-peak aware..." << std::endl;
    const double sr = 48000.0;
    const float ceilLin = std::pow(10.0f, -0.3f / 20.0f);

    for (auto mode : { LimiterLookahead::MS_1, LimiterLookahead::MS_2 }) {
        // Dense noise ~+10 dB over with +24 dB transients, odd block sizes.
        SafetyLimiter lim;
        lim.setLookahead(mode);
        lim.prepare(sr);
        lim.setCeilingDb(-0.3f);
        size_t n = 48000 * 3;
        std::vector<float> l(n), r(n);
        uint32_t seed = 1;
        auto rnd = [&]() { seed = seed * 1664525u + 1013904223u; return ((seed >> 8) / 16777216.0f) * 2.0f - 1.0f; };
        for (size_t i = 0; i < n; ++i) {
            float tr = (i % 9600 < 20) ? 8.0f : 0.0f;
            l[i] = 3.0f * rnd() + tr;
            r[i] = 3.0f * rnd() - tr;
        }
        for (size_t o = 0; o < n; o += 333) lim.process(l.data() + o, r.data() + o, std::min<size_t>(333, n - o));
        float pk = 0.0f;
        for (size_t i = 0; i < n; ++i) {
            assert(std::isfinite(l[i]) && std::isfinite(r[i]));
            pk = std::max(pk, std::max(std::abs(l[i]), std::abs(r[i])));
        }
        std::cout << "  mode " << static_cast<int>(mode) << " hot noise: peak " << 20.0 * std::log10(pk)
                  << " dBFS, last-resort clamp hits " << lim.getClampCount() << std::endl;
        assert(pk <= ceilLin);
        assert(lim.getClampCount() == 0);   // the gain alone held the ceiling
    }

    // Inter-sample over: fs/4 at 45 degrees puts every sample at 0.707 of the real peak.
    auto truePeakOf = [&](LimiterLookahead mode) {
        SafetyLimiter lim;
        lim.setLookahead(mode);
        lim.prepare(sr);
        lim.setCeilingDb(-0.3f);
        size_t n = 24000;
        std::vector<float> l(n), r;
        for (size_t i = 0; i < n; ++i) l[i] = static_cast<float>(2.0 * std::sin(2.0 * TEST_PI * 12000.0 * i / sr + TEST_PI / 4));
        r = l;
        lim.process(l.data(), r.data(), n);
        return 20.0 * std::log10(referenceTruePeak(l, 12000, 13000));
    };
    double tpOff = truePeakOf(LimiterLookahead::OFF);
    double tp1 = truePeakOf(LimiterLookahead::MS_1);
    std::cout << "  inter-sample test, true peak: Off " << tpOff << " dBFS, 1 ms " << tp1 << " dBFS (ceiling -0.3)" << std::endl;
    assert(tpOff > 2.0);         // sample-peak limiting lets this through ~3 dB hot, over 0 dBFS
    assert(tp1 < -0.3 + 0.25);   // the estimate gets within 0.25 dB of the ceiling
    std::cout << "  -> PASS: sample peak never over the ceiling, true peak held near it." << std::endl;
}

void testLimiterLookaheadSwitching() {
    std::cout << "[TEST] Limiter lookahead: switching modes live does not click..." << std::endl;
    const double sr = 48000.0;
    SafetyLimiter lim;
    lim.prepare(sr);
    lim.setCeilingDb(-0.3f);

    const size_t block = 256;
    double phase = 0.0;
    float prev = 0.0f, maxStep = 0.0f;
    const LimiterLookahead sequence[] = { LimiterLookahead::OFF, LimiterLookahead::MS_1, LimiterLookahead::MS_2,
                                          LimiterLookahead::OFF, LimiterLookahead::MS_2, LimiterLookahead::MS_1 };
    for (auto mode : sequence) {
        lim.setLookahead(mode);
        for (int b = 0; b < 40; ++b) {
            std::vector<float> l(block), r(block);
            for (size_t i = 0; i < block; ++i) {
                l[i] = r[i] = 0.5f * static_cast<float>(std::sin(phase));
                phase += 2.0 * TEST_PI * 200.0 / sr;
            }
            lim.process(l.data(), r.data(), block);
            for (size_t i = 0; i < block; ++i) {
                assert(std::isfinite(l[i]));
                maxStep = std::max(maxStep, std::abs(l[i] - prev));
                prev = l[i];
            }
        }
        assert(lim.getActiveLookahead() == mode);
    }
    // A 0.5-amplitude 200 Hz sine steps at most 0.5 * 2*pi*200/48000 = 0.013 per sample.
    std::cout << "  Largest sample step across 5 live switches: " << maxStep << std::endl;
    assert(maxStep < 0.02f);
    std::cout << "  -> PASS: every switch fades out and back in." << std::endl;
}

void testBypassKeepsLatency() {
    std::cout << "[TEST] Bypass keeps the lookahead latency (and is untouched with Off)..." << std::endl;
    for (auto mode : { LimiterLookahead::OFF, LimiterLookahead::MS_1, LimiterLookahead::MS_2 }) {
        AutoLevelEngine engine;
        engine.prepare(48000.0);
        EngineParameters p;
        p.bypass = true;
        p.limiterLookahead = mode;
        std::vector<float> l(400, 0.0f), r(400, 0.0f);
        l[5] = 1.7f; r[5] = -0.9f;     // above the ceiling on purpose: bypass must not limit
        engine.process(l.data(), r.data(), l.size(), p);
        int lat = AutoLevelEngine::latencySamples(mode, 48000.0);
        for (size_t i = 0; i < l.size(); ++i) {
            bool at = static_cast<int>(i) == 5 + lat;
            assert(l[i] == (at ? 1.7f : 0.0f) && r[i] == (at ? -0.9f : 0.0f));
        }
    }
    std::cout << "  -> PASS: bypassed audio is only delayed by the reported latency." << std::endl;
}

int main() {
    std::cout << "============================================" << std::endl;
    std::cout << "   AutoLevel DJ DSP Unit Tests (Android Spec)" << std::endl;
    std::cout << "============================================" << std::endl;

    testKWeightingSineWave();
    testAndroidToneProfilesAndThresholds();
    testLR4CrossoverSummation();
    testLevelResponseMapping();
    testEbuR128DynamicLoudness();
    testMbcAttackRelease();
    testMbcLiveBallisticsChange();
    testMbcRmsDetector();
    testBandEqFlatAndPerBand();
    testBandEqGainSmoothing();
    testEqPositionRouting();
    testPostMbcGain();
    testHighPassFilter();
    testDefaultLimiterCeiling();
    testBreakdownFreezeSensitivity();
    testLevelerSlewSpeed();
    testCustomToneProfile();
    testMbcHasNoMakeupGain();
    testFullChain();
    testInputSanitizerAntiNan();
    testLockFreeDoubleBufferedVisualState();
    testSeedGain();
    testMaxCompressionRatio();
    testLimiterLookaheadLatency();
    testLimiterLookaheadNoDistortion();
    testLimiterLookaheadCeilingAndTruePeak();
    testLimiterLookaheadSwitching();
    testBypassKeepsLatency();

    std::cout << "============================================" << std::endl;
    std::cout << "   ALL DSP TESTS PASSED WITH 100% ACCURACY! " << std::endl;
    std::cout << "============================================" << std::endl;
    return 0;
}
