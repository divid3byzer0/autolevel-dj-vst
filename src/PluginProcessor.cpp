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
        juce::NormalisableRange<float>(0.0f, 12.0f, 0.5f),
        6.0f,
        juce::AudioParameterFloatAttributes().withLabel("dB")));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ID_MAX_CUT, 1},
        "Max Cut",
        juce::NormalisableRange<float>(0.0f, 18.0f, 0.5f),
        12.0f,
        juce::AudioParameterFloatAttributes().withLabel("dB")));

    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{ID_SLEW_SPEED, 1},
        "Slew Speed",
        juce::StringArray{"Slow (0.5 dB/s)", "Normal (0.75 dB/s)", "Fast (1.5 dB/s)"},
        1));

    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{ID_FREEZE_BREAKDOWNS, 1},
        "Freeze Breakdowns",
        true));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ID_COMPRESSION_AMOUNT, 1},
        "Tone Shaping (MBC)",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f),
        0.50f,
        juce::AudioParameterFloatAttributes().withLabel("%")));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ID_TONE_SLOPE, 1},
        "Tonal Slope",
        juce::NormalisableRange<float>(-5.0f, -2.5f, 0.05f),
        -3.75f,
        juce::AudioParameterFloatAttributes().withLabel("dB/oct")));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ID_CEILING_DB, 1},
        "Ceiling (Limiter)",
        juce::NormalisableRange<float>(-2.0f, 0.0f, 0.1f),
        -0.5f,
        juce::AudioParameterFloatAttributes().withLabel("dBFS")));

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
    m_slewSpeedParam = m_apvts.getRawParameterValue(ID_SLEW_SPEED);
    m_freezeBreakdownsParam = m_apvts.getRawParameterValue(ID_FREEZE_BREAKDOWNS);
    m_compressionAmountParam = m_apvts.getRawParameterValue(ID_COMPRESSION_AMOUNT);
    m_toneSlopeParam = m_apvts.getRawParameterValue(ID_TONE_SLOPE);
    m_ceilingDbParam = m_apvts.getRawParameterValue(ID_CEILING_DB);
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
    params.maxBoostDb = m_maxBoostParam ? m_maxBoostParam->load() : 6.0f;
    params.maxCutDb = m_maxCutParam ? m_maxCutParam->load() : 12.0f;

    int speedIdx = m_slewSpeedParam ? static_cast<int>(m_slewSpeedParam->load()) : 1;
    if (speedIdx == 0) params.slewSpeedDbPerSec = 0.5f;
    else if (speedIdx == 2) params.slewSpeedDbPerSec = 1.5f;
    else params.slewSpeedDbPerSec = 0.75f;

    params.freezeBreakdowns = m_freezeBreakdownsParam ? (m_freezeBreakdownsParam->load() > 0.5f) : true;
    params.compressionAmount = m_compressionAmountParam ? m_compressionAmountParam->load() : 0.5f;
    params.toneSlopeDbPerOctave = m_toneSlopeParam ? m_toneSlopeParam->load() : -3.75f;
    params.ceilingDb = m_ceilingDbParam ? m_ceilingDbParam->load() : -0.5f;
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
