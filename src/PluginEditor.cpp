#include "PluginEditor.h"

AutoLevelDJAudioProcessorEditor::AutoLevelDJAudioProcessorEditor(AutoLevelDJAudioProcessor& p)
    : AudioProcessorEditor(&p), m_processor(p)
{
    setSize(740, 540);

    auto setupRotary = [this](juce::Slider& slider, juce::Label& label, const juce::String& text, const juce::String& suffix) {
        slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 65, 20);
        slider.setTextValueSuffix(suffix);
        slider.setColour(juce::Slider::rotarySliderFillColourId, juce::Colour(0xff00e5ff));
        slider.setColour(juce::Slider::thumbColourId, juce::Colours::white);
        addAndMakeVisible(slider);

        label.setText(text, juce::dontSendNotification);
        label.setFont(juce::FontOptions(11.0f, juce::Font::bold));
        label.setJustificationType(juce::Justification::centred);
        label.setColour(juce::Label::textColourId, juce::Colour(0xff9aa0a6));
        addAndMakeVisible(label);
    };

    // Target LUFS slider (Horizontal Bar)
    m_targetLufsSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    m_targetLufsSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 65, 24);
    m_targetLufsSlider.setTextValueSuffix(" LUFS");
    m_targetLufsSlider.setColour(juce::Slider::trackColourId, juce::Colour(0xff00e5ff));
    addAndMakeVisible(m_targetLufsSlider);

    m_targetLufsLabel.setText("TARGET LOUDNESS", juce::dontSendNotification);
    m_targetLufsLabel.setFont(juce::FontOptions(12.0f, juce::Font::bold));
    m_targetLufsLabel.setColour(juce::Label::textColourId, juce::Colour(0xff00e5ff));
    addAndMakeVisible(m_targetLufsLabel);

    // Compression slider (Horizontal / Rotary)
    setupRotary(m_compressionSlider, m_compressionLabel, "COMPRESSION", "");
    m_compressionSlider.setTextValueSuffix("x");

    // Level Response slider
    setupRotary(m_levelResponseSlider, m_levelResponseLabel, "LEVEL RESPONSE", "");

    // Tone Slope slider
    setupRotary(m_toneSlopeSlider, m_toneSlopeLabel, "TONE SLOPE", " dB/oct");

    // Target Profile ComboBox
    m_profileBox.addItem("Pink Noise (Linear)", 1);
    m_profileBox.addItem("Modern Mix (Contoured)", 2);
    m_profileBox.setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff1d222b));
    m_profileBox.setColour(juce::ComboBox::textColourId, juce::Colour(0xff00e5ff));
    addAndMakeVisible(m_profileBox);

    m_profileLabel.setText("TONAL PROFILE", juce::dontSendNotification);
    m_profileLabel.setFont(juce::FontOptions(11.0f, juce::Font::bold));
    m_profileLabel.setColour(juce::Label::textColourId, juce::Colour(0xff9aa0a6));
    addAndMakeVisible(m_profileLabel);

    // Boost, Cut, Ceiling
    setupRotary(m_maxBoostSlider, m_maxBoostLabel, "MAX BOOST", " dB");
    setupRotary(m_maxCutSlider, m_maxCutLabel, "MAX CUT", " dB");
    setupRotary(m_ceilingSlider, m_ceilingLabel, "LIMITER CEILING", " dBFS");

    // Breakdown freeze
    m_freezeBreakdownsButton.setButtonText("Breakdown Freeze");
    m_freezeBreakdownsButton.setColour(juce::ToggleButton::textColourId, juce::Colours::white);
    addAndMakeVisible(m_freezeBreakdownsButton);

    // Bypass
    m_bypassButton.setButtonText("BYPASS");
    m_bypassButton.setColour(juce::ToggleButton::textColourId, juce::Colour(0xffff3366));
    addAndMakeVisible(m_bypassButton);

    // Reset button
    m_resetButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff2d3644));
    m_resetButton.setColour(juce::TextButton::textColourOffId, juce::Colour(0xff00e5ff));
    m_resetButton.onClick = [this]() { m_processor.resetIntegration(); };
    addAndMakeVisible(m_resetButton);

    // APVTS Attachments
    auto& apvts = m_processor.getAPVTS();
    m_targetLufsAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        apvts, AutoLevelDJAudioProcessor::ID_TARGET_LUFS, m_targetLufsSlider);
    m_compressionAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        apvts, AutoLevelDJAudioProcessor::ID_COMPRESSION_AMOUNT, m_compressionSlider);
    m_levelResponseAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        apvts, AutoLevelDJAudioProcessor::ID_LEVEL_RESPONSE, m_levelResponseSlider);
    m_toneSlopeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        apvts, AutoLevelDJAudioProcessor::ID_TONE_SLOPE, m_toneSlopeSlider);
    m_profileAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        apvts, AutoLevelDJAudioProcessor::ID_TARGET_PROFILE, m_profileBox);
    m_maxBoostAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        apvts, AutoLevelDJAudioProcessor::ID_MAX_BOOST, m_maxBoostSlider);
    m_maxCutAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        apvts, AutoLevelDJAudioProcessor::ID_MAX_CUT, m_maxCutSlider);
    m_ceilingAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        apvts, AutoLevelDJAudioProcessor::ID_CEILING_DB, m_ceilingSlider);
    m_freezeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        apvts, AutoLevelDJAudioProcessor::ID_FREEZE_BREAKDOWNS, m_freezeBreakdownsButton);
    m_bypassAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        apvts, AutoLevelDJAudioProcessor::ID_BYPASS, m_bypassButton);

    startTimerHz(30);
}

