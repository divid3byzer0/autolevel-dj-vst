// Plugin tests: does the plugin behave exactly like autolevel-box?
//
// The reference is autolevel-core's AudioEngine driven the way the box's JACK callback drives
// it: settings applied, then process() in 256-frame blocks. The plugin must produce the same
// samples, bit for bit, whatever block sizes the host uses - only delayed by the 256 frames it
// needs to collect a box block.

#include "PluginProcessor.h"
#include "AudioEngine.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <iostream>
#include <random>
#include <vector>

using Proc = AutoLevelDJAudioProcessor;
using Engine = autolevel::AudioEngine;
using P = Engine::Param;

namespace {

void setHost(Proc& proc, P p, float hostValue) {
    auto* prm = proc.getAPVTS().getParameter(Proc::hostParamId(p));
    assert(prm != nullptr);
    prm->setValueNotifyingHost(prm->convertTo0to1(hostValue));
}

float getHost(Proc& proc, P p) {
    return proc.getAPVTS().getRawParameterValue(Proc::hostParamId(p))->load();
}

/** Program material: filtered noise with loud and quiet sections, a sub tone and bright hiss. */
void makeProgram(std::vector<float>& L, std::vector<float>& R, size_t n, double sr) {
    L.assign(n, 0.0f);
    R.assign(n, 0.0f);
    std::mt19937 rng(1234);
    std::normal_distribution<float> g(0.0f, 1.0f);
    float bl = 0.0f, br = 0.0f;
    for (size_t i = 0; i < n; ++i) {
        const double t = static_cast<double>(i) / sr;
        const float lvl = (static_cast<int>(t / 5.0) % 2) ? 0.04f : 0.35f; // 5 s loud / 5 s quiet
        bl = 0.96f * bl + 0.04f * g(rng);
        br = 0.96f * br + 0.04f * g(rng);
        const float sub = 0.3f * static_cast<float>(std::sin(2.0 * 3.14159265358979 * 55.0 * t));
        const float hiss = 0.02f * g(rng);
        L[i] = lvl * (3.0f * bl + sub) + hiss;
        R[i] = lvl * (3.0f * br + sub) - hiss;
    }
}

/** A non-default setting for every parameter, so the mapping of each one is exercised. */
void applyNonDefaults(Proc& proc, Engine& ref) {
    struct S { P p; float host; };
    const S settings[] = {
        { P::TARGET_LUFS, -11.5f }, { P::MAX_BOOST, 9.0f }, { P::MAX_CUT, 15.0f },
        { P::LEVEL_RESPONSE, 0.6f }, { P::SLEW_SPEED, 2.0f }, { P::COMPRESSION_AMOUNT, 0.8f },
        { P::TONE_SLOPE, -1.2f }, { P::MBC_ATTACK_MS, 8.0f }, { P::MBC_RELEASE_MS, 420.0f },
        { P::MBC_DETECTOR_RMS, 0.4f }, { P::MBC_AUTO_RELEASE, 1.0f }, { P::AIR_EXCITER, 2.0f },
        { P::POST_MBC_GAIN, 1.5f }, { P::HPF_FREQ, 40.0f }, { P::CEILING_DB, -1.0f },
        { P::FREEZE_BREAKDOWNS, 1.0f }, { P::BREAKDOWN_THRESHOLD_LU, 5.0f },
        { P::CUSTOM_BAND_0, 1.5f }, { P::CUSTOM_BAND_1, -2.5f }, { P::CUSTOM_BAND_2, 0.5f },
        { P::CUSTOM_BAND_3, 1.0f }, { P::CUSTOM_BAND_4, -2.0f }, { P::CUSTOM_BAND_5, 1.5f },
        { P::PRE_EQ_BAND_0, 2.0f }, { P::PRE_EQ_BAND_1, -1.0f }, { P::PRE_EQ_BAND_3, 1.5f },
        { P::PRE_EQ_BAND_5, -3.0f },
    };
    for (const auto& s : settings) {
        setHost(proc, s.p, s.host);
        ref.setParam(Engine::paramId(s.p), Proc::toEngineValue(s.p, s.host)); // the box's string path
    }
}

/**
 * Run the program through the plugin in host blocks drawn from `blockSizes` (cycled), and
 * through the reference in 256-frame blocks; check they match exactly after the 256-frame delay.
 */
void checkParity(double sr, const std::vector<int>& blockSizes, bool nonDefaults, const char* label) {
    const size_t n = static_cast<size_t>(sr * 20.0);
    std::vector<float> inL, inR;
    makeProgram(inL, inR, n, sr);

    Proc proc;
    Engine ref;
    proc.setPlayConfigDetails(2, 2, sr, 512);
    proc.prepareToPlay(sr, 512);
    ref.prepare(sr);
    if (nonDefaults) applyNonDefaults(proc, ref);

    assert(proc.getLatencySamples() == Proc::BOX_BLOCK + ref.latencyFrames());

    // Plugin
    std::vector<float> outL(n), outR(n);
    juce::MidiBuffer midi;
    size_t pos = 0, k = 0;
    while (pos < n) {
        const int blk = static_cast<int>(std::min<size_t>(static_cast<size_t>(blockSizes[k++ % blockSizes.size()]), n - pos));
        juce::AudioBuffer<float> buf(2, blk);
        buf.copyFrom(0, 0, inL.data() + pos, blk);
        buf.copyFrom(1, 0, inR.data() + pos, blk);
        proc.processBlock(buf, midi);
        std::copy(buf.getReadPointer(0), buf.getReadPointer(0) + blk, outL.begin() + static_cast<long>(pos));
        std::copy(buf.getReadPointer(1), buf.getReadPointer(1) + blk, outR.begin() + static_cast<long>(pos));
        pos += static_cast<size_t>(blk);
    }

    // Reference: the box
    std::vector<float> refL = inL, refR = inR;
    for (size_t i = 0; i + Proc::BOX_BLOCK <= n; i += Proc::BOX_BLOCK)
        ref.process(&refL[i], &refR[i], &refL[i], &refR[i], Proc::BOX_BLOCK);

    const size_t d = Proc::BOX_BLOCK;
    for (size_t i = 0; i < d; ++i) assert(outL[i] == 0.0f && outR[i] == 0.0f);
    size_t mismatches = 0;
    double peak = 0.0;
    const size_t compared = ((n - d) / Proc::BOX_BLOCK) * Proc::BOX_BLOCK;
    for (size_t i = 0; i < compared; ++i) {
        if (outL[i + d] != refL[i] || outR[i + d] != refR[i]) ++mismatches;
        peak = std::max(peak, static_cast<double>(std::abs(refL[i])));
    }
    std::cout << "  " << label << " @ " << sr << " Hz: " << compared << " frames, "
              << mismatches << " mismatching (output peak " << 20.0 * std::log10(peak + 1e-12) << " dBFS)" << std::endl;
    assert(peak > 0.05);   // the program actually produced output
    assert(mismatches == 0);
}

void testDefaultsMatchBox() {
    std::cout << "[TEST] Every plugin default equals the box default..." << std::endl;
    Proc proc;
    Engine box;
    for (int i = 0; i < Engine::PARAM_COUNT; ++i) {
        const auto p = static_cast<P>(i);
        const float host = getHost(proc, p);
        const float engine = Proc::toEngineValue(p, host);
        if (engine != box.getParam(p)) {
            std::cout << "  " << Engine::paramId(p) << ": plugin " << engine << " vs box " << box.getParam(p) << std::endl;
            assert(false);
        }
    }
    std::cout << "  -> PASS: " << Engine::PARAM_COUNT << " parameters" << std::endl;
}

void testRangesMatchBox() {
    std::cout << "[TEST] Every plugin range end is a value the box accepts unchanged..." << std::endl;
    Proc proc;
    for (int i = 0; i < Engine::PARAM_COUNT; ++i) {
        const auto p = static_cast<P>(i);
        auto* prm = dynamic_cast<juce::RangedAudioParameter*>(proc.getAPVTS().getParameter(Proc::hostParamId(p)));
        assert(prm != nullptr);
        for (float norm : { 0.0f, 1.0f }) {
            const float engine = Proc::toEngineValue(p, prm->convertFrom0to1(norm));
            Engine box;
            box.setParam(p, engine);
            if (box.getParam(p) != engine) {
                std::cout << "  " << Engine::paramId(p) << " end " << engine << " clamps to " << box.getParam(p) << std::endl;
                assert(false);
            }
        }
    }
    // Low Cut: its lowest host value is the box's "Off" (hpf_freq 0), 20 Hz is a real 20 Hz.
    assert(Proc::toEngineValue(P::HPF_FREQ, Proc::HPF_OFF_HOST_VALUE) == 0.0f);
    assert(Proc::toEngineValue(P::HPF_FREQ, 20.0f) == 20.0f);
    assert(Proc::toHostValue(P::HPF_FREQ, 0.0f) == Proc::HPF_OFF_HOST_VALUE);
    std::cout << "  -> PASS" << std::endl;
}

void testParity() {
    std::cout << "[TEST] Plugin output is bit-identical to the box engine (256-frame blocks)..." << std::endl;
    checkParity(48000.0, { 256 }, false, "defaults, host 256");
    checkParity(48000.0, { 512 }, false, "defaults, host 512");
    checkParity(48000.0, { 1024, 64, 333, 17, 2048, 1, 255, 257 }, false, "defaults, ragged host blocks");
    checkParity(48000.0, { 32 }, true, "every setting changed, host 32");
    checkParity(44100.0, { 441, 128, 999 }, true, "every setting changed, ragged");
    checkParity(96000.0, { 480 }, true, "every setting changed, host 480");
    std::cout << "  -> PASS" << std::endl;
}

void testBypassKeepsTiming() {
    std::cout << "[TEST] Bypass passes audio untouched, delayed by the full reported latency..." << std::endl;
    const double sr = 48000.0;
    Proc proc;
    proc.setPlayConfigDetails(2, 2, sr, 512);
    proc.prepareToPlay(sr, 512);
    setHost(proc, P::BYPASS, 1.0f);
    const int lat = proc.getLatencySamples();
    const int n = 48000;
    std::vector<float> in(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) in[static_cast<size_t>(i)] = 0.5f * static_cast<float>(std::sin(i * 0.01));
    std::vector<float> out;
    juce::MidiBuffer midi;
    for (int pos = 0; pos < n; pos += 300) {
        const int blk = std::min(300, n - pos);
        juce::AudioBuffer<float> buf(2, blk);
        buf.copyFrom(0, 0, in.data() + pos, blk);
        buf.copyFrom(1, 0, in.data() + pos, blk);
        proc.processBlock(buf, midi);
        out.insert(out.end(), buf.getReadPointer(0), buf.getReadPointer(0) + blk);
    }
    for (int i = lat; i < n; ++i) assert(out[static_cast<size_t>(i)] == in[static_cast<size_t>(i - lat)]);

    // The host's own bypass path takes the same route.
    assert(proc.getBypassParameter() == proc.getAPVTS().getParameter(Proc::hostParamId(P::BYPASS)));
    std::cout << "  -> PASS: latency " << lat << " frames (" << Proc::BOX_BLOCK << " block + "
              << (lat - Proc::BOX_BLOCK) << " limiter lookahead)" << std::endl;
}

void testPresetsAreBoxFormat() {
    std::cout << "[TEST] Presets load and save in the box's JSON format..." << std::endl;
    // A preset exactly as the box writes it (AudioEngine::getStateJson), with bypass on.
    Engine box;
    box.loadStateFromJson(R"({"target_lufs":-10.0,"max_boost":6.0,"max_cut":9.0,"level_response":0.40,)"
                          R"("slew_speed":0,"compression_amount":0.70,"tone_slope":-2.0,)"
                          R"("custom_offsets":[1.5,-2.5,0.5,1.0,-2.0,1.5],"pre_eq_gains":[0.0,3.0,0.0,0.0,-1.5,0.0],)"
                          R"("mbc_attack_ms":5.0,"mbc_release_ms":600,"mbc_detector_rms":1.00,"mbc_auto_release":true,)"
                          R"("air_exciter":3,"ceiling_db":-0.5,"post_mbc_gain":-2.0,"hpf_freq":0.0,)"
                          R"("freeze_breakdowns":false,"breakdown_threshold_lu":9.0,"bypass":true})");
    const std::string json = box.getStateJson();

    Proc proc;
    proc.loadPresetJson(json);
    Engine viaPlugin;
    viaPlugin.loadStateFromJson(proc.getPresetJson());
    for (int i = 0; i < Engine::PARAM_COUNT; ++i) {
        const auto p = static_cast<P>(i);
        if (p == P::BYPASS) {
            assert(getHost(proc, p) == 0.0f); // a preset never bypasses the plugin
            continue;
        }
        assert(viaPlugin.getParam(p) == box.getParam(p));
    }
    assert(getHost(proc, P::HPF_FREQ) == Proc::HPF_OFF_HOST_VALUE); // box "Off" survives

    // A pre-2026-10-03 box preset (mbc_speed instead of attack/release) migrates as on the box.
    proc.loadPresetJson(R"({"target_lufs":-12.0,"mbc_speed":2})");
    assert(getHost(proc, P::MBC_ATTACK_MS) == 7.5f);
    assert(getHost(proc, P::MBC_RELEASE_MS) == 100.0f);
    assert(getHost(proc, P::TARGET_LUFS) == -12.0f);
    // Fields it lacks are box defaults, not leftovers from the previous preset.
    assert(getHost(proc, P::AIR_EXCITER) == 0.0f);
    assert(getHost(proc, P::CUSTOM_BAND_1) == 0.0f);

    // Reset to Default = the box's Default preset.
    proc.resetToDefaults();
    assert(proc.getPresetJson() == Engine().getStateJson());
    std::cout << "  -> PASS" << std::endl;
}

void testSessionState() {
    std::cout << "[TEST] Session state round-trips, and v2 sessions load..." << std::endl;
    Proc a;
    Engine scratch;
    applyNonDefaults(a, scratch);
    juce::MemoryBlock mb;
    a.getStateInformation(mb);
    Proc b;
    b.setStateInformation(mb.getData(), static_cast<int>(mb.getSize()));
    for (int i = 0; i < Engine::PARAM_COUNT; ++i)
        assert(getHost(a, static_cast<P>(i)) == getHost(b, static_cast<P>(i)));

    // A v2 session: old IDs carry over, removed settings are ignored, settings v2 lacked
    // come up at the box defaults (not at whatever the instance had before).
    const char* v2 = R"(<?xml version="1.0" encoding="UTF-8"?>
<Parameters>
  <PARAM id="target_lufs" value="-12.0"/>
  <PARAM id="compression_amount" value="0.7"/>
  <PARAM id="tone_slope" value="-1.5"/>
  <PARAM id="target_profile" value="1.0"/>
  <PARAM id="eq_position" value="1.0"/>
  <PARAM id="limiter_lookahead" value="0.0"/>
  <PARAM id="mbc_release" value="3000.0"/>
  <PARAM id="eq_bass" value="2.0"/>
</Parameters>)";
    auto xml = juce::parseXML(v2);
    assert(xml != nullptr);
    juce::MemoryBlock v2mb;
    juce::AudioProcessor::copyXmlToBinary(*xml, v2mb);
    Proc c;
    setHost(c, P::AIR_EXCITER, 3.0f);   // leftovers that must not survive
    setHost(c, P::CUSTOM_BAND_2, 4.0f);
    c.setStateInformation(v2mb.getData(), static_cast<int>(v2mb.getSize()));
    assert(getHost(c, P::TARGET_LUFS) == -12.0f);
    assert(std::abs(getHost(c, P::COMPRESSION_AMOUNT) - 0.7f) < 1e-6f);
    assert(std::abs(getHost(c, P::TONE_SLOPE) + 1.5f) < 1e-6f);
    assert(getHost(c, P::PRE_EQ_BAND_1) == 2.0f);
    assert(getHost(c, P::MBC_RELEASE_MS) == 1000.0f);  // v2 allowed 5000; the box stops at 1000
    assert(getHost(c, P::AIR_EXCITER) == 0.0f);
    assert(getHost(c, P::CUSTOM_BAND_2) == 0.0f);
    std::cout << "  -> PASS" << std::endl;
}

} // namespace

