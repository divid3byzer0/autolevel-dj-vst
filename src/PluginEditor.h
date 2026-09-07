#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include <array>

//==============================================================================
/**
 * Modern hardware/studio mastering LookAndFeel.
 * Charcoal & obsidian chassis, brushed metal dials, neon ice-cyan & amber accents.
 */
class ModernHardwareLookAndFeel : public juce::LookAndFeel_V4 {
public:
    ModernHardwareLookAndFeel();

    void drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                          float sliderPosProportional, float rotaryStartAngle,
                          float rotaryEndAngle, juce::Slider& slider) override;

    void drawToggleButton(juce::Graphics& g, juce::ToggleButton& button,
                          bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;

    void drawButtonBackground(juce::Graphics& g, juce::Button& button,
                               const juce::Colour& backgroundColour,
                               bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;

    void drawButtonText(juce::Graphics& g, juce::TextButton& button,
                        bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;
};

//==============================================================================
/**
 * Real-time Tone Curve & Crossover Spectrum Visualizer.
 * Displays crossover divisions and dynamic tone curves (Linear Pink Noise vs Sculpted Modern Mix).
 */
class ToneCurveVisualizer : public juce::Component {
public:
    ToneCurveVisualizer() = default;

    void updateCurve(autolevel::dsp::TargetProfile profile, float toneSlopeDb,
                     const std::array<float, autolevel::dsp::Bands::COUNT>& thresholds);

    void paint(juce::Graphics& g) override;

private:
    autolevel::dsp::TargetProfile m_profile = autolevel::dsp::TargetProfile::MODERN_MIX;
    float m_toneSlope = -2.0f;
    std::array<float, autolevel::dsp::Bands::COUNT> m_thresholds{};
};

//==============================================================================
/**
 * Studio-Grade 6-Band Dynamics Metering Rack with segmented LEDs and peak hold.
 */
class MultibandMeterRack : public juce::Component {
public:
    MultibandMeterRack();

    void updateMeters(const std::array<float, autolevel::dsp::Bands::COUNT>& gainReductions,
                      autolevel::dsp::TargetProfile profile);

    void paint(juce::Graphics& g) override;

private:
    std::array<float, autolevel::dsp::Bands::COUNT> m_currentGr{};
    std::array<float, autolevel::dsp::Bands::COUNT> m_peakGr{};
    std::array<int, autolevel::dsp::Bands::COUNT> m_peakHoldTimers{};
    autolevel::dsp::TargetProfile m_profile = autolevel::dsp::TargetProfile::MODERN_MIX;
};

//==============================================================================
/**
 * Master Editor for AutoLevel DJ.
 */
class AutoLevelDJAudioProcessorEditor : public juce::AudioProcessorEditor, private juce::Timer {
public:
    explicit AutoLevelDJAudioProcessorEditor(AutoLevelDJAudioProcessor&);
    ~AutoLevelDJAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void setupRotary(juce::Slider& slider, juce::Label& label, const juce::String& text,
                     const juce::String& suffix, juce::Colour accentCol);

    AutoLevelDJAudioProcessor& m_processor;
    ModernHardwareLookAndFeel m_lookAndFeel;

    // Visualizers
    ToneCurveVisualizer m_toneVisualizer;
    MultibandMeterRack m_meterRack;

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

    // Profile Switcher (Tactile Segmented Buttons synced with APVTS Choice)
    juce::ComboBox m_profileBox; // APVTS bound
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> m_profileAttachment;
    juce::TextButton m_pinkNoiseBtn{"PINK NOISE (FLAT)"};
    juce::TextButton m_modernMixBtn{"MODERN MIX (CONTOURED)"};
    juce::Label m_profileDescLabel;

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
