#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace {

using Param = autolevel::AudioEngine::Param;
using Engine = autolevel::AudioEngine;

/**
 * Host parameter IDs. IDs that existed in v2 are kept where the setting still means the same
 * thing, so v2 sessions carry those values over; new settings get new IDs.
 */
constexpr const char* kHostIds[Engine::PARAM_COUNT] = {
    "target_lufs", "max_boost", "max_cut", "level_response", "slew_speed",
    "compression_amount", "tone_slope", "mbc_attack", "mbc_release", "mbc_detector",
    "mbc_release_mode", "air_lift", "post_mbc_gain", "hpf_freq", "ceiling_db",
    "freeze_breakdowns", "breakdown_threshold", "bypass",
    "contour_sub", "contour_bass", "contour_lowmid", "contour_highmid", "contour_presence", "contour_air",
    "eq_sub", "eq_bass", "eq_lowmid", "eq_highmid", "eq_presence", "eq_air"
};

/** Parameters new in v3: a higher version hint keeps AU parameter order stable for old ones. */
bool isNewInV3(Param p) noexcept {
    const int i = static_cast<int>(p);
    return p == Param::AIR_EXCITER || p == Param::BREAKDOWN_THRESHOLD_LU
        || (i >= static_cast<int>(Param::CUSTOM_BAND_0) && i < static_cast<int>(Param::PRE_EQ_BAND_0));
}

juce::ParameterID pid(Param p) {
    return juce::ParameterID{ kHostIds[static_cast<int>(p)], isNewInV3(p) ? 3 : 1 };
}

/**
 * A range whose legal values are the exact floats of its decimal steps: 0.85 is 0.85f, as the
 * box parses it, not 85 * 0.01f (one ulp off). Matters because parity is checked bit for bit.
 * skewCentre > 0 skews the travel like setSkewForCentre().
 */
juce::NormalisableRange<float> decimalRange(float lo, float hi, double step, float skewCentre = 0.0f) {
    juce::NormalisableRange<float> shape(lo, hi);
    if (skewCentre > 0.0f) shape.setSkewForCentre(skewCentre);
    auto snap = [step](float start, float end, float value) {
        const double q = std::round(static_cast<double>(value) / step) * step;
        return juce::jlimit(start, end, static_cast<float>(q));
    };
    // Snapping on the way out too: JUCE stores a parameter by its normalised value, so every
    // value the host or the engine sees comes back through convertFrom0to1.
    return juce::NormalisableRange<float>(
        lo, hi,
        [shape, snap](float start, float end, float norm) { return snap(start, end, shape.convertFrom0to1(norm)); },
        [shape](float, float, float value) { return shape.convertTo0to1(value); },
        snap);
}

/** One decimal, no unit: the editor's knobs add the unit as a suffix, hosts show the label. */
juce::String oneDecimal(float v, int) { return juce::String(v, 1); }

juce::String signedDb(float v, int decimals) {
    return (v > 0.04f ? juce::String("+") : juce::String()) + juce::String(v, decimals) + " dB";
}

} // namespace

const char* AutoLevelDJAudioProcessor::hostParamId(Param p) noexcept {
    const int i = static_cast<int>(p);
    return (i >= 0 && i < Engine::PARAM_COUNT) ? kHostIds[i] : "";
}

float AutoLevelDJAudioProcessor::toHostValue(Param p, float engineValue) noexcept {
    if (p == Param::HPF_FREQ && engineValue <= 0.0f) return HPF_OFF_HOST_VALUE;
    return engineValue;
}

float AutoLevelDJAudioProcessor::toEngineValue(Param p, float hostValue) noexcept {
    if (p == Param::HPF_FREQ && hostValue < 20.0f) return 0.0f;
    return hostValue;
}

