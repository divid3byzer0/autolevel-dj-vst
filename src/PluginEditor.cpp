#include "PluginEditor.h"

AutoLevelDJAudioProcessorEditor::AutoLevelDJAudioProcessorEditor(AutoLevelDJAudioProcessor& p)
    : AudioProcessorEditor(&p), m_processor(p)
{
    setSize(720, 520);

    auto setupRotary = [this](juce::Slider& slider, juce::Label& label, const juce::String& text, const juce::String& suffix) {
        slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 65, 20);
        slider.setTextValueSuffix(suffix);
        slider.setColour(juce::Slider::rotarySliderFillColourId, juce::Colour(0xff00e5ff));
        slider.setColour(juce::Slider::thumbColourId, juce::Colours::white);
        addAndMakeVisible(slider);

        label.setText(text, juce::dontSendNotification);
        label.setFont(juce::FontOptions(12.0f, juce::Font::bold));
        label.setJustificationType(juce::Justification::centred);
        label.setColour(juce::Label::textColourId, juce::Colour(0xff9aa0a6));
        addAndMakeVisible(label);
    };

    // Target LUFS slider (Horizontal Bar)
    m_targetLufsSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    m_targetLufsSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 60, 24);
    m_targetLufsSlider.setTextValueSuffix(" LUFS");
    m_targetLufsSlider.setColour(juce::Slider::trackColourId, juce::Colour(0xff00e5ff));
    addAndMakeVisible(m_targetLufsSlider);

    m_targetLufsLabel.setText("TARGET LOUDNESS", juce::dontSendNotification);
    m_targetLufsLabel.setFont(juce::FontOptions(13.0f, juce::Font::bold));
    m_targetLufsLabel.setColour(juce::Label::textColourId, juce::Colour(0xff00e5ff));
    addAndMakeVisible(m_targetLufsLabel);

    // Rotary controls
    setupRotary(m_toneSlopeSlider, m_toneSlopeLabel, "TONE TILT", " dB/oct");
    setupRotary(m_compressionSlider, m_compressionLabel, "TONE SHAPING", "");
    setupRotary(m_maxBoostSlider, m_maxBoostLabel, "MAX BOOST", " dB");
    setupRotary(m_maxCutSlider, m_maxCutLabel, "MAX CUT", " dB");
    setupRotary(m_ceilingSlider, m_ceilingLabel, "AMP CEILING", " dB");

    // Slew speed dropdown
    m_slewSpeedBox.addItem("Slow (0.5 dB/s)", 1);
    m_slewSpeedBox.addItem("Normal (0.75 dB/s)", 2);
    m_slewSpeedBox.addItem("Fast (1.5 dB/s)", 3);
    addAndMakeVisible(m_slewSpeedBox);

    // Freeze breakdowns toggle
    m_freezeBreakdownsButton.setButtonText("Breakdown Freeze");
    m_freezeBreakdownsButton.setColour(juce::ToggleButton::textColourId, juce::Colours::white);
    addAndMakeVisible(m_freezeBreakdownsButton);

    // Bypass toggle
    m_bypassButton.setButtonText("BYPASS");
    m_bypassButton.setColour(juce::ToggleButton::textColourId, juce::Colour(0xffff3366));
    addAndMakeVisible(m_bypassButton);

    // Reset button
    m_resetButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff2d3644));
    m_resetButton.setColour(juce::TextButton::textColourOffId, juce::Colour(0xff00e5ff));
    m_resetButton.onClick = [this]() { m_processor.resetIntegration(); };
    addAndMakeVisible(m_resetButton);

    // Attachments
    auto& apvts = m_processor.getAPVTS();
    m_targetLufsAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        apvts, AutoLevelDJAudioProcessor::ID_TARGET_LUFS, m_targetLufsSlider);
    m_toneSlopeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        apvts, AutoLevelDJAudioProcessor::ID_TONE_SLOPE, m_toneSlopeSlider);
    m_compressionAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        apvts, AutoLevelDJAudioProcessor::ID_COMPRESSION_AMOUNT, m_compressionSlider);
    m_maxBoostAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        apvts, AutoLevelDJAudioProcessor::ID_MAX_BOOST, m_maxBoostSlider);
    m_maxCutAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        apvts, AutoLevelDJAudioProcessor::ID_MAX_CUT, m_maxCutSlider);
    m_ceilingAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        apvts, AutoLevelDJAudioProcessor::ID_CEILING_DB, m_ceilingSlider);
    m_slewSpeedAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        apvts, AutoLevelDJAudioProcessor::ID_SLEW_SPEED, m_slewSpeedBox);
    m_freezeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        apvts, AutoLevelDJAudioProcessor::ID_FREEZE_BREAKDOWNS, m_freezeBreakdownsButton);
    m_bypassAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        apvts, AutoLevelDJAudioProcessor::ID_BYPASS, m_bypassButton);

    startTimerHz(30); // 30 FPS UI refresh
}

