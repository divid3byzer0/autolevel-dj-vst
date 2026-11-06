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

    juce::Label* createSliderTextBox(juce::Slider& slider) override;

    void drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                          float sliderPosProportional, float rotaryStartAngle,
                          float rotaryEndAngle, juce::Slider& slider) override;

    void drawLinearSlider(juce::Graphics& g, int x, int y, int width, int height,
                          float sliderPos, float minSliderPos, float maxSliderPos,
                          juce::Slider::SliderStyle style, juce::Slider& slider) override;

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
 * Displays crossover divisions and the MBC's tonal target: the tone-slope tilt plus the
 * per-band contour offsets (the box's custom_band_N), as the box's tone-shaper canvas does.
 */
class ToneCurveVisualizer : public juce::Component {
public:
    ToneCurveVisualizer() = default;

    void updateCurve(float toneSlopeDb, const std::array<float, autolevel::dsp::Bands::COUNT>& contourDb);

    void paint(juce::Graphics& g) override;

private:
    float m_toneSlope = 0.0f;
    std::array<float, autolevel::dsp::Bands::COUNT> m_contour{};
};

//==============================================================================
/**
 * Studio-Grade 6-Band Dynamics Metering Rack with segmented LEDs and peak hold.
 */
class MultibandMeterRack : public juce::Component {
public:
    MultibandMeterRack();

    void updateMeters(const std::array<float, autolevel::dsp::Bands::COUNT>& gainReductions,
                      const std::array<float, autolevel::dsp::Bands::COUNT>& contourDb);

    /** Layout shared with the editor, which places each band's EQ fader beside its meter. */
    static constexpr float SCALE_W = 34.0f;
    static float columnWidth(float rackWidth) noexcept { return (rackWidth - SCALE_W - 16.0f) / static_cast<float>(autolevel::dsp::Bands::COUNT); }
    static float columnX(size_t band, float rackWidth) noexcept { return SCALE_W + 8.0f + static_cast<float>(band) * columnWidth(rackWidth); }

    void paint(juce::Graphics& g) override;

private:
    std::array<float, autolevel::dsp::Bands::COUNT> m_currentGr{};
    std::array<float, autolevel::dsp::Bands::COUNT> m_peakGr{};
    std::array<int, autolevel::dsp::Bands::COUNT> m_peakHoldTimers{};
    std::array<float, autolevel::dsp::Bands::COUNT> m_contour{};
};

class AutoLevelDJAudioProcessorEditor;

class MainContentComponent : public juce::Component {
public:
    explicit MainContentComponent(AutoLevelDJAudioProcessorEditor& owner) : m_owner(owner) {}
    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseMove(const juce::MouseEvent& e) override;
private:
    AutoLevelDJAudioProcessorEditor& m_owner;
};

//==============================================================================
/**
 * Master Editor for AutoLevel DJ with Proportional Aspect-Ratio Scaling.
 */
class AutoLevelDJAudioProcessorEditor : public juce::AudioProcessorEditor, private juce::Timer {
public:
    explicit AutoLevelDJAudioProcessorEditor(AutoLevelDJAudioProcessor&);
    ~AutoLevelDJAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    friend class MainContentComponent;
    void paintContent(juce::Graphics&);
    void layoutContent();
    void contentMouseDown(const juce::MouseEvent& e);
    void contentMouseMove(const juce::MouseEvent& e);

private:
    void timerCallback() override;
    void setupRotary(juce::Slider& slider, juce::Label& label, const juce::String& text,
                     const juce::String& suffix, juce::Colour accentCol);

    AutoLevelDJAudioProcessor& m_processor;
    ModernHardwareLookAndFeel m_lookAndFeel;

    // Fixed aspect ratio canvas container
    MainContentComponent m_content{*this};

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

    // Dynamic Air Lift (Off / Low / Med / High), in the Tonal Target card
    juce::ComboBox m_airLiftBox; // APVTS bound
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> m_airLiftAttachment;
    juce::Label m_airLiftLabel;
    std::array<juce::TextButton, 4> m_airLiftBtns{ juce::TextButton{"OFF"}, juce::TextButton{"LOW"},
                                                   juce::TextButton{"MED"}, juce::TextButton{"HIGH"} };
    juce::Label m_airLiftReadout;

    // MBC detector (Peak <-> RMS blend) and ballistics
    juce::Slider m_detectorSlider;
    juce::Label m_detectorLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> m_detectorAttachment;

    juce::Slider m_attackSlider;
    juce::Label m_attackLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> m_attackAttachment;

    juce::Slider m_releaseSlider;
    juce::Label m_releaseLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> m_releaseAttachment;

    // MBC release mode switch (Manual / Auto) in the Tone Shaper header
    juce::ComboBox m_releaseModeBox; // APVTS bound
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> m_releaseModeAttachment;
    juce::Label m_releaseModeLabel;
    juce::TextButton m_releaseManualBtn{"MANUAL"};
    juce::TextButton m_releaseAutoBtn{"AUTO"};

    // One vertical fader per MBC band (beside that band's meter). Like the box's tone-shaper
    // canvas, a switch picks what they edit: the MBC contour (custom_band_N) or the Pre-MBC EQ.
    std::array<juce::Slider, autolevel::dsp::Bands::COUNT> m_bandSliders;
    std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>, autolevel::dsp::Bands::COUNT> m_bandAttachments;
    juce::Label m_faderModeLabel;
    juce::TextButton m_faderContourBtn{"CONTOUR"};
    juce::TextButton m_faderEqBtn{"PRE-EQ"};
    bool m_fadersEditEq = false;
    void setFaderMode(bool editEq);

    // Post-MBC Gain stage
    juce::Slider m_postGainSlider;
    juce::Label m_postGainLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> m_postGainAttachment;

    // High-Pass Filter (Low Cut 20 - 50 Hz, 24 dB/oct)
    juce::Slider m_hpfSlider;
    juce::Label m_hpfLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> m_hpfAttachment;

    juce::Slider m_maxBoostSlider;
    juce::Label m_maxBoostLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> m_maxBoostAttachment;

    juce::Slider m_maxCutSlider;
    juce::Label m_maxCutLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> m_maxCutAttachment;

    juce::Slider m_ceilingSlider;
    juce::Label m_ceilingLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> m_ceilingAttachment;

    // AGC Slew Speed Switcher (Slow, Normal, Fast) - lives in Card 2 (AGC Gain Correction)
    juce::ComboBox m_slewSpeedBox; // APVTS bound
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> m_slewSpeedAttachment;
    juce::Label m_slewSpeedLabel;
    juce::TextButton m_slewSlowBtn{"SLOW"};
    juce::TextButton m_slewNormalBtn{"NORMAL"};
    juce::TextButton m_slewFastBtn{"FAST"};

    juce::ToggleButton m_freezeBreakdownsButton;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> m_freezeAttachment;
    // Breakdown sensitivity, the box's three settings: Sensitive 5 LU / Normal 7 LU / Deep 9 LU
    std::array<juce::TextButton, 3> m_breakdownBtns{ juce::TextButton{"5LU"}, juce::TextButton{"7LU"},
                                                     juce::TextButton{"9LU"} };

    juce::ToggleButton m_bypassButton;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> m_bypassAttachment;

    juce::TextButton m_resetButton{"RESET SET / INTEGRATION"};

    // Presets: box-format JSON files, interchangeable with autolevel-box's saved presets
    juce::TextButton m_presetsButton{"PRESETS"};
    std::unique_ptr<juce::FileChooser> m_fileChooser;
    void showPresetsMenu();
    static juce::File presetsFolder();

    autolevel::dsp::EngineVisualState m_latestState;
    float m_maxHeldLimiterGrDb = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AutoLevelDJAudioProcessorEditor)
};