juce::AudioProcessorValueTreeState::ParameterLayout AutoLevelDJAudioProcessor::createParameterLayout() {
    // Ranges and defaults are the box's (autolevel-core AudioEngine: constructor and setParam
    // clamps). plugin_test checks every default here against a fresh AudioEngine.
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;
    using Attr = juce::AudioParameterFloatAttributes;
    using MBC = autolevel::dsp::MBCParams;

    auto addFloat = [&](Param p, const juce::String& name, juce::NormalisableRange<float> range,
                        float def, Attr attr) {
        params.push_back(std::make_unique<juce::AudioParameterFloat>(pid(p), name, range, def, attr));
    };
    auto addChoice = [&](Param p, const juce::String& name, const juce::StringArray& choices, int def) {
        params.push_back(std::make_unique<juce::AudioParameterChoice>(pid(p), name, choices, def));
    };
    auto addBool = [&](Param p, const juce::String& name, bool def) {
        params.push_back(std::make_unique<juce::AudioParameterBool>(pid(p), name, def));
    };

    // --- Leveler (AGC)
    addFloat(Param::TARGET_LUFS, "Target LUFS", decimalRange(-20.0f, -6.0f, 0.5), -14.0f, Attr().withLabel("LUFS").withStringFromValueFunction(oneDecimal));
    addFloat(Param::MAX_BOOST, "Max Boost", decimalRange(0.0f, 18.0f, 0.5), 12.0f, Attr().withLabel("dB").withStringFromValueFunction(oneDecimal));
    addFloat(Param::MAX_CUT, "Max Cut", decimalRange(0.0f, 18.0f, 0.5), 12.0f, Attr().withLabel("dB").withStringFromValueFunction(oneDecimal));
    addFloat(Param::LEVEL_RESPONSE, "Level Response", decimalRange(0.0f, 1.0f, 0.01), 0.85f,
             Attr().withStringFromValueFunction([](float v, int) { return juce::String(juce::roundToInt(v * 100.0f)) + "%"; }));
    addChoice(Param::SLEW_SPEED, "Slew Speed", { "Slow", "Normal", "Fast" }, 1);
    addBool(Param::FREEZE_BREAKDOWNS, "Freeze Breakdowns", true);
    addFloat(Param::BREAKDOWN_THRESHOLD_LU, "Breakdown Threshold", decimalRange(3.0f, 15.0f, 0.5), 7.0f,
             Attr().withLabel("LU").withStringFromValueFunction(oneDecimal));

    // --- Air Lift and Pre-MBC EQ
    addChoice(Param::AIR_EXCITER, "Air Lift", { "Off", "Low", "Med", "High" }, 0);
    {
        const char* names[6] = { "EQ Sub", "EQ Bass", "EQ Low-Mid", "EQ High-Mid", "EQ Presence", "EQ Air" };
        for (int b = 0; b < 6; ++b)
            addFloat(static_cast<Param>(static_cast<int>(Param::PRE_EQ_BAND_0) + b), names[b],
                     decimalRange(-12.0f, 12.0f, 0.1), 0.0f,
                     Attr().withStringFromValueFunction([](float v, int) { return signedDb(v, 1); }));
    }

    // --- Multiband compressor (6-band tone shaper)
    addFloat(Param::COMPRESSION_AMOUNT, "Compression", decimalRange(0.0f, 1.0f, 0.01), 0.5f,
             Attr().withStringFromValueFunction([](float v, int) { return juce::String(juce::roundToInt(v * 100.0f)) + "%"; }));
    addFloat(Param::TONE_SLOPE, "Tone Slope", decimalRange(-3.0f, 0.0f, 0.1), autolevel::dsp::Bands::DEFAULT_TONE_SLOPE,
             Attr().withLabel("dB/oct").withStringFromValueFunction(oneDecimal));
    {
        const char* names[6] = { "Contour Sub", "Contour Bass", "Contour Low-Mid", "Contour High-Mid",
                                 "Contour Presence", "Contour Air" };
        for (int b = 0; b < 6; ++b)
            addFloat(static_cast<Param>(static_cast<int>(Param::CUSTOM_BAND_0) + b), names[b],
                     decimalRange(-12.0f, 12.0f, 0.1), 0.0f,
                     Attr().withStringFromValueFunction([](float v, int) { return signedDb(v, 1); }));
    }
    {
        // Log-skewed with the default (15 ms / 200 ms) at the centre of travel, like the box's
        // log-scaled sliders. The Sub band runs at twice these.
        addFloat(Param::MBC_ATTACK_MS, "MBC Attack",
                 decimalRange(MBC::MIN_ATTACK_MS, MBC::MAX_ATTACK_MS, 0.1, MBC::DEFAULT_ATTACK_MS), MBC::DEFAULT_ATTACK_MS,
                 Attr().withLabel("ms").withStringFromValueFunction([](float v, int) { return juce::String(v, 1) + " ms"; }));
        addFloat(Param::MBC_RELEASE_MS, "MBC Release",
                 decimalRange(MBC::MIN_RELEASE_MS, MBC::MAX_RELEASE_MS, 1.0, MBC::DEFAULT_RELEASE_MS), MBC::DEFAULT_RELEASE_MS,
                 Attr().withLabel("ms").withStringFromValueFunction([](float v, int) { return juce::String(juce::roundToInt(v)) + " ms"; }));
    }
    // Kept as a Manual/Auto choice (its v2 type and ID); maps to the box's mbc_auto_release.
    addChoice(Param::MBC_AUTO_RELEASE, "MBC Release Mode", { "Manual", "Auto" }, 0);
    addFloat(Param::MBC_DETECTOR_RMS, "MBC Detector", decimalRange(0.0f, 1.0f, 0.01), 0.0f,
             Attr()
                 .withStringFromValueFunction([](float v, int) {
                     if (v <= 0.0f) return juce::String("Peak");
                     if (v >= 1.0f) return juce::String("RMS");
                     return juce::String(juce::roundToInt(v * 100.0f)) + "% RMS";
                 })
                 .withValueFromStringFunction([](const juce::String& text) {
                     auto t = text.trim().toLowerCase();
                     if (t.startsWith("peak")) return 0.0f;
                     if (t == "rms") return 1.0f;
                     return juce::jlimit(0.0f, 1.0f, t.getFloatValue() / 100.0f);
                 }));

    // --- Output
    addFloat(Param::POST_MBC_GAIN, "Post Gain", decimalRange(-24.0f, 24.0f, 0.1), 0.0f,
             Attr().withStringFromValueFunction([](float v, int) { return signedDb(v, 1); }));
    addFloat(Param::HPF_FREQ, "Low Cut", decimalRange(HPF_OFF_HOST_VALUE, 50.0f, 0.5), 30.0f,
             Attr()
                 .withStringFromValueFunction([](float v, int) {
                     return v < 20.0f ? juce::String("Off") : juce::String(v, v == std::floor(v) ? 0 : 1) + " Hz";
                 })
                 .withValueFromStringFunction([](const juce::String& text) {
                     auto t = text.trim().toLowerCase();
                     if (t.startsWith("off")) return HPF_OFF_HOST_VALUE;
                     return juce::jlimit(20.0f, 50.0f, t.getFloatValue());
                 }));
    addFloat(Param::CEILING_DB, "Limiter Ceiling", decimalRange(-3.0f, 0.0f, 0.1), -0.3f, Attr().withLabel("dBFS").withStringFromValueFunction(oneDecimal));
    addBool(Param::BYPASS, "Bypass", false);

    return { params.begin(), params.end() };
}