AutoLevelDJAudioProcessorEditor::~AutoLevelDJAudioProcessorEditor() {
    stopTimer();
}

void AutoLevelDJAudioProcessorEditor::timerCallback() {
    m_latestState = m_processor.getVisualState();
    repaint();
}

void AutoLevelDJAudioProcessorEditor::paint(juce::Graphics& g) {
    // Background
    g.fillAll(juce::Colour(0xff121418));

    // Header bar
    g.setColour(juce::Colour(0xff1a1e26));
    g.fillRect(0, 0, getWidth(), 50);

    g.setColour(juce::Colour(0xff00e5ff));
    g.setFont(juce::FontOptions(20.0f, juce::Font::bold));
    g.drawText("AUTOLEVEL DJ", 20, 10, 200, 30, juce::Justification::left);

    g.setColour(juce::Colour(0xff9aa0a6));
    g.setFont(juce::FontOptions(11.0f, juce::Font::plain));
    g.drawText("LIVE MASTER BUS INTELLIGENT LEVELLER & DYNAMIC SHAPER", 180, 16, 400, 20, juce::Justification::left);

    // Section 1: Big Loudness & Gain Panels (y: 60 to 220)
    auto drawCard = [&g](juce::Rectangle<int> bounds, const juce::String& title) {
        g.setColour(juce::Colour(0xff1a1e26));
        g.fillRoundedRectangle(bounds.toFloat(), 6.0f);
        g.setColour(juce::Colour(0xff2d3644));
        g.drawRoundedRectangle(bounds.toFloat(), 6.0f, 1.0f);

        g.setColour(juce::Colour(0xff80868b));
        g.setFont(juce::FontOptions(11.0f, juce::Font::bold));
        g.drawText(title, bounds.getX() + 10, bounds.getY() + 8, bounds.getWidth() - 20, 16, juce::Justification::left);
    };

    // Integrated Loudness Card
    juce::Rectangle<int> integCard(20, 60, 210, 130);
    drawCard(integCard, "INTEGRATED LOUDNESS");
    g.setFont(juce::FontOptions(36.0f, juce::Font::bold));
    float integ = m_latestState.loudness.integratedLUFS;
    if (integ > -70.0f) {
        g.setColour(juce::Colour(0xff00e676));
        g.drawText(juce::String(integ, 1) + " LUFS", integCard.getX(), integCard.getY() + 35, integCard.getWidth(), 45, juce::Justification::centred);
    } else {
        g.setColour(juce::Colour(0xff5f6368));
        g.drawText("---.- LUFS", integCard.getX(), integCard.getY() + 35, integCard.getWidth(), 45, juce::Justification::centred);
    }

    g.setFont(juce::FontOptions(12.0f, juce::Font::plain));
    g.setColour(juce::Colour(0xff9aa0a6));
    float mom = m_latestState.loudness.momentaryLUFS;
    juce::String momStr = (mom > -70.0f) ? (juce::String(mom, 1) + " LUFS") : "SILENT";
    g.drawText("Momentary: " + momStr, integCard.getX() + 10, integCard.getY() + 90, integCard.getWidth() - 20, 20, juce::Justification::centred);

    // Makeup Gain Slew Card
    juce::Rectangle<int> gainCard(240, 60, 210, 130);
    drawCard(gainCard, "APPLIED GAIN RIDER");
    float gain = m_latestState.appliedGainDb;
    g.setFont(juce::FontOptions(36.0f, juce::Font::bold));
    if (gain >= 0.0f) {
        g.setColour(juce::Colour(0xff00e5ff));
        g.drawText("+" + juce::String(gain, 1) + " dB", gainCard.getX(), gainCard.getY() + 35, gainCard.getWidth(), 45, juce::Justification::centred);
    } else {
        g.setColour(juce::Colour(0xffffb300));
        g.drawText(juce::String(gain, 1) + " dB", gainCard.getX(), gainCard.getY() + 35, gainCard.getWidth(), 45, juce::Justification::centred);
    }

    // Freeze badge
    if (m_latestState.isFrozen) {
        g.setColour(juce::Colour(0xffffb300));
        g.setFont(juce::FontOptions(12.0f, juce::Font::bold));
        g.drawText("[BREAKDOWN FROZEN]", gainCard.getX(), gainCard.getY() + 90, gainCard.getWidth(), 20, juce::Justification::centred);
    } else {
        g.setColour(juce::Colour(0xff9aa0a6));
        g.setFont(juce::FontOptions(12.0f, juce::Font::plain));
        g.drawText("Target: " + juce::String(m_latestState.targetGainDb, 1) + " dB", gainCard.getX(), gainCard.getY() + 90, gainCard.getWidth(), 20, juce::Justification::centred);
    }

    // MBC 6-Band Reduction Card
    juce::Rectangle<int> mbcCard(460, 60, 240, 130);
    drawCard(mbcCard, "TONE SHAPING (MBC GAIN REDUCTION)");

    const auto& bandNames = autolevel::dsp::Bands::NAMES;
    int barW = 28;
    int barSpacing = 9;
    int startX = mbcCard.getX() + 15;
    int meterTopY = mbcCard.getY() + 35;
    int meterH = 65;

    for (size_t b = 0; b < autolevel::dsp::Bands::COUNT; ++b) {
        int x = startX + static_cast<int>(b) * (barW + barSpacing);
        // Meter background
        g.setColour(juce::Colour(0xff121418));
        g.fillRect(x, meterTopY, barW, meterH);

        // Fill bar downward proportional to GR (0 to -12 dB)
        float gr = m_latestState.mbcGainReductionsDb[b];
        float norm = std::clamp(-gr / 12.0f, 0.0f, 1.0f);
        int fillH = static_cast<int>(norm * meterH);

        g.setColour(juce::Colour(0xff00e5ff));
        g.fillRect(x, meterTopY, barW, fillH);

        // Label
        g.setColour(juce::Colour(0xff80868b));
        g.setFont(juce::FontOptions(9.0f, juce::Font::plain));
        g.drawText(juce::String(bandNames[b].data()), x - 2, meterTopY + meterH + 4, barW + 4, 15, juce::Justification::centred);
    }

    // Section 2: Controls Card (y: 200 to 500)
    juce::Rectangle<int> ctrlCard(20, 200, 680, 305);
    drawCard(ctrlCard, "MASTER PROCESSOR CONTROLS");
}

