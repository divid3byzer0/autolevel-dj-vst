#include "PluginProcessor.h"
#include "PluginEditor.h"

juce::AudioProcessorValueTreeState::ParameterLayout AutoLevelDJAudioProcessor::createParameterLayout() {
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ID_TARGET_LUFS, 1},
        "Target LUFS",
        juce::NormalisableRange<float>(-24.0f, -4.0f, 0.5f),
        -14.0f,
        juce::AudioParameterFloatAttributes().withLabel("LUFS")));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ID_MAX_BOOST, 1},
        "Max Boost",
        juce::NormalisableRange<float>(0.0f, 18.0f, 0.5f),
        12.0f,
        juce::AudioParameterFloatAttributes().withLabel("dB")));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ID_MAX_CUT, 1},
        "Max Cut",
        juce::NormalisableRange<float>(0.0f, 18.0f, 0.5f),
        12.0f,
        juce::AudioParameterFloatAttributes().withLabel("dB")));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ID_LEVEL_RESPONSE, 1},
        "Level Response",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f),
        0.85f,
        juce::AudioParameterFloatAttributes().withLabel("%")));

    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{ID_SLEW_SPEED, 1},
        "Slew Speed",
        juce::StringArray{"Slow", "Normal", "Fast"},
        1)); // Default: Normal (0.75 dB/s up / 1.5 dB/s down, matching the original fixed behavior)

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ID_COMPRESSION_AMOUNT, 1},
        "Compression",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f),
        0.50f,
        juce::AudioParameterFloatAttributes().withLabel("%")));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ID_TONE_SLOPE, 1},
        "Tone Slope",
        juce::NormalisableRange<float>(-3.0f, 0.0f, 0.1f),
        -1.5f,
        juce::AudioParameterFloatAttributes().withLabel("dB/oct")));

    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{ID_TARGET_PROFILE, 1},
        "Target Profile",
        juce::StringArray{"Pink Noise (Linear)", "Modern Mix (Contoured)"},
        1)); // Default: Modern Mix

    // MBC ballistics. Log-skewed with the old "Normal" preset (15 ms / 200 ms) at the centre of
    // travel; the Sub band runs at twice whatever is set here (its release capped at 5000 ms).
    {
        using Mbc = autolevel::dsp::MultibandCompressor;
        juce::NormalisableRange<float> attackRange(Mbc::MIN_ATTACK_MS, Mbc::MAX_ATTACK_MS, 0.1f);
        attackRange.setSkewForCentre(15.0f);
        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID{ID_MBC_ATTACK, 1},
            "MBC Attack",
            attackRange,
            15.0f,
            juce::AudioParameterFloatAttributes()
                .withLabel("ms")
                .withStringFromValueFunction([](float v, int) { return juce::String(v, 1) + " ms"; })));

        juce::NormalisableRange<float> releaseRange(Mbc::MIN_RELEASE_MS, Mbc::MAX_RELEASE_MS, 1.0f);
        releaseRange.setSkewForCentre(200.0f);
        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID{ID_MBC_RELEASE, 1},
            "MBC Release",
            releaseRange,
            200.0f,
            juce::AudioParameterFloatAttributes()
                .withLabel("ms")
                .withStringFromValueFunction([](float v, int) { return juce::String(juce::roundToInt(v)) + " ms"; })));
    }

    // MBC release mode. Auto = program-dependent: short hits recover at the Release time,
    // sustained compression up to 10x slower (capped at 5000 ms).
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{ID_MBC_RELEASE_MODE, 1},
        "MBC Release Mode",
        juce::StringArray{"Manual", "Auto"},
        0)); // Default: Manual (the long-standing behaviour)

    // Level detector for the MBC: 0 = pure peak, 1 = pure RMS, in between blends the two.
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ID_MBC_DETECTOR, 1},
        "MBC Detector",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f),
        0.0f, // Default: Peak (the long-standing behaviour)
        juce::AudioParameterFloatAttributes()
            .withStringFromValueFunction([](float v, int) {
                if (v <= 0.0f) return juce::String("Peak");
                if (v >= 1.0f) return juce::String("RMS");
                return juce::String(juce::roundToInt(v * 100.0f)) + "% RMS";
            })
            .withValueFromStringFunction([](const juce::String& text) {
                auto t = text.trim().toLowerCase();
                if (t.startsWith("peak")) return 0.0f;
                if (t == "rms") return 1.0f;
                float v = t.getFloatValue();          // "30", "30%", "30% rms"
                return juce::jlimit(0.0f, 1.0f, v / 100.0f);
            })));

    // Band EQ: one gain per MBC band, and where it sits relative to the MBC (always after the AGC).
    {
        const char* bandNames[autolevel::dsp::Bands::COUNT] = {
            "EQ Sub", "EQ Bass", "EQ Low-Mid", "EQ High-Mid", "EQ Presence", "EQ Air"
        };
        for (size_t b = 0; b < autolevel::dsp::Bands::COUNT; ++b) {
            params.push_back(std::make_unique<juce::AudioParameterFloat>(
                juce::ParameterID{ID_EQ_BANDS[b], 1},
                bandNames[b],
                juce::NormalisableRange<float>(-autolevel::dsp::BandEQ::MAX_GAIN_DB,
                                               autolevel::dsp::BandEQ::MAX_GAIN_DB, 0.1f),
                0.0f,
                juce::AudioParameterFloatAttributes()
                    .withLabel("dB")
                    .withStringFromValueFunction([](float v, int) {
                        return (v > 0.04f ? juce::String("+") : juce::String()) + juce::String(v, 1);
                    })));
        }
    }

    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{ID_EQ_POSITION, 1},
        "EQ Position",
        juce::StringArray{"Before MBC", "After MBC"},
        1)); // Default: After MBC

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ID_POST_MBC_GAIN, 1},
        "Post Gain",
        juce::NormalisableRange<float>(-12.0f, 12.0f, 0.1f),
        0.0f,
        juce::AudioParameterFloatAttributes().withLabel("dB")));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ID_HPF_FREQ, 1},
        "Low Cut",
        juce::NormalisableRange<float>(20.0f, 50.0f, 0.5f),
        30.0f, // Default: 30 Hz (24 dB/oct Butterworth)
        juce::AudioParameterFloatAttributes().withLabel("Hz")));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ID_CEILING_DB, 1},
        "Limiter Ceiling",
        juce::NormalisableRange<float>(-3.0f, 0.0f, 0.1f),
        -0.3f, // Default: -0.3 dBFS
        juce::AudioParameterFloatAttributes().withLabel("dBFS")));

    // Safety Limiter lookahead. Off is the original zero-latency limiter; 1 ms / 2 ms switch to
    // the lookahead brickwall, which does not distort when pushed, at that much latency.
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{ID_LIMITER_LOOKAHEAD, 1},
        "Limiter Lookahead",
        juce::StringArray{"Off", "1 ms", "2 ms"},
        0)); // Default: Off (zero latency, as before)

    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{ID_FREEZE_BREAKDOWNS, 1},
        "Freeze Breakdowns",
        true));

    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{ID_BYPASS, 1},
        "Bypass",
        false));

    return { params.begin(), params.end() };
}

