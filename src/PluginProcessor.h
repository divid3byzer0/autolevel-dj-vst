#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "AudioEngine.h" // autolevel-core: the same engine and parameter store the box runs
#include <array>

/**
 * AutoLevel DJ: the autolevel-box processing chain as a plugin.
 *
 * Sound parity with the box rests on three things, all here:
 *  - the engine is autolevel-core's AudioEngine, the box's own parameter store and chain;
 *  - every setting is one host parameter mapped 1:1 onto an AudioEngine::Param, with the box's
 *    ranges and defaults;
 *  - the engine always runs in BOX_BLOCK-frame blocks, the box's JACK period. The Leveler and
 *    meters update once per block, so other block sizes give a (slightly) different result.
 *    Collecting those blocks costs BOX_BLOCK frames of latency, reported to the host.
 */
class AutoLevelDJAudioProcessor : public juce::AudioProcessor {
public:
    using Param = autolevel::AudioEngine::Param;

    /** The box's JACK period (service/autolevel-box.service, jackd -p 256). */
    static constexpr int BOX_BLOCK = 256;

    AutoLevelDJAudioProcessor();
    ~AutoLevelDJAudioProcessor() override = default;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;

    using juce::AudioProcessor::processBlock;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using juce::AudioProcessor::processBlockBypassed;
    void processBlockBypassed(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorParameter* getBypassParameter() const override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    const juce::String getName() const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int index, const juce::String& newName) override;

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& getAPVTS() noexcept { return m_apvts; }
    autolevel::dsp::EngineVisualState getVisualState() const noexcept { return m_engine.getVisualState(); }
    /** The box's "Reset" button: clears loudness history and filter state, keeps settings. */
    void resetIntegration() noexcept { m_engine.reset(); }

    /** Current settings as a box preset (the exact JSON autolevel-box saves). Message thread. */
    std::string getPresetJson() const;
    /**
     * Apply a box preset. Goes through the box's own loader (clamping, legacy-field migration,
     * unknown fields ignored), then pushes the result into the host parameters so it is
     * automatable and saved with the session. Fields a preset lacks reset to the box defaults
     * first, so loading always gives the box's sound for that preset. Message thread.
     */
    void loadPresetJson(const std::string& json);
    /** Every setting back to the box's defaults (the box's "Default" preset). Message thread. */
    void resetToDefaults();

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    /** Host parameter ID for each engine parameter (AudioEngine::Param order). */
    static const char* hostParamId(Param p) noexcept;
    /** Engine value -> host parameter value, and back. Identity except Low Cut's "Off". */
    static float toHostValue(Param p, float engineValue) noexcept;
    static float toEngineValue(Param p, float hostValue) noexcept;

    /** Low Cut host value shown as "Off" (the engine's hpf_freq 0). */
    static constexpr float HPF_OFF_HOST_VALUE = 19.5f;

private:
    void processChunked(juce::AudioBuffer<float>& buffer, bool forceBypass);
    void pushParamsToEngine(bool forceBypass) noexcept;
    void clearFifos() noexcept;

    juce::AudioProcessorValueTreeState m_apvts;
    autolevel::AudioEngine m_engine;
    std::array<std::atomic<float>*, autolevel::AudioEngine::PARAM_COUNT> m_params{};
    juce::AudioProcessorParameter* m_bypassParam = nullptr;

    // BOX_BLOCK collection: input fills m_in while m_out (the previous block, processed) drains.
    std::array<std::array<float, BOX_BLOCK>, 2> m_in{};
    std::array<std::array<float, BOX_BLOCK>, 2> m_out{};
    int m_fifoPos = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AutoLevelDJAudioProcessor)
};