void AutoLevelDJAudioProcessorEditor::resized() {
    // Header controls
    m_bypassButton.setBounds(getWidth() - 110, 12, 90, 26);
    m_resetButton.setBounds(getWidth() - 310, 12, 190, 26);

    // Target LUFS Bar in Control Card
    m_targetLufsLabel.setBounds(40, 230, 160, 24);
    m_targetLufsSlider.setBounds(200, 230, 470, 24);

    // Rotary controls row 1
    int rotY = 275;
    int rotW = 90;
    int rotH = 90;
    int stepX = 135;
    int startX = 40;

    m_toneSlopeLabel.setBounds(startX, rotY, rotW, 16);
    m_toneSlopeSlider.setBounds(startX, rotY + 16, rotW, rotH);

    m_compressionLabel.setBounds(startX + stepX, rotY, rotW, 16);
    m_compressionSlider.setBounds(startX + stepX, rotY + 16, rotW, rotH);

    m_maxBoostLabel.setBounds(startX + 2 * stepX, rotY, rotW, 16);
    m_maxBoostSlider.setBounds(startX + 2 * stepX, rotY + 16, rotW, rotH);

    m_maxCutLabel.setBounds(startX + 3 * stepX, rotY, rotW, 16);
    m_maxCutSlider.setBounds(startX + 3 * stepX, rotY + 16, rotW, rotH);

    m_ceilingLabel.setBounds(startX + 4 * stepX, rotY, rotW, 16);
    m_ceilingSlider.setBounds(startX + 4 * stepX, rotY + 16, rotW, rotH);

    // Options row at bottom
    m_slewSpeedBox.setBounds(40, 440, 160, 26);
    m_freezeBreakdownsButton.setBounds(230, 440, 180, 26);
}