/** `plugin_test --snapshot out.png`: render the editor (after some audio) to an image. */
int snapshot(const char* path) {
    const double sr = 48000.0;
    Proc proc;
    proc.setPlayConfigDetails(2, 2, sr, 512);
    proc.prepareToPlay(sr, 512);
    setHost(proc, P::AIR_EXCITER, 2.0f);
    setHost(proc, P::CUSTOM_BAND_1, -2.5f);
    setHost(proc, P::CUSTOM_BAND_4, -2.0f);
    setHost(proc, P::CUSTOM_BAND_5, 1.5f);
    std::vector<float> L, R;
    makeProgram(L, R, static_cast<size_t>(sr * 12.0), sr);
    juce::MidiBuffer midi;
    for (size_t pos = 0; pos + 512 <= L.size(); pos += 512) {
        juce::AudioBuffer<float> buf(2, 512);
        buf.copyFrom(0, 0, L.data() + pos, 512);
        buf.copyFrom(1, 0, R.data() + pos, 512);
        proc.processBlock(buf, midi);
    }
    std::unique_ptr<juce::AudioProcessorEditor> ed(proc.createEditor());
    ed->setSize(840 * 2, 700 * 2);
    for (int i = 0; i < 3; ++i) { juce::Thread::sleep(40); juce::Timer::callPendingTimersSynchronously(); }
    auto img = ed->createComponentSnapshot(ed->getLocalBounds(), true, 1.0f);
    juce::File out(juce::File::getCurrentWorkingDirectory().getChildFile(path));
    out.deleteFile();
    juce::FileOutputStream os(out);
    return juce::PNGImageFormat().writeImageToStream(img, os) ? 0 : 1;
}