AutoLevelDJAudioProcessor::AutoLevelDJAudioProcessor()
    : AudioProcessor(BusesProperties()
                     .withInput("Input", juce::AudioChannelSet::stereo(), true)
                     .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      m_apvts(*this, nullptr, "Parameters", createParameterLayout())
{
    for (int i = 0; i < Engine::PARAM_COUNT; ++i) {
        m_params[static_cast<size_t>(i)] = m_apvts.getRawParameterValue(kHostIds[i]);
        jassert(m_params[static_cast<size_t>(i)] != nullptr);
    }
    m_bypassParam = m_apvts.getParameter(hostParamId(Param::BYPASS));
}

const juce::String AutoLevelDJAudioProcessor::getName() const { return "AutoLevel DJ"; }

bool AutoLevelDJAudioProcessor::acceptsMidi() const { return false; }
bool AutoLevelDJAudioProcessor::producesMidi() const { return false; }
bool AutoLevelDJAudioProcessor::isMidiEffect() const { return false; }
double AutoLevelDJAudioProcessor::getTailLengthSeconds() const { return 0.0; }

int AutoLevelDJAudioProcessor::getNumPrograms() { return 1; }
int AutoLevelDJAudioProcessor::getCurrentProgram() { return 0; }
void AutoLevelDJAudioProcessor::setCurrentProgram(int) {}
const juce::String AutoLevelDJAudioProcessor::getProgramName(int) { return {}; }
void AutoLevelDJAudioProcessor::changeProgramName(int, const juce::String&) {}

juce::AudioProcessorParameter* AutoLevelDJAudioProcessor::getBypassParameter() const {
    // The host's bypass button drives the box's own bypass, which keeps the audio running
    // through the limiter's delay line: bypassing never shifts the timing.
    return m_bypassParam;
}

void AutoLevelDJAudioProcessor::prepareToPlay(double sampleRate, int) {
    m_engine.prepare(sampleRate);
    clearFifos();
    setLatencySamples(BOX_BLOCK + m_engine.latencyFrames());
}

void AutoLevelDJAudioProcessor::releaseResources() {
    // Nothing: the box never resets its loudness history on its own, so neither does the
    // plugin when a host stops the transport. prepareToPlay() starts fresh.
}

bool AutoLevelDJAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo()
        && layouts.getMainInputChannelSet() == juce::AudioChannelSet::stereo();
}

