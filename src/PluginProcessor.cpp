#include "PluginProcessor.h"
#include "PluginEditor.h"

juce::AudioProcessorValueTreeState::ParameterLayout AutoLevelDJAudioProcessor::createParameterLayout() {
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ID_TARGET_LUFS, 1},
        "Target LUFS",
        juce::NormalisableRange<float>(-24.0f, -4.0f, 0.5f),
        -9.0f,
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
        0.25f,
        juce::AudioParameterFloatAttributes().withLabel("%")));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ID_COMPRESSION_AMOUNT, 1},
        "Compression",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f),
        0.50f,
        juce::AudioParameterFloatAttributes().withLabel("%")));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ID_TONE_SLOPE, 1},
        "Tone Slope",
        juce::NormalisableRange<float>(-6.0f, 0.0f, 0.1f),
        -2.0f,
        juce::AudioParameterFloatAttributes().withLabel("dB/oct")));

    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{ID_TARGET_PROFILE, 1},
        "Target Profile",
        juce::StringArray{"Pink Noise (Linear)", "Modern Mix (Contoured)"},
        1)); // Default: Modern Mix

    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{ID_MBC_SPEED, 1},
        "MBC Speed",
        juce::StringArray{"Slow", "Normal", "Fast"},
        1)); // Default: Normal (middle)

    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{ID_SUB_WEIGHT, 1},
        "Sub Weight",
        juce::StringArray{"Off", "Low", "Medium", "High"},
        0)); // Default: Off (backward compatible)

    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{ID_AIR_EXCITER, 1},
        "Air Exciter",
        juce::StringArray{"Off", "Low", "Medium", "High"},
        0)); // Default: Off (backward compatible)

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
    m_compressionAmountParam = m_apvts.getRawParameterValue(ID_COMPRESSION_AMOUNT);
    m_toneSlopeParam = m_apvts.getRawParameterValue(ID_TONE_SLOPE);
    m_targetProfileParam = m_apvts.getRawParameterValue(ID_TARGET_PROFILE);
    m_mbcSpeedParam = m_apvts.getRawParameterValue(ID_MBC_SPEED);
    m_subWeightParam = m_apvts.getRawParameterValue(ID_SUB_WEIGHT);
    m_airExciterParam = m_apvts.getRawParameterValue(ID_AIR_EXCITER);
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
    m_engine.prepare(sampleRate);
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
    params.targetLUFS = m_targetLufsParam ? m_targetLufsParam->load() : -9.0f;
    params.maxBoostDb = m_maxBoostParam ? m_maxBoostParam->load() : 12.0f;
    params.maxCutDb = m_maxCutParam ? m_maxCutParam->load() : 12.0f;
    params.levelResponse = m_levelResponseParam ? m_levelResponseParam->load() : 0.25f;
    params.compressionAmount = m_compressionAmountParam ? m_compressionAmountParam->load() : 0.5f;
    params.toneSlopeDbPerOctave = m_toneSlopeParam ? m_toneSlopeParam->load() : -2.0f;

    int profileIdx = m_targetProfileParam ? juce::roundToInt(m_targetProfileParam->load()) : 1;
    params.targetProfile = (profileIdx == 0)
        ? autolevel::dsp::TargetProfile::PINK_NOISE
        : autolevel::dsp::TargetProfile::MODERN_MIX;

    int speedIdx = m_mbcSpeedParam ? juce::roundToInt(m_mbcSpeedParam->load()) : 1;
    if (speedIdx == 0) params.mbcSpeed = autolevel::dsp::MBCSpeed::SLOW;
    else if (speedIdx == 2) params.mbcSpeed = autolevel::dsp::MBCSpeed::FAST;
    else params.mbcSpeed = autolevel::dsp::MBCSpeed::NORMAL;

    int subWeightIdx = m_subWeightParam ? juce::roundToInt(m_subWeightParam->load()) : 0;
    if (subWeightIdx == 1) params.subWeight = autolevel::dsp::SubWeight::LOW;
    else if (subWeightIdx == 2) params.subWeight = autolevel::dsp::SubWeight::MED;
    else if (subWeightIdx == 3) params.subWeight = autolevel::dsp::SubWeight::HIGH;
    else params.subWeight = autolevel::dsp::SubWeight::OFF;

    int airExciterIdx = m_airExciterParam ? juce::roundToInt(m_airExciterParam->load()) : 0;
    if (airExciterIdx == 1) params.airWeight = autolevel::dsp::AirWeight::LOW;
    else if (airExciterIdx == 2) params.airWeight = autolevel::dsp::AirWeight::MED;
    else if (airExciterIdx == 3) params.airWeight = autolevel::dsp::AirWeight::HIGH;
    else params.airWeight = autolevel::dsp::AirWeight::OFF;

    params.postMbcGainDb = m_postMbcGainParam ? m_postMbcGainParam->load() : 0.0f;
    params.hpfCutoffHz = m_hpfFreqParam ? m_hpfFreqParam->load() : 30.0f;
    params.hpfEnabled = (params.hpfCutoffHz >= 20.0f);
    params.ceilingDb = m_ceilingDbParam ? m_ceilingDbParam->load() : -0.3f;
    params.freezeBreakdowns = m_freezeBreakdownsParam ? (m_freezeBreakdownsParam->load() > 0.5f) : true;
    params.bypass = m_bypassParam ? (m_bypassParam->load() > 0.5f) : false;

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
