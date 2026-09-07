#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "dsp/AutoLevelEngine.h"

class AutoLevelDJAudioProcessor : public juce::AudioProcessor {
public:
    AutoLevelDJAudioProcessor();
    ~AutoLevelDJAudioProcessor() override = default;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;

    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

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
    void resetIntegration() noexcept { m_engine.reset(); }

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    // Parameter ID constants
    static constexpr const char* ID_TARGET_LUFS = "target_lufs";
    static constexpr const char* ID_MAX_BOOST = "max_boost";
    static constexpr const char* ID_MAX_CUT = "max_cut";
    static constexpr const char* ID_SLEW_SPEED = "slew_speed";
    static constexpr const char* ID_FREEZE_BREAKDOWNS = "freeze_breakdowns";
    static constexpr const char* ID_COMPRESSION_AMOUNT = "compression_amount";
    static constexpr const char* ID_TONE_SLOPE = "tone_slope";
    static constexpr const char* ID_CEILING_DB = "ceiling_db";
    static constexpr const char* ID_BYPASS = "bypass";

private:
    juce::AudioProcessorValueTreeState m_apvts;
    autolevel::dsp::AutoLevelEngine m_engine;

    // Parameter pointers
    std::atomic<float>* m_targetLufsParam = nullptr;
    std::atomic<float>* m_maxBoostParam = nullptr;
    std::atomic<float>* m_maxCutParam = nullptr;
    std::atomic<float>* m_slewSpeedParam = nullptr;
    std::atomic<float>* m_freezeBreakdownsParam = nullptr;
    std::atomic<float>* m_compressionAmountParam = nullptr;
    std::atomic<float>* m_toneSlopeParam = nullptr;
    std::atomic<float>* m_ceilingDbParam = nullptr;
    std::atomic<float>* m_bypassParam = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AutoLevelDJAudioProcessor)
};