void AutoLevelDJAudioProcessor::clearFifos() noexcept {
    for (auto& ch : m_in) ch.fill(0.0f);
    for (auto& ch : m_out) ch.fill(0.0f);
    m_fifoPos = 0;
}

void AutoLevelDJAudioProcessor::pushParamsToEngine(bool forceBypass) noexcept {
    for (int i = 0; i < Engine::PARAM_COUNT; ++i) {
        const auto p = static_cast<Param>(i);
        float v = m_params[static_cast<size_t>(i)]->load(std::memory_order_relaxed);
        if (p == Param::BYPASS && forceBypass) v = 1.0f;
        m_engine.setParam(p, toEngineValue(p, v));
    }
}

void AutoLevelDJAudioProcessor::processChunked(juce::AudioBuffer<float>& buffer, bool forceBypass) {
    juce::ScopedNoDenormals noDenormals;
    for (auto i = getTotalNumInputChannels(); i < getTotalNumOutputChannels(); ++i)
        buffer.clear(i, 0, buffer.getNumSamples());
    if (buffer.getNumChannels() < 2) return;

    float* left = buffer.getWritePointer(0);
    float* right = buffer.getWritePointer(1);
    const int n = buffer.getNumSamples();

    for (int s = 0; s < n;) {
        // Swap as much as fits before the current box block is full.
        const int take = std::min(n - s, BOX_BLOCK - m_fifoPos);
        for (int k = 0; k < take; ++k) {
            const int f = m_fifoPos + k;
            const float inL = left[s + k];
            const float inR = right[s + k];
            left[s + k] = m_out[0][static_cast<size_t>(f)];
            right[s + k] = m_out[1][static_cast<size_t>(f)];
            m_in[0][static_cast<size_t>(f)] = inL;
            m_in[1][static_cast<size_t>(f)] = inR;
        }
        m_fifoPos += take;
        s += take;

        if (m_fifoPos == BOX_BLOCK) {
            // Exactly what the box's JACK callback does once per period: pick up the current
            // settings, then run one 256-frame block.
            pushParamsToEngine(forceBypass);
            m_engine.process(m_in[0].data(), m_in[1].data(), m_out[0].data(), m_out[1].data(),
                             static_cast<size_t>(BOX_BLOCK));
            m_fifoPos = 0;
        }
    }
}

void AutoLevelDJAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) {
    processChunked(buffer, false);
}

void AutoLevelDJAudioProcessor::processBlockBypassed(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) {
    // Only reached in hosts that bypass without using getBypassParameter(). Same path, forced
    // to the box's bypass, so the latency the host compensates for stays the same.
    processChunked(buffer, true);
}

bool AutoLevelDJAudioProcessor::hasEditor() const { return true; }

juce::AudioProcessorEditor* AutoLevelDJAudioProcessor::createEditor() {
    return new AutoLevelDJAudioProcessorEditor(*this);
}

void AutoLevelDJAudioProcessor::getStateInformation(juce::MemoryBlock& destData) {
    auto state = m_apvts.copyState();
    state.setProperty("stateVersion", 3, nullptr);
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);
}

void AutoLevelDJAudioProcessor::setStateInformation(const void* data, int sizeInBytes) {
    std::unique_ptr<juce::XmlElement> xmlState(getXmlFromBinary(data, sizeInBytes));
    if (xmlState == nullptr || !xmlState->hasTagName(m_apvts.state.getType())) return;

    // replaceState() leaves parameters the saved state lacks at whatever they currently are.
    // Fill those in with the box defaults, so a v2 session gets the box's values for settings
    // v2 did not have (contour, Air Lift, breakdown threshold) rather than leftovers. Done in
    // the tree, not via setValueNotifyingHost, so restoring a session does not mark it edited.
    auto tree = juce::ValueTree::fromXml(*xmlState);
    for (auto* p : getParameters()) {
        auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(p);
        if (ranged == nullptr) continue;
        const auto id = ranged->getParameterID();
        if (tree.getChildWithProperty("id", id).isValid()) continue;
        juce::ValueTree child("PARAM");
        child.setProperty("id", id, nullptr);
        child.setProperty("value", ranged->convertFrom0to1(ranged->getDefaultValue()), nullptr);
        tree.appendChild(child, nullptr);
    }
    m_apvts.replaceState(tree);
}

std::string AutoLevelDJAudioProcessor::getPresetJson() const {
    Engine snapshot;
    for (int i = 0; i < Engine::PARAM_COUNT; ++i) {
        const auto p = static_cast<Param>(i);
        snapshot.setParam(p, toEngineValue(p, m_params[static_cast<size_t>(i)]->load()));
    }
    return snapshot.getStateJson();
}

void AutoLevelDJAudioProcessor::loadPresetJson(const std::string& json) {
    Engine parsed;             // box defaults...
    parsed.loadStateFromJson(json); // ...overwritten by whatever the preset has, via the box's loader
    for (int i = 0; i < Engine::PARAM_COUNT; ++i) {
        const auto p = static_cast<Param>(i);
        if (p == Param::BYPASS) continue; // a preset never bypasses the plugin
        if (auto* param = m_apvts.getParameter(kHostIds[i])) {
            const float host = toHostValue(p, parsed.getParam(p));
            param->setValueNotifyingHost(param->convertTo0to1(host));
        }
    }
}

void AutoLevelDJAudioProcessor::resetToDefaults() {
    for (auto* param : getParameters()) {
        if (param == m_bypassParam) continue;
        param->setValueNotifyingHost(param->getDefaultValue());
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new AutoLevelDJAudioProcessor();
}