AutoLevelDJAudioProcessor::AutoLevelDJAudioProcessor()
    : AudioProcessor(BusesProperties()
                     .withInput("Input", juce::AudioChannelSet::stereo(), true)
                     .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      m_apvts(*this, nullptr, "Parameters", createParameterLayout())
{
    m_targetLufsParam = m_apvts.getRawParameterValue(ID_TARGET_LUFS);
    m_maxBoostParam = m_apvts.getRawParameterValue(ID_MAX_BOOST);
    m_maxCutParam = m_apvts.getRawParameterValue(ID_MAX_CUT);
    m_levelResponseParam = m_apvts.getRawParameterValue(ID_LEVEL_RESPONSE);
    m_slewSpeedParam = m_apvts.getRawParameterValue(ID_SLEW_SPEED);
    m_compressionAmountParam = m_apvts.getRawParameterValue(ID_COMPRESSION_AMOUNT);
    m_toneSlopeParam = m_apvts.getRawParameterValue(ID_TONE_SLOPE);
    m_targetProfileParam = m_apvts.getRawParameterValue(ID_TARGET_PROFILE);
    m_mbcAttackParam = m_apvts.getRawParameterValue(ID_MBC_ATTACK);
    m_mbcReleaseParam = m_apvts.getRawParameterValue(ID_MBC_RELEASE);
    m_mbcDetectorParam = m_apvts.getRawParameterValue(ID_MBC_DETECTOR);
    m_mbcReleaseModeParam = m_apvts.getRawParameterValue(ID_MBC_RELEASE_MODE);
    m_eqPositionParam = m_apvts.getRawParameterValue(ID_EQ_POSITION);
    m_lookaheadParam = m_apvts.getRawParameterValue(ID_LIMITER_LOOKAHEAD);
    for (size_t b = 0; b < autolevel::dsp::Bands::COUNT; ++b)
        m_eqBandParams[b] = m_apvts.getRawParameterValue(ID_EQ_BANDS[b]);
    m_postMbcGainParam = m_apvts.getRawParameterValue(ID_POST_MBC_GAIN);
    m_hpfFreqParam = m_apvts.getRawParameterValue(ID_HPF_FREQ);
    m_ceilingDbParam = m_apvts.getRawParameterValue(ID_CEILING_DB);
    m_freezeBreakdownsParam = m_apvts.getRawParameterValue(ID_FREEZE_BREAKDOWNS);
    m_bypassParam = m_apvts.getRawParameterValue(ID_BYPASS);
}

const juce::String AutoLevelDJAudioProcessor::getName() const {
    return "AutoLevel DJ";
}

bool AutoLevelDJAudioProcessor::acceptsMidi() const { return false; }
bool AutoLevelDJAudioProcessor::producesMidi() const { return false; }
bool AutoLevelDJAudioProcessor::isMidiEffect() const { return false; }
double AutoLevelDJAudioProcessor::getTailLengthSeconds() const { return 0.0; }

int AutoLevelDJAudioProcessor::getNumPrograms() { return 1; }
int AutoLevelDJAudioProcessor::getCurrentProgram() { return 0; }
void AutoLevelDJAudioProcessor::setCurrentProgram(int) {}
const juce::String AutoLevelDJAudioProcessor::getProgramName(int) { return {}; }
void AutoLevelDJAudioProcessor::changeProgramName(int, const juce::String&) {}

void AutoLevelDJAudioProcessor::prepareToPlay(double sampleRate, int) {
    m_sampleRate.store(sampleRate);
    auto lookahead = currentLookahead();
    m_lastLookahead.store(static_cast<int>(lookahead));
    setLatencySamples(autolevel::dsp::AutoLevelEngine::latencySamples(lookahead, sampleRate));
    m_engine.prepare(sampleRate);
}

autolevel::dsp::LimiterLookahead AutoLevelDJAudioProcessor::currentLookahead() const noexcept {
    int idx = m_lookaheadParam ? juce::roundToInt(m_lookaheadParam->load()) : 0;
    if (idx == 1) return autolevel::dsp::LimiterLookahead::MS_1;
    if (idx == 2) return autolevel::dsp::LimiterLookahead::MS_2;
    return autolevel::dsp::LimiterLookahead::OFF;
}

void AutoLevelDJAudioProcessor::handleAsyncUpdate() {
    auto lookahead = static_cast<autolevel::dsp::LimiterLookahead>(m_lastLookahead.load());
    setLatencySamples(autolevel::dsp::AutoLevelEngine::latencySamples(lookahead, m_sampleRate.load()));
}

void AutoLevelDJAudioProcessor::processBlockBypassed(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) {
    // Host bypass: keep the reported latency by passing the audio through the lookahead delay.
    if (getTotalNumInputChannels() < 2) return;
    m_engine.processBypassed(buffer.getWritePointer(0), buffer.getWritePointer(1),
                             static_cast<size_t>(buffer.getNumSamples()), currentLookahead());
}

void AutoLevelDJAudioProcessor::releaseResources() {
    m_engine.reset();
}

bool AutoLevelDJAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;
    if (layouts.getMainInputChannelSet() != juce::AudioChannelSet::stereo())
        return false;
    return true;
}

void AutoLevelDJAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) {
    juce::ScopedNoDenormals noDenormals;
    auto totalNumInputChannels  = getTotalNumInputChannels();
    auto totalNumOutputChannels = getTotalNumOutputChannels();

    for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear(i, 0, buffer.getNumSamples());

    if (totalNumInputChannels < 2) return;

    autolevel::dsp::EngineParameters params;
    params.targetLUFS = m_targetLufsParam ? m_targetLufsParam->load() : -14.0f;
    params.maxBoostDb = m_maxBoostParam ? m_maxBoostParam->load() : 12.0f;
    params.maxCutDb = m_maxCutParam ? m_maxCutParam->load() : 12.0f;
    params.levelResponse = m_levelResponseParam ? m_levelResponseParam->load() : 0.85f;
    params.compressionAmount = m_compressionAmountParam ? m_compressionAmountParam->load() : 0.5f;
    params.toneSlopeDbPerOctave = m_toneSlopeParam ? m_toneSlopeParam->load() : -1.5f;

    int slewSpeedIdx = m_slewSpeedParam ? juce::roundToInt(m_slewSpeedParam->load()) : 1;
    if (slewSpeedIdx == 0) params.slewSpeed = autolevel::dsp::LevelerSpeed::SLOW;
    else if (slewSpeedIdx == 2) params.slewSpeed = autolevel::dsp::LevelerSpeed::FAST;
    else params.slewSpeed = autolevel::dsp::LevelerSpeed::NORMAL;

    int profileIdx = m_targetProfileParam ? juce::roundToInt(m_targetProfileParam->load()) : 1;
    params.targetProfile = (profileIdx == 0)
        ? autolevel::dsp::TargetProfile::PINK_NOISE
        : autolevel::dsp::TargetProfile::MODERN_MIX;

    params.mbcAttackMs = m_mbcAttackParam ? m_mbcAttackParam->load() : 15.0f;
    params.mbcReleaseMs = m_mbcReleaseParam ? m_mbcReleaseParam->load() : 200.0f;
    params.mbcDetectorRms = m_mbcDetectorParam ? m_mbcDetectorParam->load() : 0.0f;
    params.mbcAutoRelease = m_mbcReleaseModeParam ? (juce::roundToInt(m_mbcReleaseModeParam->load()) == 1) : false;

    for (size_t b = 0; b < autolevel::dsp::Bands::COUNT; ++b)
        params.eqGainsDb[b] = m_eqBandParams[b] ? m_eqBandParams[b]->load() : 0.0f;
    int eqPosIdx = m_eqPositionParam ? juce::roundToInt(m_eqPositionParam->load()) : 1;
    params.eqPosition = (eqPosIdx == 0) ? autolevel::dsp::EqPosition::PRE_MBC
                                        : autolevel::dsp::EqPosition::POST_MBC;

    params.postMbcGainDb = m_postMbcGainParam ? m_postMbcGainParam->load() : 0.0f;
    params.hpfCutoffHz = m_hpfFreqParam ? m_hpfFreqParam->load() : 30.0f;
    params.hpfEnabled = (params.hpfCutoffHz >= 20.0f);
    params.ceilingDb = m_ceilingDbParam ? m_ceilingDbParam->load() : -0.3f;
    params.freezeBreakdowns = m_freezeBreakdownsParam ? (m_freezeBreakdownsParam->load() > 0.5f) : true;
    params.bypass = m_bypassParam ? (m_bypassParam->load() > 0.5f) : false;
    params.limiterLookahead = currentLookahead();

    // A changed lookahead changes the plugin's latency; tell the host from the message thread.
    int lookaheadIdx = static_cast<int>(params.limiterLookahead);
    if (m_lastLookahead.exchange(lookaheadIdx) != lookaheadIdx)
        triggerAsyncUpdate();

    float* left = buffer.getWritePointer(0);
    float* right = buffer.getWritePointer(1);
    size_t numSamples = static_cast<size_t>(buffer.getNumSamples());

    m_engine.process(left, right, numSamples, params);
}

bool AutoLevelDJAudioProcessor::hasEditor() const { return true; }

juce::AudioProcessorEditor* AutoLevelDJAudioProcessor::createEditor() {
    return new AutoLevelDJAudioProcessorEditor(*this);
}

void AutoLevelDJAudioProcessor::getStateInformation(juce::MemoryBlock& destData) {
    auto state = m_apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);
}

void AutoLevelDJAudioProcessor::setStateInformation(const void* data, int sizeInBytes) {
    std::unique_ptr<juce::XmlElement> xmlState(getXmlFromBinary(data, sizeInBytes));
    if (xmlState != nullptr && xmlState->hasTagName(m_apvts.state.getType())) {
        m_apvts.replaceState(juce::ValueTree::fromXml(*xmlState));
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new AutoLevelDJAudioProcessor();
}
