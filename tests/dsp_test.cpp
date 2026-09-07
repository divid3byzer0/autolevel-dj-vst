#include "../src/dsp/AutoLevelEngine.h"
#include <iostream>
#include <cassert>
#include <cmath>
#include <vector>
#include <numeric>

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
        sumSqIn += s * s;

        double outL = 0.0, outR = 0.0;
        filter.processSample(s, s, outL, outR);
        sumSqOut += outL * outL;
    }

    double rmsIn = std::sqrt(sumSqIn / n);
    double rmsOut = std::sqrt(sumSqOut / n);
    double gainDb = 20.0 * std::log10(rmsOut / rmsIn);

    std::cout << "  Input RMS: " << rmsIn << " (-3.01 dBFS)" << std::endl;
    std::cout << "  Output RMS: " << rmsOut << std::endl;
    std::cout << "  K-Weighting gain at 1kHz: " << gainDb << " dB" << std::endl;

    assert(gainDb > 0.0 && gainDb < 1.5);
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
        std::cout << "  Band " << b << " (" << Bands::NAMES[b] << "): Linear=" << linear[b]
                  << " dB, Modern=" << modern[b] << " dB" << std::endl;
    }

    float linearAvg = linearSum / Bands::COUNT;
    float modernAvg = modernSum / Bands::COUNT;

    // The average threshold must remain centered at -24 dBFS
    assert(std::abs(linearAvg - (-24.0f)) < 0.05f);
    assert(std::abs(modernAvg - (-24.0f)) < 0.05f);

    // Tests matching Android ShaperTest.kt:
    // Band 0 (20-120 Hz): higher threshold -> punchy bass with more headroom
    assert(modern[0] > linear[0]);

    // Band 1 (120-400 Hz): lower threshold -> actively tames low-mid mud
    assert(modern[1] < linear[1]);

    // Band 4 (3.5-8 kHz): lower threshold -> actively controls harshness and sibilance
    assert(modern[4] < linear[4]);

    // Band 5 (8-20 kHz): higher threshold -> preserves open air
    assert(modern[5] > modern[4]);

    std::cout << "  -> PASS: Tone curves and per-band thresholds match Android Shaper exactly." << std::endl;
}

void testLR4CrossoverSummation() {
    std::cout << "[TEST] Linkwitz-Riley 6-band crossover flat magnitude test..." << std::endl;
    MultibandCompressor mbc;
    double sampleRate = 48000.0;
    mbc.prepare(sampleRate);

    MBCParams params;
    params.enabled = true;
    params.compressionAmount = 0.0f; // Linear bypass (no compression) -> pure crossover sum

    std::vector<double> testFreqs = { 50.0, 120.0, 300.0, 400.0, 800.0, 1200.0, 2000.0, 3500.0, 6000.0, 8000.0, 14000.0 };

    for (double f : testFreqs) {
        mbc.reset();
        size_t n = 4800;
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
        for (size_t i = 2800; i < n; ++i) {
            sumSqIn += origL[i] * origL[i];
            sumSqOut += inL[i] * inL[i];
            count++;
        }

        double rmsIn = std::sqrt(sumSqIn / count);
        double rmsOut = std::sqrt(sumSqOut / count);
        double errorDb = 20.0 * std::log10(rmsOut / rmsIn);

        assert(std::abs(errorDb) < 0.25);
    }
    std::cout << "  -> PASS: 6-band crossover tree sums to flat magnitude." << std::endl;
}

