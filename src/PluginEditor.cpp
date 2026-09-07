#include "PluginEditor.h"

//==============================================================================
// ModernHardwareLookAndFeel Implementation
//==============================================================================

ModernHardwareLookAndFeel::ModernHardwareLookAndFeel() {
    setColour(juce::Slider::rotarySliderFillColourId, juce::Colour(0xff00e5ff));
    setColour(juce::Slider::thumbColourId, juce::Colours::white);
    setColour(juce::Slider::textBoxTextColourId, juce::Colours::white);
    setColour(juce::Slider::textBoxBackgroundColourId, juce::Colour(0xff12161f));
    setColour(juce::Slider::textBoxOutlineColourId, juce::Colour(0xff222836));

    setColour(juce::TextButton::buttonColourId, juce::Colour(0xff181d26));
    setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff202734));
    setColour(juce::TextButton::textColourOffId, juce::Colour(0xffa6adb9));
    setColour(juce::TextButton::textColourOnId, juce::Colour(0xff00e5ff));
}

juce::Label* ModernHardwareLookAndFeel::createSliderTextBox(juce::Slider& slider) {
    auto* l = juce::LookAndFeel_V4::createSliderTextBox(slider);
    l->setFont(juce::FontOptions(11.5f, juce::Font::bold));
    return l;
}

void ModernHardwareLookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                                                 float sliderPosProportional, float rotaryStartAngle,
                                                 float rotaryEndAngle, juce::Slider& slider)
{
    float textBoxH = 22.0f;
    float diameter = std::min(static_cast<float>(width), static_cast<float>(height) - textBoxH) - 14.0f;
    if (diameter < 10.0f) return;

    float radius = diameter * 0.5f;
    float centerX = static_cast<float>(x) + static_cast<float>(width) * 0.5f;
    float centerY = static_cast<float>(y) + (static_cast<float>(height) - textBoxH) * 0.5f + 1.0f;

    float arcRadius = radius + 4.0f;

    // 1. Background arc track
    juce::Path bgArc;
    bgArc.addCentredArc(centerX, centerY, arcRadius, arcRadius, 0.0f, rotaryStartAngle, rotaryEndAngle, true);
    g.setColour(juce::Colour(0xff1b212b));
    g.strokePath(bgArc, juce::PathStrokeType(4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // 2. Glowing value arc
    float currentAngle = rotaryStartAngle + sliderPosProportional * (rotaryEndAngle - rotaryStartAngle);
    if (currentAngle > rotaryStartAngle + 0.01f) {
        juce::Path valArc;
        valArc.addCentredArc(centerX, centerY, arcRadius, arcRadius, 0.0f, rotaryStartAngle, currentAngle, true);

        juce::Colour fillCol = slider.findColour(juce::Slider::rotarySliderFillColourId);
        // Soft outer glow
        g.setColour(fillCol.withAlpha(0.25f));
        g.strokePath(valArc, juce::PathStrokeType(7.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // Core bright arc
        g.setColour(fillCol);
        g.strokePath(valArc, juce::PathStrokeType(4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // 3. Dial outer metallic ring
    juce::Rectangle<float> dialBounds(centerX - radius, centerY - radius, diameter, diameter);
    g.setColour(juce::Colour(0xff2d3646));
    g.drawEllipse(dialBounds, 1.5f);

    // 4. Dial metallic gradient body
    juce::ColourGradient grad(juce::Colour(0xff222935), centerX, centerY - radius,
                              juce::Colour(0xff12151c), centerX, centerY + radius, false);
    g.setGradientFill(grad);
    g.fillEllipse(dialBounds.reduced(1.0f));

    // 5. Inset center cap
    g.setColour(juce::Colour(0xff161a22));
    g.fillEllipse(dialBounds.reduced(radius * 0.40f));
    g.setColour(juce::Colour(0xff262e3c));
    g.drawEllipse(dialBounds.reduced(radius * 0.40f), 1.0f);

    // 6. Etched pointer needle
    juce::Path p;
    float pLen1 = radius * 0.42f;
    float pLen2 = radius * 0.88f;
    float cosA = std::sin(currentAngle);
    float sinA = -std::cos(currentAngle);

    p.startNewSubPath(centerX + pLen1 * cosA, centerY + pLen1 * sinA);
    p.lineTo(centerX + pLen2 * cosA, centerY + pLen2 * sinA);

    g.setColour(juce::Colours::white);
    g.strokePath(p, juce::PathStrokeType(2.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

void ModernHardwareLookAndFeel::drawToggleButton(juce::Graphics& g, juce::ToggleButton& button,
                                                 bool shouldDrawButtonAsHighlighted, bool)
{
    auto bounds = button.getLocalBounds().toFloat().reduced(1.0f);
    bool isOn = button.getToggleState();

    juce::Colour accentCol = button.findColour(juce::ToggleButton::textColourId);
    if (!isOn && accentCol == juce::Colours::white) {
        accentCol = juce::Colour(0xff00e5ff);
    }

    juce::Colour bg = isOn ? juce::Colour(0xff18202c) : juce::Colour(0xff12151d);
    if (shouldDrawButtonAsHighlighted) bg = bg.brighter(0.08f);

    g.setColour(bg);
    g.fillRoundedRectangle(bounds, 5.0f);

    g.setColour(isOn ? accentCol : juce::Colour(0xff252c3a));
    g.drawRoundedRectangle(bounds, 5.0f, 1.2f);

    // Glowing LED Dot
    float ledX = bounds.getX() + 14.0f;
    float ledY = bounds.getCentreY();
    float ledR = 3.5f;

    if (isOn) {
        g.setColour(accentCol.withAlpha(0.35f));
        g.fillEllipse(ledX - ledR - 2.0f, ledY - ledR - 2.0f, (ledR + 2.0f) * 2.0f, (ledR + 2.0f) * 2.0f);
        g.setColour(accentCol);
        g.fillEllipse(ledX - ledR, ledY - ledR, ledR * 2.0f, ledR * 2.0f);
    } else {
        g.setColour(juce::Colour(0xff2a3240));
        g.fillEllipse(ledX - ledR, ledY - ledR, ledR * 2.0f, ledR * 2.0f);
    }

    g.setColour(isOn ? juce::Colours::white : juce::Colour(0xff949da8));
    g.setFont(juce::FontOptions(11.0f, juce::Font::bold));
    g.drawText(button.getButtonText(), bounds.withTrimmedLeft(26.0f), juce::Justification::centredLeft);
}

void ModernHardwareLookAndFeel::drawButtonBackground(juce::Graphics& g, juce::Button& button,
                                                     const juce::Colour& backgroundColour,
                                                     bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown)
{
    auto bounds = button.getLocalBounds().toFloat().reduced(0.5f);
    bool isToggled = button.getToggleState();

    juce::Colour bg = backgroundColour;
    if (isToggled) {
        bg = juce::Colour(0xff182434);
    } else if (shouldDrawButtonAsDown) {
        bg = bg.darker(0.15f);
    } else if (shouldDrawButtonAsHighlighted) {
        bg = bg.brighter(0.12f);
    }

    g.setColour(bg);
    g.fillRoundedRectangle(bounds, 5.0f);

    juce::Colour borderCol = isToggled ? juce::Colour(0xff00e5ff) : juce::Colour(0xff2d3647);
    if (shouldDrawButtonAsHighlighted && !isToggled) borderCol = borderCol.brighter(0.2f);

    g.setColour(borderCol);
    g.drawRoundedRectangle(bounds, 5.0f, isToggled ? 1.5f : 1.0f);
}

void ModernHardwareLookAndFeel::drawButtonText(juce::Graphics& g, juce::TextButton& button,
                                               bool, bool)
{
    auto bounds = button.getLocalBounds().toFloat();
    bool isToggled = button.getToggleState();

    juce::Colour textCol = isToggled ? juce::Colour(0xff00e5ff) : button.findColour(juce::TextButton::textColourOffId);
    g.setColour(textCol);
    g.setFont(juce::FontOptions(11.0f, juce::Font::bold));
    g.drawText(button.getButtonText(), bounds, juce::Justification::centred);
}

//==============================================================================
// ToneCurveVisualizer Implementation
//==============================================================================

void ToneCurveVisualizer::updateCurve(autolevel::dsp::TargetProfile profile, float toneSlopeDb,
                                     const std::array<float, autolevel::dsp::Bands::COUNT>& thresholds)
{
    m_profile = profile;
    m_toneSlope = toneSlopeDb;
    m_thresholds = thresholds;
    repaint();
}

void ToneCurveVisualizer::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    g.setColour(juce::Colour(0xff0e1117));
    g.fillRoundedRectangle(bounds, 6.0f);
    g.setColour(juce::Colour(0xff222937));
    g.drawRoundedRectangle(bounds, 6.0f, 1.0f);

    auto plotArea = bounds.reduced(8.0f, 14.0f);
    float plotW = plotArea.getWidth();
    float plotH = plotArea.getHeight();
    float plotX = plotArea.getX();
    float plotY = plotArea.getY();

    // 0 dB centerline
    float midY = plotY + plotH * 0.5f;
    g.setColour(juce::Colour(0xff1c222e));
    g.drawHorizontalLine(static_cast<int>(midY), plotX, plotX + plotW);

    // Crossover frequencies
    const auto& crossovers = autolevel::dsp::Bands::CROSSOVERS;
    const auto& bandNames = autolevel::dsp::Bands::NAMES;

    float minLog = std::log10(20.0f);
    float maxLog = std::log10(20000.0f);

    auto freqToX = [plotX, plotW, minLog, maxLog](float freq) {
        float norm = (std::log10(freq) - minLog) / (maxLog - minLog);
        return plotX + std::clamp(norm, 0.0f, 1.0f) * plotW;
    };

    // Draw vertical crossover grid lines and band tags
    for (size_t i = 0; i < crossovers.size(); ++i) {
        float x = freqToX(crossovers[i]);
        g.setColour(juce::Colour(0xff1c2330));
        g.drawVerticalLine(static_cast<int>(x), plotY, plotY + plotH);

        g.setColour(juce::Colour(0xff7c889a));
        g.setFont(juce::FontOptions(9.5f, juce::Font::bold));
        juce::String freqStr = (crossovers[i] >= 1000.0f)
            ? (juce::String(crossovers[i] / 1000.0f, 1) + "k")
            : (juce::String(static_cast<int>(crossovers[i])));
        g.drawText(freqStr, static_cast<int>(x) - 18, static_cast<int>(plotY + plotH) - 11, 36, 11, juce::Justification::centred);
    }

    // Band names at top
    for (size_t b = 0; b < autolevel::dsp::Bands::COUNT; ++b) {
        float x1 = (b == 0) ? plotX : freqToX(crossovers[b - 1]);
        float x2 = (b == 5) ? (plotX + plotW) : freqToX(crossovers[b]);
        g.setColour(juce::Colour(0xffb0bac8));
        g.setFont(juce::FontOptions(10.0f, juce::Font::bold));
        g.drawText(juce::String(bandNames[b].data()), static_cast<int>(x1), static_cast<int>(plotY) + 1, static_cast<int>(x2 - x1), 12, juce::Justification::centred);
    }

    // Build the dynamic tone curve
    juce::Path curvePath;
    juce::Path fillPath;
    constexpr int NUM_STEPS = 64;

    const float* contour = (m_profile == autolevel::dsp::TargetProfile::MODERN_MIX)
        ? autolevel::dsp::Bands::MODERN_CONTOUR_DB.data() : nullptr;

    auto getContourOffset = [contour](float oct) -> float {
        if (!contour) return 0.0f;
        // Smooth gaussian-weighted interpolation between the 6 band center octaves
        float sumW = 0.0f;
        float sumVal = 0.0f;
        for (size_t b = 0; b < autolevel::dsp::Bands::COUNT; ++b) {
            float diff = oct - autolevel::dsp::Bands::OCTAVES[b];
            float w = std::exp(-0.5f * (diff * diff) / (0.8f * 0.8f));
            sumVal += w * contour[b];
            sumW += w;
        }
        return (sumW > 1e-4f) ? (sumVal / sumW) : 0.0f;
    };

    fillPath.startNewSubPath(plotX, midY);

    for (int step = 0; step <= NUM_STEPS; ++step) {
        float norm = static_cast<float>(step) / static_cast<float>(NUM_STEPS);
        float f = std::pow(10.0f, minLog + norm * (maxLog - minLog));
        float oct = static_cast<float>(std::log2(f));

        // Base tilt slope + dynamic contour
        float tilt = m_toneSlope * (oct - autolevel::dsp::Bands::MEAN_OCTAVE);
        float offset = getContourOffset(oct);
        float totalDb = tilt + offset;

        // Map +/- 12 dB to height
        float ny = 0.5f - (totalDb / 20.0f);
        float x = plotX + norm * plotW;
        float y = plotY + std::clamp(ny, 0.05f, 0.95f) * plotH;

        if (step == 0) {
            curvePath.startNewSubPath(x, y);
            fillPath.lineTo(x, y);
        } else {
            curvePath.lineTo(x, y);
            fillPath.lineTo(x, y);
        }

        if (step == NUM_STEPS) {
            fillPath.lineTo(x, midY);
            fillPath.closeSubPath();
        }
    }

    // Semi-transparent gradient fill
    juce::Colour accentCol = (m_profile == autolevel::dsp::TargetProfile::MODERN_MIX)
        ? juce::Colour(0xff00e5ff) : juce::Colour(0xffffb300);

    juce::ColourGradient fillGrad(accentCol.withAlpha(0.20f), plotX + plotW * 0.5f, plotY,
                                 accentCol.withAlpha(0.01f), plotX + plotW * 0.5f, midY, false);
    g.setGradientFill(fillGrad);
    g.fillPath(fillPath);

    // Glowing outline stroke
    g.setColour(accentCol.withAlpha(0.35f));
    g.strokePath(curvePath, juce::PathStrokeType(4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour(accentCol);
    g.strokePath(curvePath, juce::PathStrokeType(1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

//==============================================================================
// MultibandMeterRack Implementation
//==============================================================================

MultibandMeterRack::MultibandMeterRack() {
    m_currentGr.fill(0.0f);
    m_peakGr.fill(0.0f);
    m_peakHoldTimers.fill(0);
}

void MultibandMeterRack::updateMeters(const std::array<float, autolevel::dsp::Bands::COUNT>& gainReductions,
                                     autolevel::dsp::TargetProfile profile,
                                     autolevel::dsp::SubWeight subWeight)
{
    m_profile = profile;
    m_subWeight = subWeight;
    for (size_t b = 0; b < autolevel::dsp::Bands::COUNT; ++b) {
        float gr = gainReductions[b]; // Negative dB (0 to -12)
        m_currentGr[b] = gr;

        if (gr < m_peakGr[b]) { // Greater reduction
            m_peakGr[b] = gr;
            m_peakHoldTimers[b] = 18; // ~600ms hold at 30Hz
        } else {
            if (m_peakHoldTimers[b] > 0) {
                --m_peakHoldTimers[b];
            } else {
                m_peakGr[b] = std::min(0.0f, m_peakGr[b] + 0.35f); // Smooth decay
            }
        }
    }
    repaint();
}

void MultibandMeterRack::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    g.setColour(juce::Colour(0xff12161f));
    g.fillRoundedRectangle(bounds, 6.0f);
    g.setColour(juce::Colour(0xff222937));
    g.drawRoundedRectangle(bounds, 6.0f, 1.0f);

    const auto& bandNames = autolevel::dsp::Bands::NAMES;
    const auto& contours = autolevel::dsp::Bands::MODERN_CONTOUR_DB;

    int numBands = static_cast<int>(autolevel::dsp::Bands::COUNT);
    float scaleW = 34.0f;
    float availW = bounds.getWidth() - scaleW - 16.0f;
    float colW = availW / static_cast<float>(numBands);
    float startX = bounds.getX() + scaleW + 8.0f;

    float meterTopY = bounds.getY() + 26.0f;
    float meterH = bounds.getHeight() - 56.0f;

    // Draw dB scale ticks on left
    g.setColour(juce::Colour(0xff8a96a7));
    g.setFont(juce::FontOptions(9.5f, juce::Font::bold));
    std::array<float, 5> scaleDbs = { 0.0f, -3.0f, -6.0f, -9.0f, -12.0f };
    for (float db : scaleDbs) {
        float norm = -db / 12.0f;
        float y = meterTopY + norm * meterH;
        g.drawText(juce::String(static_cast<int>(db)) + "dB", static_cast<int>(bounds.getX() + 2), static_cast<int>(y - 6), static_cast<int>(scaleW), 12, juce::Justification::centredRight);
        g.setColour(juce::Colour(0xff1f2532));
        g.drawHorizontalLine(static_cast<int>(y), bounds.getX() + scaleW + 4, bounds.getRight() - 6);
    }

    // Draw individual band meters
    for (int b = 0; b < numBands; ++b) {
        float bx = startX + static_cast<float>(b) * colW;
        float barW = std::min(colW - 8.0f, 34.0f);
        float barX = bx + (colW - barW) * 0.5f;

        // Band Name at top (glows cyan with '+' indicator if Sub Weight is active)
        bool isSubWithWeight = (b == 0 && m_subWeight != autolevel::dsp::SubWeight::OFF);
        g.setColour(isSubWithWeight ? juce::Colour(0xff00e5ff) : juce::Colour(0xffd0d7e2));
        g.setFont(juce::FontOptions(11.0f, juce::Font::bold));
        juce::String name = isSubWithWeight ? "SUB +" : juce::String(bandNames[static_cast<size_t>(b)].data());
        g.drawText(name, static_cast<int>(bx), static_cast<int>(bounds.getY() + 5), static_cast<int>(colW), 16, juce::Justification::centred);

        // Meter Trough
        juce::Rectangle<float> trough(barX, meterTopY, barW, meterH);
        g.setColour(juce::Colour(0xff0a0c10));
        g.fillRoundedRectangle(trough, 3.0f);
        g.setColour(juce::Colour(0xff1c222e));
        g.drawRoundedRectangle(trough, 3.0f, 1.0f);

        // Segmented LED gain reduction bar (12 discrete segments)
        constexpr int NUM_SEGMENTS = 12;
        float gr = m_currentGr[static_cast<size_t>(b)];
        float normGr = std::clamp(-gr / 12.0f, 0.0f, 1.0f);
        int activeSegments = static_cast<int>(std::round(normGr * static_cast<float>(NUM_SEGMENTS)));

        float segH = (meterH - static_cast<float>(NUM_SEGMENTS - 1) * 2.0f) / static_cast<float>(NUM_SEGMENTS);

        for (int s = 0; s < NUM_SEGMENTS; ++s) {
            float sy = meterTopY + static_cast<float>(s) * (segH + 2.0f);
            juce::Rectangle<float> segRect(barX + 2.0f, sy, barW - 4.0f, segH);

            if (s < activeSegments) {
                // Color by reduction severity: 0-3 dB cyan, 3-6 dB amber, 6+ dB red
                juce::Colour segCol;
                if (s < 3)       segCol = juce::Colour(0xff00e5ff); // 0 to -3 dB
                else if (s < 6)  segCol = juce::Colour(0xffffb300); // -3 to -6 dB
                else             segCol = juce::Colour(0xffff3366); // -6 to -12 dB

                g.setColour(segCol);
                g.fillRoundedRectangle(segRect, 1.5f);
            } else {
                g.setColour(juce::Colour(0xff141820));
                g.fillRoundedRectangle(segRect, 1.5f);
            }
        }

        // Peak Hold Line
        float peakGr = m_peakGr[static_cast<size_t>(b)];
        if (peakGr < -0.2f) {
            float normPeak = std::clamp(-peakGr / 12.0f, 0.0f, 1.0f);
            float peakY = meterTopY + normPeak * (meterH - 2.0f);
            g.setColour(juce::Colour(0xffffea00));
            g.fillRect(barX + 1.0f, peakY, barW - 2.0f, 2.0f);
        }

        // Real-time numeric dB readout below meter
        g.setColour((gr < -0.1f) ? juce::Colour(0xff00e5ff) : juce::Colour(0xff7c889a));
        g.setFont(juce::FontOptions(10.5f, juce::Font::bold));
        juce::String grText = (gr < -0.05f) ? (juce::String(gr, 1) + "dB") : "0.0dB";
        g.drawText(grText, static_cast<int>(bx), static_cast<int>(meterTopY + meterH + 4), static_cast<int>(colW), 14, juce::Justification::centred);

        // Contour offset badge
        float offset = (m_profile == autolevel::dsp::TargetProfile::MODERN_MIX)
            ? contours[static_cast<size_t>(b)] : 0.0f;

        juce::String offsetStr = (offset > 0.0f) ? ("+" + juce::String(offset, 1)) : juce::String(offset, 1);
        juce::Colour offsetCol = (offset < 0.0f) ? juce::Colour(0xffffb300) : (offset > 0.0f ? juce::Colour(0xff00e5ff) : juce::Colour(0xff556070));
        g.setColour(offsetCol);
        g.setFont(juce::FontOptions(9.5f, juce::Font::bold));
        g.drawText(offsetStr, static_cast<int>(bx), static_cast<int>(meterTopY + meterH + 18), static_cast<int>(colW), 12, juce::Justification::centred);
    }
}

//==============================================================================
// AutoLevelDJAudioProcessorEditor Implementation
//==============================================================================

AutoLevelDJAudioProcessorEditor::AutoLevelDJAudioProcessorEditor(AutoLevelDJAudioProcessor& p)
    : AudioProcessorEditor(&p), m_processor(p)
{
    setLookAndFeel(&m_lookAndFeel);
    setSize(840, 660);

    // Setup Visualizers
    addAndMakeVisible(m_toneVisualizer);
    addAndMakeVisible(m_meterRack);

    // Row 1: Primary Knobs
    setupRotary(m_targetLufsSlider, m_targetLufsLabel, "TARGET LUFS", " LUFS", juce::Colour(0xff00e676));
    setupRotary(m_compressionSlider, m_compressionLabel, "COMPRESSION", "x", juce::Colour(0xff00e5ff));
    setupRotary(m_levelResponseSlider, m_levelResponseLabel, "LEVEL RESPONSE", "", juce::Colour(0xff00e5ff));
    setupRotary(m_toneSlopeSlider, m_toneSlopeLabel, "TONE SLOPE", " dB/oct", juce::Colour(0xffffb300));

    // Row 2: Secondary Knobs (including Post-MBC Gain)
    setupRotary(m_maxBoostSlider, m_maxBoostLabel, "MAX BOOST", " dB", juce::Colour(0xff00e5ff));
    setupRotary(m_maxCutSlider, m_maxCutLabel, "MAX CUT", " dB", juce::Colour(0xffffb300));
    setupRotary(m_postGainSlider, m_postGainLabel, "POST GAIN", " dB", juce::Colour(0xff00e5ff));
    setupRotary(m_ceilingSlider, m_ceilingLabel, "LIMITER CEILING", " dBFS", juce::Colour(0xffff3366));

    // Breakdown freeze (located in Card 2 - AGC Gain Correction)
    m_freezeBreakdownsButton.setButtonText("Breakdown Freeze");
    m_freezeBreakdownsButton.setColour(juce::ToggleButton::textColourId, juce::Colour(0xffffb300));
    addAndMakeVisible(m_freezeBreakdownsButton);

    // Bypass Button
    m_bypassButton.setButtonText("BYPASS");
    m_bypassButton.setColour(juce::ToggleButton::textColourId, juce::Colour(0xffff3366));
    addAndMakeVisible(m_bypassButton);

    // Reset button
    m_resetButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff181d26));
    m_resetButton.setColour(juce::TextButton::textColourOffId, juce::Colour(0xff00e5ff));
    m_resetButton.onClick = [this]() { m_processor.resetIntegration(); };
    addAndMakeVisible(m_resetButton);

    // Hidden APVTS-bound ComboBox for Profile
    m_profileBox.addItem("Pink Noise (Linear)", 1);
    m_profileBox.addItem("Modern Mix (Contoured)", 2);
    addChildComponent(m_profileBox);

    // Tactile Profile Segmented Buttons
    m_pinkNoiseBtn.setButtonText("PINK NOISE");
    m_pinkNoiseBtn.setClickingTogglesState(false);
    m_pinkNoiseBtn.onClick = [this]() {
        m_profileBox.setSelectedItemIndex(0, juce::sendNotificationSync);
    };
    addAndMakeVisible(m_pinkNoiseBtn);

    m_modernMixBtn.setButtonText("MODERN MIX");
    m_modernMixBtn.setClickingTogglesState(false);
    m_modernMixBtn.onClick = [this]() {
        m_profileBox.setSelectedItemIndex(1, juce::sendNotificationSync);
    };
    addAndMakeVisible(m_modernMixBtn);

    m_profileDescLabel.setText("Club Contour: -2.5dB @ 250Hz | -2.0dB @ 5kHz", juce::dontSendNotification);
    m_profileDescLabel.setFont(juce::FontOptions(10.0f, juce::Font::bold));
    m_profileDescLabel.setJustificationType(juce::Justification::centred);
    m_profileDescLabel.setColour(juce::Label::textColourId, juce::Colour(0xff8b95a5));
    addAndMakeVisible(m_profileDescLabel);

    // MBC Speed Controls (Segmented header buttons)
    m_mbcSpeedBox.addItem("Slow", 1);
    m_mbcSpeedBox.addItem("Normal", 2);
    m_mbcSpeedBox.addItem("Fast", 3);
    addChildComponent(m_mbcSpeedBox);

    m_mbcSpeedLabel.setText("MBC SPEED:", juce::dontSendNotification);
    m_mbcSpeedLabel.setFont(juce::FontOptions(10.0f, juce::Font::bold));
    m_mbcSpeedLabel.setColour(juce::Label::textColourId, juce::Colour(0xff8b95a5));
    m_mbcSpeedLabel.setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(m_mbcSpeedLabel);

    auto setupSpeedBtn = [this](juce::TextButton& btn, int index) {
        btn.setClickingTogglesState(false);
        btn.onClick = [this, index]() {
            m_mbcSpeedBox.setSelectedItemIndex(index, juce::sendNotificationSync);
        };
        addAndMakeVisible(btn);
    };

    setupSpeedBtn(m_speedSlowBtn, 0);
    setupSpeedBtn(m_speedNormalBtn, 1);
    setupSpeedBtn(m_speedFastBtn, 2);

    // Sub Weight Controls (Segmented header buttons in Card 4)
    m_subWeightBox.addItem("Off", 1);
    m_subWeightBox.addItem("Low", 2);
    m_subWeightBox.addItem("Medium", 3);
    m_subWeightBox.addItem("High", 4);
    addChildComponent(m_subWeightBox);

    m_subWeightLabel.setText("SUB WEIGHT:", juce::dontSendNotification);
    m_subWeightLabel.setFont(juce::FontOptions(10.0f, juce::Font::bold));
    m_subWeightLabel.setColour(juce::Label::textColourId, juce::Colour(0xff8b95a5));
    m_subWeightLabel.setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(m_subWeightLabel);

    auto setupSubWeightBtn = [this](juce::TextButton& btn, int index) {
        btn.setClickingTogglesState(false);
        btn.onClick = [this, index]() {
            m_subWeightBox.setSelectedItemIndex(index, juce::sendNotificationSync);
        };
        addAndMakeVisible(btn);
    };

    setupSubWeightBtn(m_subWeightOffBtn, 0);
    setupSubWeightBtn(m_subWeightLowBtn, 1);
    setupSubWeightBtn(m_subWeightMedBtn, 2);
    setupSubWeightBtn(m_subWeightHighBtn, 3);

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
    m_mbcSpeedAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        apvts, AutoLevelDJAudioProcessor::ID_MBC_SPEED, m_mbcSpeedBox);
    m_subWeightAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        apvts, AutoLevelDJAudioProcessor::ID_SUB_WEIGHT, m_subWeightBox);
    m_maxBoostAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        apvts, AutoLevelDJAudioProcessor::ID_MAX_BOOST, m_maxBoostSlider);
    m_maxCutAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        apvts, AutoLevelDJAudioProcessor::ID_MAX_CUT, m_maxCutSlider);
    m_postGainAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        apvts, AutoLevelDJAudioProcessor::ID_POST_MBC_GAIN, m_postGainSlider);
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
    setLookAndFeel(nullptr);
}

void AutoLevelDJAudioProcessorEditor::setupRotary(juce::Slider& slider, juce::Label& label,
                                                 const juce::String& text, const juce::String& suffix,
                                                 juce::Colour accentCol)
{
    slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 76, 22);
    slider.setTextValueSuffix(suffix);
    slider.setColour(juce::Slider::rotarySliderFillColourId, accentCol);
    addAndMakeVisible(slider);

    label.setText(text, juce::dontSendNotification);
    label.setFont(juce::FontOptions(11.0f, juce::Font::bold));
    label.setJustificationType(juce::Justification::centred);
    label.setColour(juce::Label::textColourId, juce::Colour(0xff9aa5b4));
    addAndMakeVisible(label);
}

void AutoLevelDJAudioProcessorEditor::timerCallback() {
    m_latestState = m_processor.getVisualState();

    // Sync profile segmented buttons
    int curIdx = m_profileBox.getSelectedItemIndex();
    bool isPink = (curIdx == 0);
    m_pinkNoiseBtn.setToggleState(isPink, juce::dontSendNotification);
    m_modernMixBtn.setToggleState(!isPink, juce::dontSendNotification);

    if (isPink) {
        m_profileDescLabel.setText("Linear 1/f: Flat octave energy balance", juce::dontSendNotification);
        m_profileDescLabel.setColour(juce::Label::textColourId, juce::Colour(0xffffb300));
    } else {
        m_profileDescLabel.setText("Club Contour: -2.5dB @ 250Hz | -2.0dB @ 5kHz", juce::dontSendNotification);
        m_profileDescLabel.setColour(juce::Label::textColourId, juce::Colour(0xff00e5ff));
    }

    // Sync MBC speed segmented buttons
    int speedIdx = m_mbcSpeedBox.getSelectedItemIndex();
    m_speedSlowBtn.setToggleState(speedIdx == 0, juce::dontSendNotification);
    m_speedNormalBtn.setToggleState(speedIdx == 1, juce::dontSendNotification);
    m_speedFastBtn.setToggleState(speedIdx == 2, juce::dontSendNotification);

    // Sync Sub Weight segmented buttons
    int subWeightIdx = m_subWeightBox.getSelectedItemIndex();
    m_subWeightOffBtn.setToggleState(subWeightIdx == 0, juce::dontSendNotification);
    m_subWeightLowBtn.setToggleState(subWeightIdx == 1, juce::dontSendNotification);
    m_subWeightMedBtn.setToggleState(subWeightIdx == 2, juce::dontSendNotification);
    m_subWeightHighBtn.setToggleState(subWeightIdx == 3, juce::dontSendNotification);

    // Update Visualizers
    m_toneVisualizer.updateCurve(m_latestState.activeProfile, m_latestState.activeToneSlope, m_latestState.mbcThresholdsDb);
    m_meterRack.updateMeters(m_latestState.mbcGainReductionsDb, m_latestState.activeProfile, m_latestState.activeSubWeight);

    repaint();
}

void AutoLevelDJAudioProcessorEditor::paint(juce::Graphics& g) {
    // Obsidian dark chassis backdrop
    g.fillAll(juce::Colour(0xff0a0c10));

    // Top Header Bar
    juce::Rectangle<int> headerArea(0, 0, getWidth(), 54);
    juce::ColourGradient headerGrad(juce::Colour(0xff161a24), 0.0f, 0.0f,
                                    juce::Colour(0xff0e1118), 0.0f, 54.0f, false);
    g.setGradientFill(headerGrad);
    g.fillRect(headerArea);

    g.setColour(juce::Colour(0xff222938));
    g.drawHorizontalLine(54, 0.0f, static_cast<float>(getWidth()));

    // Title & Logo
    g.setColour(juce::Colour(0xff00e5ff));
    g.setFont(juce::FontOptions(20.0f, juce::Font::bold));
    g.drawText("AUTOLEVEL DJ", 22, 10, 160, 24, juce::Justification::left);

    // PRO badge
    juce::Rectangle<float> badgeRect(178.0f, 13.0f, 38.0f, 16.0f);
    g.setColour(juce::Colour(0xff00e5ff).withAlpha(0.18f));
    g.fillRoundedRectangle(badgeRect, 3.0f);
    g.setColour(juce::Colour(0xff00e5ff));
    g.drawRoundedRectangle(badgeRect, 3.0f, 1.0f);
    g.setFont(juce::FontOptions(9.5f, juce::Font::bold));
    g.drawText("PRO", badgeRect, juce::Justification::centred);

    g.setColour(juce::Colour(0xff757f90));
    g.setFont(juce::FontOptions(10.5f, juce::Font::plain));
    g.drawText("DYNAMIC BROADCAST & CLUB LEVELING SYSTEM", 22, 32, 340, 16, juce::Justification::left);

    auto drawCard = [&g](juce::Rectangle<int> bounds, const juce::String& title) {
        juce::ColourGradient cardGrad(juce::Colour(0xff141822), static_cast<float>(bounds.getX()), static_cast<float>(bounds.getY()),
                                     juce::Colour(0xff0f121a), static_cast<float>(bounds.getX()), static_cast<float>(bounds.getBottom()), false);
        g.setGradientFill(cardGrad);
        g.fillRoundedRectangle(bounds.toFloat(), 6.0f);
        g.setColour(juce::Colour(0xff242c3b));
        g.drawRoundedRectangle(bounds.toFloat(), 6.0f, 1.0f);

        g.setColour(juce::Colour(0xff8893a4));
        g.setFont(juce::FontOptions(10.5f, juce::Font::bold));
        g.drawText(title, bounds.getX() + 12, bounds.getY() + 8, bounds.getWidth() - 24, 16, juce::Justification::left);
    };

    // Card 1: Perceived Loudness (Left)
    juce::Rectangle<int> integCard(20, 64, 240, 162);
    drawCard(integCard, "PERCEIVED LOUDNESS (EBU R128)");

    float integ = m_latestState.loudness.integratedLUFS;
    g.setFont(juce::FontOptions(32.0f, juce::Font::bold));
    if (integ > -70.0f) {
        g.setColour(juce::Colour(0xff00e676));
        g.drawText(juce::String(integ, 1) + " LUFS", integCard.getX(), integCard.getY() + 28, integCard.getWidth(), 36, juce::Justification::centred);
    } else {
        g.setColour(juce::Colour(0xff4a5360));
        g.drawText("---.- LUFS", integCard.getX(), integCard.getY() + 28, integCard.getWidth(), 36, juce::Justification::centred);
    }

    // Momentary & Short-Term Readings (EBU R128 M & S)
    g.setFont(juce::FontOptions(11.0f, juce::Font::plain));
    float mom = m_latestState.loudness.momentaryLUFS;
    float st = m_latestState.loudness.shortTermLUFS;
    juce::String momStr = (mom > -70.0f) ? (juce::String(mom, 1) + " LUFS") : "--.-";
    juce::String stStr  = (st > -70.0f)  ? (juce::String(st, 1)  + " LUFS") : "--.-";

    g.setColour(juce::Colour(0xff9aa3b0));
    g.drawText("M (400ms): " + momStr + "  |  S (3s): " + stStr, integCard.getX() + 8, integCard.getY() + 68, integCard.getWidth() - 16, 16, juce::Justification::centred);

    // Target Delta (Clean ASCII - no Greek delta / mojibake!)
    float targetLufs = static_cast<float>(m_targetLufsSlider.getValue());
    float delta = (integ > -70.0f) ? (integ - targetLufs) : 0.0f;
    juce::String deltaSign = (delta >= 0.0f) ? "+" : "";
    juce::String deltaStr = "Target Delta: " + deltaSign + juce::String(delta, 1) + " LU";
    g.setColour((std::abs(delta) < 1.0f) ? juce::Colour(0xff00e676) : juce::Colour(0xffffb300));
    g.setFont(juce::FontOptions(11.5f, juce::Font::bold));
    g.drawText(deltaStr, integCard.getX() + 12, integCard.getY() + 90, integCard.getWidth() - 24, 16, juce::Justification::centred);

    // Integrated Status Badge
    juce::Rectangle<float> statusRect(static_cast<float>(integCard.getX() + 16), static_cast<float>(integCard.getY() + 124),
                                      static_cast<float>(integCard.getWidth() - 32), 24.0f);
    g.setColour(juce::Colour(0xff18222f));
    g.fillRoundedRectangle(statusRect, 4.0f);
    g.setColour(juce::Colour(0xff00e676));
    g.drawRoundedRectangle(statusRect, 4.0f, 1.0f);
    g.setFont(juce::FontOptions(9.5f, juce::Font::bold));
    g.drawText("EBU R128 DUAL-GATED CALIBRATED", statusRect, juce::Justification::centred);

    // Card 2: AGC Gain Rider (Middle)
    juce::Rectangle<int> gainCard(270, 64, 240, 162);
    drawCard(gainCard, "AGC GAIN CORRECTION");

    float gain = m_latestState.appliedGainDb;
    g.setFont(juce::FontOptions(34.0f, juce::Font::bold));
    if (gain >= 0.0f) {
        g.setColour(juce::Colour(0xff00e5ff));
        g.drawText("+" + juce::String(gain, 1) + " dB", gainCard.getX(), gainCard.getY() + 28, gainCard.getWidth(), 36, juce::Justification::centred);
    } else {
        g.setColour(juce::Colour(0xffffb300));
        g.drawText(juce::String(gain, 1) + " dB", gainCard.getX(), gainCard.getY() + 28, gainCard.getWidth(), 36, juce::Justification::centred);
    }

    // Bi-directional AGC Meter Bar
    float meterBarX = static_cast<float>(gainCard.getX() + 20);
    float meterBarY = static_cast<float>(gainCard.getY() + 70);
    float meterBarW = static_cast<float>(gainCard.getWidth() - 40);
    float meterBarH = 10.0f;
    g.setColour(juce::Colour(0xff0a0c10));
    g.fillRoundedRectangle(meterBarX, meterBarY, meterBarW, meterBarH, 3.0f);

    float centerBarX = meterBarX + meterBarW * 0.5f;
    float normGain = std::clamp(gain / 12.0f, -1.0f, 1.0f);
    if (normGain > 0.0f) {
        g.setColour(juce::Colour(0xff00e5ff));
        g.fillRoundedRectangle(centerBarX, meterBarY, normGain * (meterBarW * 0.5f), meterBarH, 2.0f);
    } else if (normGain < 0.0f) {
        g.setColour(juce::Colour(0xffffb300));
        float fillW = -normGain * (meterBarW * 0.5f);
        g.fillRoundedRectangle(centerBarX - fillW, meterBarY, fillW, meterBarH, 2.0f);
    }
    g.setColour(juce::Colour(0xff2d3646));
    g.drawVerticalLine(static_cast<int>(centerBarX), meterBarY - 2.0f, meterBarY + meterBarH + 2.0f);

    // Target & Half-life readout
    g.setColour(juce::Colour(0xff9aa3b0));
    g.setFont(juce::FontOptions(11.0f, juce::Font::plain));
    juce::String hlStr = (m_latestState.activeHalfLifeSeconds > 0.0f)
        ? ("T1/2: " + juce::String(static_cast<int>(m_latestState.activeHalfLifeSeconds)) + "s")
        : "Track Hold";
    juce::String targetSign = (m_latestState.targetGainDb >= 0.0f) ? "+" : "";
    g.drawText("Target: " + targetSign + juce::String(m_latestState.targetGainDb, 1) + " dB (" + hlStr + ")",
               gainCard.getX(), gainCard.getY() + 88, gainCard.getWidth(), 18, juce::Justification::centred);

    // Card 3: Tone Target & Profile Visualizer (Right)
    juce::Rectangle<int> profileCard(520, 64, 300, 162);
    drawCard(profileCard, "TONAL TARGET & SPECTRUM CURVE");

    // Card 4: 6-Band Dynamics Metering Card (Middle Full Width)
    juce::Rectangle<int> mbcCard(20, 236, 800, 158);
    drawCard(mbcCard, "DYNAMIC TONE SHAPER");

    // Real-time Sub-Harmonic Injection Activity Meter in Card 4 header
    float meterX = 458.0f;
    float meterY = 244.0f;
    float meterW = 82.0f;
    float meterH = 16.0f;

    juce::Rectangle<float> subTrough(meterX, meterY, meterW, meterH);
    g.setColour(juce::Colour(0xff0a0d13));
    g.fillRoundedRectangle(subTrough, 3.0f);
    g.setColour(juce::Colour(0xff1e2634));
    g.drawRoundedRectangle(subTrough, 3.0f, 1.0f);

    bool isSubActive = (m_latestState.activeSubWeight != autolevel::dsp::SubWeight::OFF);
    if (!isSubActive) {
        g.setColour(juce::Colour(0xff454f5e));
        g.setFont(juce::FontOptions(8.5f, juce::Font::bold));
        g.drawText("METER OFF", subTrough, juce::Justification::centred);
    } else {
        constexpr int NUM_LEDS = 6;
        float normLevel = std::clamp(m_latestState.subInjectedLevel / 0.30f, 0.0f, 1.0f);
        int activeLeds = static_cast<int>(std::round(normLevel * static_cast<float>(NUM_LEDS)));

        float ledW = 10.0f;
        float ledH = 10.0f;
        float ledY = meterY + 3.0f;
        float startLedX = meterX + 5.0f;

        for (int i = 0; i < NUM_LEDS; ++i) {
            float lx = startLedX + static_cast<float>(i) * (ledW + 2.0f);
            juce::Rectangle<float> ledRect(lx, ledY, ledW, ledH);

            if (i < activeLeds) {
                juce::Colour col = (i < 3) ? juce::Colour(0xff00e5ff) :
                                   (i < 5) ? juce::Colour(0xff00e676) : juce::Colour(0xffffb300);
                g.setColour(col);
                g.fillRoundedRectangle(ledRect, 1.5f);
            } else {
                g.setColour(juce::Colour(0xff141a24));
                g.fillRoundedRectangle(ledRect, 1.5f);
            }
        }
    }

    // Card 5: Parameters Panel (Bottom Full Width)
    juce::Rectangle<int> ctrlCard(20, 404, 800, 244);
    drawCard(ctrlCard, "MASTER PROCESSOR CONTROLS");
}

void AutoLevelDJAudioProcessorEditor::resized() {
    // Header Buttons
    m_bypassButton.setBounds(getWidth() - 116, 13, 96, 28);
    m_resetButton.setBounds(getWidth() - 326, 13, 200, 28);

    // Profile Card Controls
    m_pinkNoiseBtn.setBounds(530, 92, 136, 26);
    m_modernMixBtn.setBounds(674, 92, 136, 26);
    m_profileDescLabel.setBounds(524, 120, 292, 16);

    // Tone Curve Visualizer inside Profile Card
    m_toneVisualizer.setBounds(530, 138, 280, 80);

    // Breakdown Freeze button inside Card 2 (AGC Gain Correction)
    m_freezeBreakdownsButton.setBounds(288, 180, 204, 28);

    // Sub Weight Controls inside Card 4 header
    m_subWeightLabel.setBounds(195, 242, 78, 20);
    m_subWeightOffBtn.setBounds(276, 242, 38, 20);
    m_subWeightLowBtn.setBounds(318, 242, 40, 20);
    m_subWeightMedBtn.setBounds(362, 242, 40, 20);
    m_subWeightHighBtn.setBounds(406, 242, 44, 20);

    // MBC Speed Controls inside Card 4 header
    m_mbcSpeedLabel.setBounds(555, 242, 75, 20);
    m_speedSlowBtn.setBounds(635, 242, 48, 20);
    m_speedNormalBtn.setBounds(687, 242, 58, 20);
    m_speedFastBtn.setBounds(749, 242, 48, 20);

    // 6-Band Meter Rack inside MBC Card
    m_meterRack.setBounds(26, 266, 788, 120);

    // Controls Row 1 (Primary Master Knobs)
    int row1Y = 424;
    int knobW = 100;
    int knobH = 88;
    int colSpacing = 195;
    int startX = 35;

    m_targetLufsLabel.setBounds(startX, row1Y, knobW, 14);
    m_targetLufsSlider.setBounds(startX, row1Y + 14, knobW, knobH);

    m_compressionLabel.setBounds(startX + colSpacing, row1Y, knobW, 14);
    m_compressionSlider.setBounds(startX + colSpacing, row1Y + 14, knobW, knobH);

    m_levelResponseLabel.setBounds(startX + 2 * colSpacing, row1Y, knobW, 14);
    m_levelResponseSlider.setBounds(startX + 2 * colSpacing, row1Y + 14, knobW, knobH);

    m_toneSlopeLabel.setBounds(startX + 3 * colSpacing, row1Y, knobW, 14);
    m_toneSlopeSlider.setBounds(startX + 3 * colSpacing, row1Y + 14, knobW, knobH);

    // Controls Row 2 (Secondary Knobs: Boost, Cut, Post-MBC Gain, Limiter Ceiling)
    int row2Y = 536;

    m_maxBoostLabel.setBounds(startX, row2Y, knobW, 14);
    m_maxBoostSlider.setBounds(startX, row2Y + 14, knobW, knobH);

    m_maxCutLabel.setBounds(startX + colSpacing, row2Y, knobW, 14);
    m_maxCutSlider.setBounds(startX + colSpacing, row2Y + 14, knobW, knobH);

    m_postGainLabel.setBounds(startX + 2 * colSpacing, row2Y, knobW, 14);
    m_postGainSlider.setBounds(startX + 2 * colSpacing, row2Y + 14, knobW, knobH);

    m_ceilingLabel.setBounds(startX + 3 * colSpacing, row2Y, knobW, 14);
    m_ceilingSlider.setBounds(startX + 3 * colSpacing, row2Y + 14, knobW, knobH);
}