AutoLevelDJAudioProcessorEditor::~AutoLevelDJAudioProcessorEditor() {
    stopTimer();
}

void AutoLevelDJAudioProcessorEditor::timerCallback() {
    m_latestState = m_processor.getVisualState();
    repaint();
}

void AutoLevelDJAudioProcessorEditor::paint(juce::Graphics& g) {
    g.fillAll(juce::Colour(0xff121418));

    // Header
    g.setColour(juce::Colour(0xff1a1e26));
    g.fillRect(0, 0, getWidth(), 50);

    g.setColour(juce::Colour(0xff00e5ff));
    g.setFont(juce::FontOptions(20.0f, juce::Font::bold));
    g.drawText("AUTOLEVEL DJ", 20, 10, 200, 30, juce::Justification::left);

    g.setColour(juce::Colour(0xff9aa0a6));
    g.setFont(juce::FontOptions(11.0f, juce::Font::plain));
    g.drawText("CHAIN: AGC (LOUDNESS RIDER) -> MBC (TONE SHAPER) -> LIMITER", 180, 16, 450, 20, juce::Justification::left);

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
    juce::Rectangle<int> integCard(20, 60, 215, 130);
    drawCard(integCard, "PERCEIVED LOUDNESS (BS.1770)");
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

    // Applied Gain Rider Card
    juce::Rectangle<int> gainCard(245, 60, 215, 130);
    drawCard(gainCard, "AGC GAIN CORRECTION");
    float gain = m_latestState.appliedGainDb;
    g.setFont(juce::FontOptions(36.0f, juce::Font::bold));
    if (gain >= 0.0f) {
        g.setColour(juce::Colour(0xff00e5ff));
        g.drawText("+" + juce::String(gain, 1) + " dB", gainCard.getX(), gainCard.getY() + 35, gainCard.getWidth(), 45, juce::Justification::centred);
    } else {
        g.setColour(juce::Colour(0xffffb300));
        g.drawText(juce::String(gain, 1) + " dB", gainCard.getX(), gainCard.getY() + 35, gainCard.getWidth(), 45, juce::Justification::centred);
    }

    if (m_latestState.isFrozen) {
        g.setColour(juce::Colour(0xffffb300));
        g.setFont(juce::FontOptions(12.0f, juce::Font::bold));
        g.drawText("[BREAKDOWN FROZEN]", gainCard.getX(), gainCard.getY() + 90, gainCard.getWidth(), 20, juce::Justification::centred);
    } else {
        g.setColour(juce::Colour(0xff9aa0a6));
        g.setFont(juce::FontOptions(12.0f, juce::Font::plain));
        juce::String hlStr = (m_latestState.activeHalfLifeSeconds > 0.0f)
            ? ("T1/2: " + juce::String(static_cast<int>(m_latestState.activeHalfLifeSeconds)) + "s")
            : "Track Hold";
        g.drawText("Target: " + juce::String(m_latestState.targetGainDb, 1) + " dB (" + hlStr + ")", gainCard.getX(), gainCard.getY() + 90, gainCard.getWidth(), 20, juce::Justification::centred);
    }

    // 6-Band MBC Tone Shaper Card
    juce::Rectangle<int> mbcCard(470, 60, 250, 130);
    drawCard(mbcCard, "TONE SHAPER (6-BAND DYNAMICS)");

    const auto& bandNames = autolevel::dsp::Bands::NAMES;
    int barW = 30;
    int barSpacing = 9;
    int startX = mbcCard.getX() + 12;
    int meterTopY = mbcCard.getY() + 35;
    int meterH = 65;

    for (size_t b = 0; b < autolevel::dsp::Bands::COUNT; ++b) {
        int x = startX + static_cast<int>(b) * (barW + barSpacing);
        g.setColour(juce::Colour(0xff121418));
        g.fillRect(x, meterTopY, barW, meterH);

        float gr = m_latestState.mbcGainReductionsDb[b];
        float norm = std::clamp(-gr / 12.0f, 0.0f, 1.0f);
        int fillH = static_cast<int>(norm * static_cast<float>(meterH));

        g.setColour(juce::Colour(0xff00e5ff));
        g.fillRect(x, meterTopY, barW, fillH);

        g.setColour(juce::Colour(0xff80868b));
        g.setFont(juce::FontOptions(9.0f, juce::Font::plain));
        g.drawText(juce::String(bandNames[b].data()), x - 2, meterTopY + meterH + 4, barW + 4, 15, juce::Justification::centred);
    }

    // Controls Card
    juce::Rectangle<int> ctrlCard(20, 200, 700, 320);
    drawCard(ctrlCard, "MASTER PROCESSOR CONTROLS");
}

