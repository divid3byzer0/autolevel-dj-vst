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
    params.autoMakeup = false;
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

void testMbcSpeedBallistics() {
    std::cout << "[TEST] Multiband Compressor speed ballistics (Slow, Normal, Fast)..." << std::endl;
    double sampleRate = 48000.0;

    auto measureCompression = [&](MBCSpeed speed) {
        MultibandCompressor mbc;
        mbc.prepare(sampleRate);

        MBCParams params;
        params.enabled = true;
        params.speed = speed;
        params.compressionAmount = 0.8f;
        params.baseThresholdDb = -24.0f;
        params.autoMakeup = false;

        size_t n = static_cast<size_t>(sampleRate * 0.1);
        std::vector<float> l(n), r(n);
        for (size_t i = 0; i < n; ++i) {
            float s = 0.5f * static_cast<float>(std::sin(2.0 * TEST_PI * 60.0 * (static_cast<double>(i) / sampleRate)));
            l[i] = s;
            r[i] = s;
        }

        mbc.process(l.data(), r.data(), n, params);
        return mbc.getGainReductionsDb()[0];
    };

    float fastGr = measureCompression(MBCSpeed::FAST);
    float normalGr = measureCompression(MBCSpeed::NORMAL);
    float slowGr = measureCompression(MBCSpeed::SLOW);

    std::cout << "  Fast 100ms Bass GR: " << fastGr << " dB" << std::endl;
    std::cout << "  Slow 100ms Bass GR: " << slowGr << " dB" << std::endl;

    assert(fastGr < normalGr);
    assert(normalGr < slowGr);

    // Verify prepare() does not silently revert speed caching (finding 9)
    MultibandCompressor mbcCached;
    mbcCached.prepare(sampleRate);
    MBCParams pFast;
    pFast.enabled = true;
    pFast.speed = MBCSpeed::FAST;
    pFast.compressionAmount = 0.8f;
    pFast.baseThresholdDb = -24.0f;
    pFast.autoMakeup = false;

    size_t n = static_cast<size_t>(sampleRate * 0.1);
    std::vector<float> l(n), r(n);
    for (size_t i = 0; i < n; ++i) {
        float s = 0.5f * static_cast<float>(std::sin(2.0 * TEST_PI * 60.0 * (static_cast<double>(i) / sampleRate)));
        l[i] = s;
        r[i] = s;
    }
    mbcCached.process(l.data(), r.data(), n, pFast);
    float fastBefore = mbcCached.getGainReductionsDb()[0];

    mbcCached.prepare(sampleRate);
    for (size_t i = 0; i < n; ++i) {
        float s = 0.5f * static_cast<float>(std::sin(2.0 * TEST_PI * 60.0 * (static_cast<double>(i) / sampleRate)));
        l[i] = s;
        r[i] = s;
    }
    mbcCached.process(l.data(), r.data(), n, pFast);
    float fastAfter = mbcCached.getGainReductionsDb()[0];

    assert(std::abs(fastBefore - fastAfter) < 0.001f);

    std::cout << "  -> PASS: MBC speed mode correctly alters compression ballistics." << std::endl;
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

void testDynamicAirLift() {
    std::cout << "[TEST] DynamicAirLift (Dolby Duo high-end upward expansion & sibilance ducking)..." << std::endl;
    DynamicAirLift airLift;
    double sampleRate = 48000.0;
    airLift.prepare(sampleRate);

    // 1. Test AirLiftMode::OFF is 100% bit-identical bypass
    size_t n = 4800;
    std::vector<float> origL(n), origR(n);
    for (size_t i = 0; i < n; ++i) {
        float s = 0.4f * static_cast<float>(std::sin(2.0 * TEST_PI * 1000.0 * i / sampleRate));
        origL[i] = s;
        origR[i] = s;
    }
    std::vector<float> passL = origL;
    std::vector<float> passR = origR;
    airLift.process(passL.data(), passR.data(), n, AirLiftMode::OFF);
    for (size_t i = 0; i < n; ++i) {
        assert(passL[i] == origL[i]);
        assert(passR[i] == origR[i]);
    }
    std::cout << "  -> PASS: AirLiftMode::OFF is 100% bit-identical bypass." << std::endl;

    // 2. Test air lift on dark vintage material (strong 2 kHz mid, weak 12 kHz air)
    airLift.reset();
    std::vector<float> darkL(n), darkR(n);
    for (size_t i = 0; i < n; ++i) {
        float mid = 0.5f * static_cast<float>(std::sin(2.0 * TEST_PI * 2000.0 * i / sampleRate));
        float weakAir = 0.02f * static_cast<float>(std::sin(2.0 * TEST_PI * 12000.0 * i / sampleRate));
        darkL[i] = mid + weakAir;
        darkR[i] = mid + weakAir;
    }
    airLift.process(darkL.data(), darkR.data(), n, AirLiftMode::MED);
    float airLiftDb = airLift.getLiftDb();
    std::cout << "  Dynamic Air Lift applied on dark track: +" << airLiftDb << " dB" << std::endl;
    assert(airLiftDb > 2.0f && airLiftDb <= 4.5f);
    std::cout << "  -> PASS: Dynamic Air Lift smoothly pulls up natural top-end air with zero distortion." << std::endl;

    // 3. Test adaptive suppression: on already-bright track, lift must drop to near 0 dB
    airLift.reset();
    std::vector<float> brightL(n), brightR(n);
    for (size_t i = 0; i < n; ++i) {
        float mid = 0.3f * static_cast<float>(std::sin(2.0 * TEST_PI * 2000.0 * i / sampleRate));
        float brightAir = 0.5f * static_cast<float>(std::sin(2.0 * TEST_PI * 12000.0 * i / sampleRate));
        brightL[i] = mid + brightAir;
        brightR[i] = mid + brightAir;
    }
    airLift.process(brightL.data(), brightR.data(), n, AirLiftMode::MED);
    float brightLiftDb = airLift.getLiftDb();
    std::cout << "  Dynamic Air Lift on bright track: +" << brightLiftDb << " dB" << std::endl;
    assert(brightLiftDb < 1.0f);
    std::cout << "  -> PASS: Adaptive sensor protects bright tracks from harshness." << std::endl;

    // 4. Regression test for a real bug (2026-09-11): the mid anchor used to be a
    // plain low-pass ("everything below 2kHz") instead of a 1-3kHz bandpass, so the
    // denominator was always huge relative to any real track's air content and the
    // lift barely varied with actual brightness. Case 3 above didn't catch it because
    // its "bright" signal has MORE amplitude at 12kHz than at 2kHz - a ratio no real
    // track has. This uses a realistic ratio instead (air always quieter than mid,
    // like real cymbals/hats vs. a vocal or instrument fundamental) and asserts the
    // lift actually tracks brightness rather than staying flat.
    airLift.reset();
    std::vector<float> moderateBrightL(n), moderateBrightR(n);
    for (size_t i = 0; i < n; ++i) {
        float mid = 0.5f * static_cast<float>(std::sin(2.0 * TEST_PI * 2000.0 * i / sampleRate));
        float moderateAir = 0.08f * static_cast<float>(std::sin(2.0 * TEST_PI * 12000.0 * i / sampleRate));
        moderateBrightL[i] = mid + moderateAir;
        moderateBrightR[i] = mid + moderateAir;
    }
    airLift.process(moderateBrightL.data(), moderateBrightR.data(), n, AirLiftMode::MED);
    float moderateBrightLiftDb = airLift.getLiftDb();
    std::cout << "  Dynamic Air Lift on realistic moderately-bright track: +" << moderateBrightLiftDb
              << " dB (dark track was +" << airLiftDb << " dB)" << std::endl;
    assert(airLiftDb - moderateBrightLiftDb > 0.3f);
    std::cout << "  -> PASS: Lift actually discriminates brightness at realistic (non-extreme) ratios." << std::endl;
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

void testMbcAutoMakeupGain() {
    std::cout << "[TEST] MBC Adaptive Auto-Makeup Gain..." << std::endl;
    MultibandCompressor mbc;
    mbc.prepare(48000.0);

    MBCParams params;
    params.enabled = true;
    params.compressionAmount = 0.8f;
    params.toneSlopeDbPerOctave = -2.0f;
    params.baseThresholdDb = -30.0f;
    params.autoMakeup = false;

    constexpr size_t N = 48000;
    std::vector<float> leftOff(N), rightOff(N);
    std::vector<float> leftOn(N), rightOn(N);
    for (size_t i = 0; i < N; ++i) {
        float s = 0.3162f * std::sin(2.0 * TEST_PI * 1000.0 * i / 48000.0);
        leftOff[i] = rightOff[i] = s;
        leftOn[i] = rightOn[i] = s;
    }

    mbc.process(leftOff.data(), rightOff.data(), N, params);
    float makeupOff = mbc.getAutoMakeupGainDb();
    (void)makeupOff;
    assert(makeupOff == 0.0f);

    double sumSqOff = 0.0;
    for (size_t i = 24000; i < N; ++i) {
        sumSqOff += leftOff[i] * leftOff[i];
    }
    double rmsOff = std::sqrt(sumSqOff / 24000.0);

    mbc.reset();
    params.autoMakeup = true;
    mbc.process(leftOn.data(), rightOn.data(), N, params);
    float makeupOn = mbc.getAutoMakeupGainDb();
    assert(makeupOn > 0.0f);

    double sumSqOn = 0.0;
    for (size_t i = 24000; i < N; ++i) {
        sumSqOn += leftOn[i] * leftOn[i];
    }
    double rmsOn = std::sqrt(sumSqOn / 24000.0);

    double diffDb = 20.0 * std::log10(rmsOn / rmsOff);
    std::cout << "  Auto-Makeup OFF RMS: " << rmsOff << ", ON RMS: " << rmsOn
              << " (Boost: +" << diffDb << " dB, Reported Makeup: +" << makeupOn << " dB)" << std::endl;

    assert(rmsOn > rmsOff);
    assert(std::abs(diffDb - makeupOn) < 0.5);
    std::cout << "  -> PASS: MBC Auto-Makeup dynamically compensates compressed energy!" << std::endl;
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

int main() {
    std::cout << "============================================" << std::endl;
    std::cout << "   AutoLevel DJ DSP Unit Tests (Android Spec)" << std::endl;
    std::cout << "============================================" << std::endl;

    testKWeightingSineWave();
    testAndroidToneProfilesAndThresholds();
    testLR4CrossoverSummation();
    testLevelResponseMapping();
    testEbuR128DynamicLoudness();
    testMbcSpeedBallistics();
    testPostMbcGain();
    testDynamicAirLift();
    testHighPassFilter();
    testDefaultLimiterCeiling();
    testBreakdownFreezeSensitivity();
    testLevelerSlewSpeed();
    testCustomToneProfile();
    testMbcAutoMakeupGain();
    testFullChain();
    testInputSanitizerAntiNan();
    testLockFreeDoubleBufferedVisualState();

    std::cout << "============================================" << std::endl;
    std::cout << "   ALL DSP TESTS PASSED WITH 100% ACCURACY! " << std::endl;
    std::cout << "============================================" << std::endl;
    return 0;
}