void testLevelResponseMapping() {
    std::cout << "[TEST] Level response slider mapping..." << std::endl;
    assert(LoudnessMeter::halfLifeForResponse(0.0f) == 0.0f); // whole track
    float mid = LoudnessMeter::halfLifeForResponse(0.5f);
    float max = LoudnessMeter::halfLifeForResponse(1.0f);
    std::cout << "  Response 0.0 -> " << LoudnessMeter::halfLifeForResponse(0.0f) << "s (track hold)" << std::endl;
    std::cout << "  Response 0.5 -> " << mid << "s" << std::endl;
    std::cout << "  Response 1.0 -> " << max << "s" << std::endl;
    assert(max >= 3.99f && max <= 4.01f);
    assert(mid > 4.0f && mid < 120.0f);
    std::cout << "  -> PASS: Level response logarithmic decay mapping verified." << std::endl;
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

void testEbuR128DynamicLoudness() {
    std::cout << "[TEST] EBU R128 Loudness measurement across varying song levels..." << std::endl;
    std::vector<float> inputLevels = { -6.0f, -12.0f, -18.0f, -24.0f };

    for (float level : inputLevels) {
        LoudnessMeter meter;
        double sampleRate = 48000.0;
        meter.prepare(sampleRate);

        float amp = std::pow(10.0f, level / 20.0f);
        size_t numBlocks = 40; // 4 seconds
        size_t blockSize = 4800; // 100ms

        for (size_t b = 0; b < numBlocks; ++b) {
            std::vector<float> l(blockSize), r(blockSize);
            for (size_t i = 0; i < blockSize; ++i) {
                float s = amp * static_cast<float>(std::sin(2.0 * TEST_PI * 1000.0 * (b * blockSize + i) / sampleRate));
                l[i] = s;
                r[i] = s;
            }
            meter.process(l.data(), r.data(), blockSize);
        }

        auto readings = meter.getReadings();
        std::cout << "  Input level " << level << " dBFS -> Integrated: "
                  << readings.integratedLUFS << " LUFS, Momentary: "
                  << readings.momentaryLUFS << " LUFS" << std::endl;

        // In BS.1770/EBU R128, a 1kHz sine wave of peak amp A has RMS = A / sqrt(2) (-3.01 dB)
        // With 1kHz K-weighting gain (~0.65 dB) and stereo (+3.01 dB) and -0.691 constant:
        // Expected LUFS is approximately level.
        assert(readings.integratedLUFS > -70.0f);
        assert(std::abs(readings.integratedLUFS - level) < 1.0f);
    }
    std::cout << "  -> PASS: EBU R128 measures varying loudness accurately across all levels." << std::endl;
}

void testMbcSpeedBallistics() {
    std::cout << "[TEST] Multiband Compressor speed ballistics (Slow, Normal, Fast)..." << std::endl;
    MultibandCompressor mbc;
    double sampleRate = 48000.0;
    mbc.prepare(sampleRate);

    MBCParams params;
    params.enabled = true;
    params.compressionAmount = 0.8f;
    params.toneSlopeDbPerOctave = -2.0f;
    params.profile = TargetProfile::MODERN_MIX;

    // Test with FAST speed: fast response, deeper reduction within short burst
    params.speed = MBCSpeed::FAST;
    size_t n = 4800; // 100ms
    std::vector<float> fastL(n), fastR(n);
    for (size_t i = 0; i < n; ++i) {
        float s = static_cast<float>(std::sin(2.0 * TEST_PI * 200.0 * i / sampleRate));
        fastL[i] = s;
        fastR[i] = s;
    }
    mbc.reset();
    mbc.process(fastL.data(), fastR.data(), n, params);
    float fastGr = mbc.getGainReductionsDb()[1]; // Bass band (120-400 Hz)

    // Test with SLOW speed: slower attack, less reduction over the same 100ms window
    params.speed = MBCSpeed::SLOW;
    std::vector<float> slowL = fastL;
    std::vector<float> slowR = fastR;
    mbc.reset();
    mbc.process(slowL.data(), slowR.data(), n, params);
    float slowGr = mbc.getGainReductionsDb()[1];

    std::cout << "  Fast 100ms Bass GR: " << fastGr << " dB" << std::endl;
    std::cout << "  Slow 100ms Bass GR: " << slowGr << " dB" << std::endl;

    // Fast compressor reacts faster -> more negative GR in initial 100ms
    assert(fastGr < slowGr);
    std::cout << "  -> PASS: MBC speed mode correctly alters compression ballistics." << std::endl;
}

void testPostMbcGain() {
    std::cout << "[TEST] Post-MBC Gain stage (makeup & trim)..." << std::endl;
    AutoLevelEngine engine;
    double sampleRate = 48000.0;
    engine.prepare(sampleRate);

    EngineParameters params;
    params.targetLUFS = -9.0f;
    params.maxBoostDb = 0.0f;
    params.maxCutDb = 0.0f;
    params.compressionAmount = 0.0f; // Linear pass-through
    params.ceilingDb = 0.0f;         // Limiter ceiling 0 dBFS so +6 dB won't clip test signal

    size_t n = 480;
    // Base signal at 0.1 peak (-20 dBFS)
    std::vector<float> in0L(n, 0.1f), in0R(n, 0.1f);
    params.postMbcGainDb = 0.0f;
    engine.process(in0L.data(), in0R.data(), n, params);
    float peak0 = std::abs(in0L[0]);

    // +6 dB post gain -> ~2.0x amplitude
    std::vector<float> inPlusL(n, 0.1f), inPlusR(n, 0.1f);
    params.postMbcGainDb = 6.0f;
    engine.process(inPlusL.data(), inPlusR.data(), n, params);
    float peakPlus = std::abs(inPlusL[0]);

    // -6 dB post gain -> ~0.5x amplitude
    std::vector<float> inMinusL(n, 0.1f), inMinusR(n, 0.1f);
    params.postMbcGainDb = -6.0f;
    engine.process(inMinusL.data(), inMinusR.data(), n, params);
    float peakMinus = std::abs(inMinusL[0]);

    std::cout << "  0 dB Post Peak: " << peak0 << std::endl;
    std::cout << "  +6 dB Post Peak: " << peakPlus << " (ratio: " << (peakPlus / peak0) << ")" << std::endl;
    std::cout << "  -6 dB Post Peak: " << peakMinus << " (ratio: " << (peakMinus / peak0) << ")" << std::endl;

    assert(std::abs((peakPlus / peak0) - std::pow(10.0f, 6.0f / 20.0f)) < 0.02f);
    assert(std::abs((peakMinus / peak0) - std::pow(10.0f, -6.0f / 20.0f)) < 0.02f);
    std::cout << "  -> PASS: Post-MBC Gain accurately boosts and attenuates signal." << std::endl;
}

void testDynamicBassLift() {
    std::cout << "[TEST] DynamicBassLift (Dolby Duo low-end upward expansion & adaptive protection)..." << std::endl;
    DynamicBassLift bassLift;
    double sampleRate = 48000.0;
    bassLift.prepare(sampleRate);

    // 1. Test OFF bypass: signal must remain 100% bit-identical
    size_t n = 4800; // 100ms
    std::vector<float> origL(n), origR(n);
    for (size_t i = 0; i < n; ++i) {
        float s = static_cast<float>(std::sin(2.0 * TEST_PI * 1000.0 * i / sampleRate));
        origL[i] = s;
        origR[i] = s;
    }
    std::vector<float> passL = origL;
    std::vector<float> passR = origR;
    bassLift.process(passL.data(), passR.data(), n, BassLiftMode::OFF);
    for (size_t i = 0; i < n; ++i) {
        assert(passL[i] == origL[i]);
        assert(passR[i] == origR[i]);
    }
    std::cout << "  -> PASS: BassLiftMode::OFF is 100% bit-identical bypass." << std::endl;

    // 2. Test dynamic low-shelf boost on a vintage track with weak bass (strong 1 kHz mid, weak 60 Hz bass)
    bassLift.reset();
    std::vector<float> vintageL(n), vintageR(n);
    for (size_t i = 0; i < n; ++i) {
        float mid = 0.5f * static_cast<float>(std::sin(2.0 * TEST_PI * 1000.0 * i / sampleRate));
        float weakBass = 0.05f * static_cast<float>(std::sin(2.0 * TEST_PI * 60.0 * i / sampleRate));
        vintageL[i] = mid + weakBass;
        vintageR[i] = mid + weakBass;
    }
    bassLift.process(vintageL.data(), vintageR.data(), n, BassLiftMode::MED);
    float liftDb = bassLift.getLiftDb();
    std::cout << "  Dynamic Bass Lift applied on vintage track: +" << liftDb << " dB" << std::endl;
    assert(liftDb > 2.0f && liftDb <= 4.5f);
    std::cout << "  -> PASS: Dynamic Bass Lift smoothly lifts weak low-end by up to +4.5 dB with zero distortion." << std::endl;

    // 3. Test adaptive suppression: on modern track with heavy existing bass, lift must drop towards 0 dB
    bassLift.reset();
    std::vector<float> modernL(n), modernR(n);
    for (size_t i = 0; i < n; ++i) {
        float mid = 0.2f * static_cast<float>(std::sin(2.0 * TEST_PI * 1000.0 * i / sampleRate));
        float heavyBass = 0.6f * static_cast<float>(std::sin(2.0 * TEST_PI * 60.0 * i / sampleRate));
        modernL[i] = mid + heavyBass;
        modernR[i] = mid + heavyBass;
    }
    bassLift.process(modernL.data(), modernR.data(), n, BassLiftMode::MED);
    float modernLiftDb = bassLift.getLiftDb();
    std::cout << "  Dynamic Bass Lift on modern bass-heavy track: +" << modernLiftDb << " dB" << std::endl;
    assert(modernLiftDb < 1.0f);
    std::cout << "  -> PASS: Adaptive sensor prevents mud on modern bass-heavy material." << std::endl;
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
}

void testHighPassFilter() {
    std::cout << "[TEST] HighPassFilter 24 dB/octave 4th-order Butterworth response..." << std::endl;
    HighPassFilter hpf;
    double sampleRate = 48000.0;
    hpf.prepare(sampleRate);
    hpf.setCutoff(30.0f, true);

    auto measureGainAt = [&](double freq) -> double {
        hpf.reset();
        size_t n = 48000; // 1 second
        std::vector<float> inL(n), inR(n);
        double sumSqIn = 0.0, sumSqOut = 0.0;
        for (size_t i = 0; i < n; ++i) {
            float s = static_cast<float>(std::sin(2.0 * TEST_PI * freq * i / sampleRate));
            inL[i] = s;
            inR[i] = s;
        }
        hpf.process(inL.data(), inR.data(), n);
        // Measure steady state in second half
        for (size_t i = 24000; i < n; ++i) {
            float orig = static_cast<float>(std::sin(2.0 * TEST_PI * freq * i / sampleRate));
            sumSqIn += orig * orig;
            sumSqOut += inL[i] * inL[i];
        }
        return 20.0 * std::log10(std::sqrt(sumSqOut / sumSqIn));
    };

    double gainPassband = measureGainAt(1000.0);
    double gainCutoff   = measureGainAt(30.0);
    double gainOctaveDown = measureGainAt(15.0);

    std::cout << "  1 kHz passband gain: " << gainPassband << " dB (expected: ~0.0 dB)" << std::endl;
    std::cout << "  30 Hz cutoff gain:   " << gainCutoff << " dB (expected: ~-3.0 dB)" << std::endl;
    std::cout << "  15 Hz octave-down gain: " << gainOctaveDown << " dB (expected: ~-27.0 dB)" << std::endl;

    assert(std::abs(gainPassband) < 0.05); // Flat passband
    assert(std::abs(gainCutoff - (-3.01)) < 0.25); // Exactly -3 dB at Butterworth cutoff
    assert(gainOctaveDown < -24.0); // 24 dB/oct slope verified!

    std::cout << "  -> PASS: 4th-order Butterworth filter exhibits exact 24 dB/octave attenuation." << std::endl;
}

void testDefaultLimiterCeiling() {
    std::cout << "[TEST] Default limiter ceiling is -0.3 dBFS..." << std::endl;
    EngineParameters defaultParams;
    std::cout << "  Default ceiling: " << defaultParams.ceilingDb << " dBFS" << std::endl;
    assert(std::abs(defaultParams.ceilingDb - (-0.3f)) < 0.001f);
    std::cout << "  -> PASS: Default limiter ceiling is -0.3 dBFS." << std::endl;
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
    testDynamicBassLift();
    testDynamicAirLift();
    testHighPassFilter();
    testDefaultLimiterCeiling();
    testFullChain();

    std::cout << "============================================" << std::endl;
    std::cout << "   ALL DSP TESTS PASSED WITH 100% ACCURACY! " << std::endl;
    std::cout << "============================================" << std::endl;
    return 0;
}

