#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"

class AutoLevelDJAudioProcessorEditor : public juce::AudioProcessorEditor, private juce::Timer {
public:
    explicit AutoLevelDJAudioProcessorEditor(AutoLevelDJAudioProcessor&);
    ~AutoLevelDJAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    AutoLevelDJAudioProcessor& m_processor;

    // Sliders & Controls
    juce::Slider m_targetLufsSlider;
    juce::Label m_targetLufsLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> m_targetLufsAttachment;

    juce::Slider m_compressionSlider;
    juce::Label m_compressionLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> m_compressionAttachment;

    juce::Slider m_levelResponseSlider;
    juce::Label m_levelResponseLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> m_levelResponseAttachment;

    juce::Slider m_toneSlopeSlider;
    juce::Label m_toneSlopeLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> m_toneSlopeAttachment;

    juce::ComboBox m_profileBox;
    juce::Label m_profileLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> m_profileAttachment;

    juce::Slider m_maxBoostSlider;
    juce::Label m_maxBoostLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> m_maxBoostAttachment;

    juce::Slider m_maxCutSlider;
    juce::Label m_maxCutLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> m_maxCutAttachment;

    juce::Slider m_ceilingSlider;
    juce::Label m_ceilingLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> m_ceilingAttachment;

    juce::ToggleButton m_freezeBreakdownsButton;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> m_freezeAttachment;

    juce::ToggleButton m_bypassButton;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> m_bypassAttachment;

    juce::TextButton m_resetButton{"RESET SET / INTEGRATION"};

    autolevel::dsp::EngineVisualState m_latestState;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AutoLevelDJAudioProcessorEditor)
};