void AutoLevelDJAudioProcessorEditor::resized() {
    m_bypassButton.setBounds(getWidth() - 110, 12, 90, 26);
    m_resetButton.setBounds(getWidth() - 320, 12, 200, 26);

    // Target LUFS Bar
    m_targetLufsLabel.setBounds(40, 230, 150, 24);
    m_targetLufsSlider.setBounds(190, 230, 500, 24);

    // Primary Row: Compression, Level Response, Tone Slope, Profile
    int row1Y = 270;
    int rotW = 95;
    int rotH = 95;
    int colSpacing = 135;
    int startColX = 45;

    m_compressionLabel.setBounds(startColX, row1Y, rotW, 16);
    m_compressionSlider.setBounds(startColX, row1Y + 16, rotW, rotH);

    m_levelResponseLabel.setBounds(startColX + colSpacing, row1Y, rotW, 16);
    m_levelResponseSlider.setBounds(startColX + colSpacing, row1Y + 16, rotW, rotH);

    m_toneSlopeLabel.setBounds(startColX + 2 * colSpacing, row1Y, rotW, 16);
    m_toneSlopeSlider.setBounds(startColX + 2 * colSpacing, row1Y + 16, rotW, rotH);

    m_profileLabel.setBounds(startColX + 3 * colSpacing + 10, row1Y + 10, 160, 16);
    m_profileBox.setBounds(startColX + 3 * colSpacing + 10, row1Y + 36, 180, 28);
    m_freezeBreakdownsButton.setBounds(startColX + 3 * colSpacing + 10, row1Y + 74, 180, 24);

    // Secondary Row: Max Boost, Max Cut, Limiter Ceiling
    int row2Y = 395;

    m_maxBoostLabel.setBounds(startColX, row2Y, rotW, 16);
    m_maxBoostSlider.setBounds(startColX, row2Y + 16, rotW, rotH);

    m_maxCutLabel.setBounds(startColX + colSpacing, row2Y, rotW, 16);
    m_maxCutSlider.setBounds(startColX + colSpacing, row2Y + 16, rotW, rotH);

    m_ceilingLabel.setBounds(startColX + 2 * colSpacing, row2Y, rotW, 16);
    m_ceilingSlider.setBounds(startColX + 2 * colSpacing, row2Y + 16, rotW, rotH);
}