/**
 * `plugin_test --render in.f32 out.f32 [hostBlock]`: run interleaved stereo float32 at 48 kHz
 * through the plugin at its defaults and write what comes out, with the plugin's latency block
 * (256 frames) removed so it lines up with a box render. For comparing against the box built
 * its own way (tools/box_render in autolevel-core, or the box itself).
 */
int render(const char* inPath, const char* outPath, int hostBlock) {
    juce::MemoryBlock raw;
    if (!juce::File::getCurrentWorkingDirectory().getChildFile(inPath).loadFileAsData(raw)) return 1;
    const size_t frames = raw.getSize() / (2 * sizeof(float));
    const float* in = static_cast<const float*>(raw.getData());
    Proc proc;
    proc.setPlayConfigDetails(2, 2, 48000.0, hostBlock);
    proc.prepareToPlay(48000.0, hostBlock);
    std::vector<float> out;
    out.reserve(frames * 2);
    juce::MidiBuffer midi;
    for (size_t pos = 0; pos < frames; pos += static_cast<size_t>(hostBlock)) {
        const int blk = static_cast<int>(std::min<size_t>(static_cast<size_t>(hostBlock), frames - pos));
        juce::AudioBuffer<float> buf(2, blk);
        for (int i = 0; i < blk; ++i) {
            buf.setSample(0, i, in[(pos + static_cast<size_t>(i)) * 2]);
            buf.setSample(1, i, in[(pos + static_cast<size_t>(i)) * 2 + 1]);
        }
        proc.processBlock(buf, midi);
        for (int i = 0; i < blk; ++i) {
            out.push_back(buf.getSample(0, i));
            out.push_back(buf.getSample(1, i));
        }
    }
    const size_t skip = static_cast<size_t>(Proc::BOX_BLOCK) * 2;
    juce::File o = juce::File::getCurrentWorkingDirectory().getChildFile(outPath);
    o.deleteFile();
    return o.replaceWithData(out.data() + skip, (out.size() - skip) * sizeof(float)) ? 0 : 1;
}

int main(int argc, char** argv) {
    juce::ScopedJuceInitialiser_GUI juce;
    if ((argc == 4 || argc == 5) && std::string(argv[1]) == "--render")
        return render(argv[2], argv[3], argc == 5 ? std::atoi(argv[4]) : 512);
    if (argc == 3 && std::string(argv[1]) == "--snapshot") return snapshot(argv[2]);
    std::cout << "============================================" << std::endl;
    std::cout << "   AutoLevel DJ plugin tests (box parity)" << std::endl;
    std::cout << "============================================" << std::endl;
    testDefaultsMatchBox();
    testRangesMatchBox();
    testParity();
    testBypassKeepsTiming();
    testPresetsAreBoxFormat();
    testSessionState();
    std::cout << "============================================" << std::endl;
    std::cout << "   ALL PLUGIN TESTS PASSED" << std::endl;
    std::cout << "============================================" << std::endl;
    return 0;
}
