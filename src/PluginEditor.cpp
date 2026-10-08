#include "PluginEditor.h"

//==============================================================================
// MainContentComponent Implementation (Proportional Resizing Canvas)
//==============================================================================

void MainContentComponent::paint(juce::Graphics& g) {
    m_owner.paintContent(g);
}

void MainContentComponent::resized() {
    m_owner.layoutContent();
}

void MainContentComponent::mouseDown(const juce::MouseEvent& e) {
    m_owner.contentMouseDown(e);
}

void MainContentComponent::mouseMove(const juce::MouseEvent& e) {
    m_owner.contentMouseMove(e);
}

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

void ModernHardwareLookAndFeel::drawLinearSlider(juce::Graphics& g, int x, int y, int width, int height,
                                                 float sliderPos, float minSliderPos, float maxSliderPos,
                                                 juce::Slider::SliderStyle style, juce::Slider& slider)
{
    if (style != juce::Slider::LinearVertical) {
        juce::LookAndFeel_V4::drawLinearSlider(g, x, y, width, height, sliderPos, minSliderPos, maxSliderPos, style, slider);
        return;
    }

    // Bipolar fader: the groove runs the full travel, the lit bar runs from the 0 mark to the thumb.
    const float cx = static_cast<float>(x) + static_cast<float>(width) * 0.5f;
    // minSliderPos / maxSliderPos are the range-slider thumb positions, not the ends of the travel,
    // so ask the slider where its range ends and where 0 dB sits (all in this slider's own coords).
    const float bottomY = static_cast<float>(slider.getPositionOfValue(slider.getMinimum()));
    const float topY = static_cast<float>(slider.getPositionOfValue(slider.getMaximum()));
    const float zeroY = static_cast<float>(slider.getPositionOfValue(0.0));

    juce::Rectangle<float> groove(cx - 2.5f, topY, 5.0f, bottomY - topY);
    g.setColour(juce::Colour(0xff0a0c10));
    g.fillRoundedRectangle(groove, 2.5f);
    g.setColour(juce::Colour(0xff1c222e));
    g.drawRoundedRectangle(groove, 2.5f, 1.0f);

    // Scale ticks every quarter of travel, the 0 dB one stronger
    for (int i = 0; i <= 4; ++i) {
        float ty = topY + (bottomY - topY) * (static_cast<float>(i) / 4.0f);
        g.setColour(juce::Colour(0xff252e3e));
        g.drawHorizontalLine(static_cast<int>(std::round(ty)), cx - 11.0f, cx - 6.0f);
        g.drawHorizontalLine(static_cast<int>(std::round(ty)), cx + 6.0f, cx + 11.0f);
    }
    g.setColour(juce::Colour(0xff4a566a));
    g.drawHorizontalLine(static_cast<int>(std::round(zeroY)), cx - 13.0f, cx + 13.0f);

    const bool boost = slider.getValue() >= 0.0;
    juce::Colour fillCol = boost ? slider.findColour(juce::Slider::trackColourId) : juce::Colour(0xffffb300);
    float barTop = std::min(zeroY, sliderPos);
    float barBottom = std::max(zeroY, sliderPos);
    if (barBottom - barTop > 0.5f) {
        g.setColour(fillCol.withAlpha(0.25f));
        g.fillRoundedRectangle(cx - 4.5f, barTop, 9.0f, barBottom - barTop, 3.0f);
        g.setColour(fillCol);
        g.fillRoundedRectangle(cx - 2.0f, barTop, 4.0f, barBottom - barTop, 2.0f);
    }

    // Thumb
    juce::Rectangle<float> thumb(cx - 11.0f, sliderPos - 5.5f, 22.0f, 11.0f);
    juce::ColourGradient grad(juce::Colour(0xff2a3240), thumb.getX(), thumb.getY(),
                              juce::Colour(0xff151a22), thumb.getX(), thumb.getBottom(), false);
    g.setGradientFill(grad);
    g.fillRoundedRectangle(thumb, 3.0f);
    g.setColour(juce::Colour(0xff3b465a));
    g.drawRoundedRectangle(thumb, 3.0f, 1.0f);
    g.setColour(juce::Colours::white);
    g.drawHorizontalLine(static_cast<int>(std::round(thumb.getCentreY())), thumb.getX() + 4.0f, thumb.getRight() - 4.0f);
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
                                      autolevel::dsp::TargetProfile profile)
{
    m_profile = profile;
    for (size_t b = 0; b < autolevel::dsp::Bands::COUNT; ++b) {
        float gr = gainReductions[b]; // Negative dB (0 to -12)
        m_currentGr[b] = gr;

        if (gr < m_peakGr[b]) { // Greater reduction
            m_peakGr[b] = gr;
            m_peakHoldTimers[b] = 36; // ~600ms hold at 60Hz
        } else {
            if (m_peakHoldTimers[b] > 0) {
                --m_peakHoldTimers[b];
            } else {
                m_peakGr[b] = std::min(0.0f, m_peakGr[b] + 0.18f); // Smooth decay at 60Hz
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
    float scaleW = SCALE_W;
    float colW = columnWidth(bounds.getWidth());

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
        float bx = bounds.getX() + columnX(static_cast<size_t>(b), bounds.getWidth());
        // Left part of the column is the GR meter; the right part is left free for the
        // band's EQ fader, which the editor places there.
        float barW = 34.0f;
        float barX = bx + 10.0f;
        float meterColW = barW + 20.0f; // span the readouts below the meter are centred in

        g.setColour(juce::Colour(0xffd0d7e2));
        g.setFont(juce::FontOptions(11.0f, juce::Font::bold));
        g.drawText(juce::String(bandNames[static_cast<size_t>(b)].data()), static_cast<int>(bx), static_cast<int>(bounds.getY() + 5), static_cast<int>(colW), 16, juce::Justification::centred);

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
        g.drawText(grText, static_cast<int>(bx), static_cast<int>(meterTopY + meterH + 4), static_cast<int>(meterColW), 14, juce::Justification::centred);

        // Contour offset badge
        float offset = (m_profile == autolevel::dsp::TargetProfile::MODERN_MIX)
            ? contours[static_cast<size_t>(b)] : 0.0f;

        juce::String offsetStr = (offset > 0.0f) ? ("+" + juce::String(offset, 1)) : juce::String(offset, 1);
        juce::Colour offsetCol = (offset < 0.0f) ? juce::Colour(0xffffb300) : (offset > 0.0f ? juce::Colour(0xff00e5ff) : juce::Colour(0xff556070));
        g.setColour(offsetCol);
        g.setFont(juce::FontOptions(9.5f, juce::Font::bold));
        g.drawText(offsetStr, static_cast<int>(bx), static_cast<int>(meterTopY + meterH + 18), static_cast<int>(meterColW), 12, juce::Justification::centred);
    }
}

//==============================================================================
// AutoLevelDJAudioProcessorEditor Implementation
//==============================================================================

AutoLevelDJAudioProcessorEditor::AutoLevelDJAudioProcessorEditor(AutoLevelDJAudioProcessor& p)
    : AudioProcessorEditor(&p), m_processor(p)
{
    setLookAndFeel(&m_lookAndFeel);
    m_content.setLookAndFeel(&m_lookAndFeel);
    addAndMakeVisible(m_content);

    setResizable(true, true);
    setResizeLimits(630, 525, 1680, 1400);
    getConstrainer()->setFixedAspectRatio(840.0 / 700.0);
    setSize(840, 700);

    // Setup Visualizers
    m_content.addAndMakeVisible(m_toneVisualizer);
    m_content.addAndMakeVisible(m_meterRack);

    // Row 1: Primary Knobs
    setupRotary(m_targetLufsSlider, m_targetLufsLabel, "TARGET LUFS", " LUFS", juce::Colour(0xff00e676));
    setupRotary(m_compressionSlider, m_compressionLabel, "COMPRESSION", "x", juce::Colour(0xff00e5ff));
    setupRotary(m_levelResponseSlider, m_levelResponseLabel, "LEVEL RESPONSE", "", juce::Colour(0xff00e5ff));
    setupRotary(m_toneSlopeSlider, m_toneSlopeLabel, "TONE SLOPE", " dB/oct", juce::Colour(0xffffb300));
    setupRotary(m_ceilingSlider, m_ceilingLabel, "LIMITER CEILING", " dBFS", juce::Colour(0xffff3366));
    setupRotary(m_detectorSlider, m_detectorLabel, "DETECTOR", "", juce::Colour(0xff00e5ff));
    setupRotary(m_attackSlider, m_attackLabel, "MBC ATTACK", "", juce::Colour(0xff00e5ff));
    setupRotary(m_releaseSlider, m_releaseLabel, "MBC RELEASE", "", juce::Colour(0xffffb300));

    // Row 2: Secondary Knobs
    setupRotary(m_maxBoostSlider, m_maxBoostLabel, "MAX BOOST", " dB", juce::Colour(0xff00e5ff));
    setupRotary(m_maxCutSlider, m_maxCutLabel, "MAX CUT", " dB", juce::Colour(0xffffb300));
    setupRotary(m_postGainSlider, m_postGainLabel, "POST GAIN", " dB", juce::Colour(0xff00e5ff));
    setupRotary(m_hpfSlider, m_hpfLabel, "LOW CUT", " Hz", juce::Colour(0xffffb300));

    // AGC Slew Speed Controls (Segmented header buttons, Card 2 - AGC Gain Correction)
    m_slewSpeedBox.addItem("Slow", 1);
    m_slewSpeedBox.addItem("Normal", 2);
    m_slewSpeedBox.addItem("Fast", 3);
    m_content.addChildComponent(m_slewSpeedBox);

    m_slewSpeedLabel.setText("SLEW:", juce::dontSendNotification);
    m_slewSpeedLabel.setFont(juce::FontOptions(10.0f, juce::Font::bold));
    m_slewSpeedLabel.setColour(juce::Label::textColourId, juce::Colour(0xff8b95a5));
    m_slewSpeedLabel.setJustificationType(juce::Justification::centredRight);
    m_content.addAndMakeVisible(m_slewSpeedLabel);

    auto setupSlewSpeedBtn = [this](juce::TextButton& btn, int index) {
        btn.setClickingTogglesState(false);
        btn.onClick = [this, index]() {
            m_slewSpeedBox.setSelectedItemIndex(index, juce::sendNotificationSync);
        };
        m_content.addAndMakeVisible(btn);
    };

    setupSlewSpeedBtn(m_slewSlowBtn, 0);
    setupSlewSpeedBtn(m_slewNormalBtn, 1);
    setupSlewSpeedBtn(m_slewFastBtn, 2);

    // Breakdown freeze (located in Card 2 - AGC Gain Correction)
    m_freezeBreakdownsButton.setButtonText("Breakdown Freeze");
    m_freezeBreakdownsButton.setColour(juce::ToggleButton::textColourId, juce::Colour(0xffffb300));
    m_content.addAndMakeVisible(m_freezeBreakdownsButton);

    // Bypass Button
    m_bypassButton.setButtonText("BYPASS");
    m_bypassButton.setColour(juce::ToggleButton::textColourId, juce::Colour(0xffff3366));
    m_content.addAndMakeVisible(m_bypassButton);

    // Reset button
    m_resetButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff181d26));
    m_resetButton.setColour(juce::TextButton::textColourOffId, juce::Colour(0xff00e5ff));
    m_resetButton.onClick = [this]() { m_processor.resetIntegration(); };
    m_content.addAndMakeVisible(m_resetButton);

    // Limiter lookahead (header): Off = original zero-latency limiter, 1/2 ms = clean lookahead limiting
    m_lookaheadBox.addItem("Off", 1);
    m_lookaheadBox.addItem("1 ms", 2);
    m_lookaheadBox.addItem("2 ms", 3);
    m_content.addChildComponent(m_lookaheadBox);

    m_lookaheadLabel.setText("LOOKAHEAD:", juce::dontSendNotification);
    m_lookaheadLabel.setFont(juce::FontOptions(10.0f, juce::Font::bold));
    m_lookaheadLabel.setColour(juce::Label::textColourId, juce::Colour(0xff8b95a5));
    m_lookaheadLabel.setJustificationType(juce::Justification::centredRight);
    m_lookaheadLabel.setTooltip("Safety limiter lookahead. 1 or 2 ms stops the limiter distorting when pushed, "
                                "at that much added latency. Off = zero latency.");
    m_content.addAndMakeVisible(m_lookaheadLabel);

    auto setupLookaheadBtn = [this](juce::TextButton& btn, int index) {
        btn.setClickingTogglesState(false);
        btn.onClick = [this, index]() {
            m_lookaheadBox.setSelectedItemIndex(index, juce::sendNotificationSync);
        };
        m_content.addAndMakeVisible(btn);
    };
    setupLookaheadBtn(m_lookaheadOffBtn, 0);
    setupLookaheadBtn(m_lookahead1Btn, 1);
    setupLookaheadBtn(m_lookahead2Btn, 2);

    // Hidden APVTS-bound ComboBox for Profile
    m_profileBox.addItem("Pink Noise (Linear)", 1);
    m_profileBox.addItem("Modern Mix (Contoured)", 2);
    m_content.addChildComponent(m_profileBox);

    // Tactile Profile Segmented Buttons
    m_pinkNoiseBtn.setButtonText("PINK NOISE");
    m_pinkNoiseBtn.setClickingTogglesState(false);
    m_pinkNoiseBtn.onClick = [this]() {
        m_profileBox.setSelectedItemIndex(0, juce::sendNotificationSync);
    };
    m_content.addAndMakeVisible(m_pinkNoiseBtn);

    m_modernMixBtn.setButtonText("MODERN MIX");
    m_modernMixBtn.setClickingTogglesState(false);
    m_modernMixBtn.onClick = [this]() {
        m_profileBox.setSelectedItemIndex(1, juce::sendNotificationSync);
    };
    m_content.addAndMakeVisible(m_modernMixBtn);

    m_profileDescLabel.setText("Club Contour: -2.5dB @ 250Hz | -2.0dB @ 5kHz", juce::dontSendNotification);
    m_profileDescLabel.setFont(juce::FontOptions(10.0f, juce::Font::bold));
    m_profileDescLabel.setJustificationType(juce::Justification::centred);
    m_profileDescLabel.setColour(juce::Label::textColourId, juce::Colour(0xff8b95a5));
    m_content.addAndMakeVisible(m_profileDescLabel);

    // Band EQ: Before/After-MBC switch in the Tone Shaper header (hidden APVTS-bound ComboBox + two buttons)
    m_eqPositionBox.addItem("Before MBC", 1);
    m_eqPositionBox.addItem("After MBC", 2);
    m_content.addChildComponent(m_eqPositionBox);

    m_eqPositionLabel.setText("EQ:", juce::dontSendNotification);
    m_eqPositionLabel.setFont(juce::FontOptions(10.0f, juce::Font::bold));
    m_eqPositionLabel.setColour(juce::Label::textColourId, juce::Colour(0xff8b95a5));
    m_eqPositionLabel.setJustificationType(juce::Justification::centredRight);
    m_content.addAndMakeVisible(m_eqPositionLabel);

    auto setupEqPosBtn = [this](juce::TextButton& btn, int index) {
        btn.setClickingTogglesState(false);
        btn.onClick = [this, index]() {
            m_eqPositionBox.setSelectedItemIndex(index, juce::sendNotificationSync);
        };
        m_content.addAndMakeVisible(btn);
    };
    setupEqPosBtn(m_eqBeforeBtn, 0);
    setupEqPosBtn(m_eqAfterBtn, 1);

    // MBC release mode (Tone Shaper header): Manual / Auto
    m_releaseModeBox.addItem("Manual", 1);
    m_releaseModeBox.addItem("Auto", 2);
    m_content.addChildComponent(m_releaseModeBox);

    m_releaseModeLabel.setText("RELEASE:", juce::dontSendNotification);
    m_releaseModeLabel.setFont(juce::FontOptions(10.0f, juce::Font::bold));
    m_releaseModeLabel.setColour(juce::Label::textColourId, juce::Colour(0xff8b95a5));
    m_releaseModeLabel.setJustificationType(juce::Justification::centredRight);
    m_releaseModeLabel.setTooltip("Auto: short hits recover at the MBC Release time, sustained "
                                  "compression up to 10x slower (max 5000 ms), so dense passages don't pump.");
    m_content.addAndMakeVisible(m_releaseModeLabel);

    auto setupReleaseModeBtn = [this](juce::TextButton& btn, int index) {
        btn.setClickingTogglesState(false);
        btn.onClick = [this, index]() {
            m_releaseModeBox.setSelectedItemIndex(index, juce::sendNotificationSync);
        };
        m_content.addAndMakeVisible(btn);
    };
    setupReleaseModeBtn(m_releaseManualBtn, 0);
    setupReleaseModeBtn(m_releaseAutoBtn, 1);

    // Band EQ faders, one beside each band's GR meter (added after the rack so they sit on top)
    for (auto& sl : m_eqSliders) {
        sl.setSliderStyle(juce::Slider::LinearVertical);
        sl.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 46, 16);
        sl.setColour(juce::Slider::trackColourId, juce::Colour(0xff00e5ff));
        sl.setTooltip("Band EQ gain (double-click to reset)");
        m_content.addAndMakeVisible(sl);
    }

    // APVTS Attachments
    auto& apvts = m_processor.getAPVTS();
    m_targetLufsAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        apvts, AutoLevelDJAudioProcessor::ID_TARGET_LUFS, m_targetLufsSlider);
    m_compressionAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        apvts, AutoLevelDJAudioProcessor::ID_COMPRESSION_AMOUNT, m_compressionSlider);
    m_levelResponseAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        apvts, AutoLevelDJAudioProcessor::ID_LEVEL_RESPONSE, m_levelResponseSlider);
    m_slewSpeedAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        apvts, AutoLevelDJAudioProcessor::ID_SLEW_SPEED, m_slewSpeedBox);
    m_toneSlopeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        apvts, AutoLevelDJAudioProcessor::ID_TONE_SLOPE, m_toneSlopeSlider);
    m_profileAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        apvts, AutoLevelDJAudioProcessor::ID_TARGET_PROFILE, m_profileBox);
    m_detectorAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        apvts, AutoLevelDJAudioProcessor::ID_MBC_DETECTOR, m_detectorSlider);
    m_attackAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        apvts, AutoLevelDJAudioProcessor::ID_MBC_ATTACK, m_attackSlider);
    m_releaseAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        apvts, AutoLevelDJAudioProcessor::ID_MBC_RELEASE, m_releaseSlider);
    m_eqPositionAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        apvts, AutoLevelDJAudioProcessor::ID_EQ_POSITION, m_eqPositionBox);
    m_releaseModeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        apvts, AutoLevelDJAudioProcessor::ID_MBC_RELEASE_MODE, m_releaseModeBox);
    m_lookaheadAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        apvts, AutoLevelDJAudioProcessor::ID_LIMITER_LOOKAHEAD, m_lookaheadBox);
    for (size_t b = 0; b < autolevel::dsp::Bands::COUNT; ++b) {
        m_eqAttachments[b] = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
            apvts, AutoLevelDJAudioProcessor::ID_EQ_BANDS[b], m_eqSliders[b]);
        m_eqSliders[b].setDoubleClickReturnValue(true, 0.0);
    }
    m_maxBoostAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        apvts, AutoLevelDJAudioProcessor::ID_MAX_BOOST, m_maxBoostSlider);
    m_maxCutAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        apvts, AutoLevelDJAudioProcessor::ID_MAX_CUT, m_maxCutSlider);
    m_postGainAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        apvts, AutoLevelDJAudioProcessor::ID_POST_MBC_GAIN, m_postGainSlider);
    m_hpfAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        apvts, AutoLevelDJAudioProcessor::ID_HPF_FREQ, m_hpfSlider);
    m_ceilingAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        apvts, AutoLevelDJAudioProcessor::ID_CEILING_DB, m_ceilingSlider);
    m_freezeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        apvts, AutoLevelDJAudioProcessor::ID_FREEZE_BREAKDOWNS, m_freezeBreakdownsButton);
    m_bypassAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        apvts, AutoLevelDJAudioProcessor::ID_BYPASS, m_bypassButton);

    startTimerHz(60);
}

AutoLevelDJAudioProcessorEditor::~AutoLevelDJAudioProcessorEditor() {
    stopTimer();
    m_content.setLookAndFeel(nullptr);
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
    m_content.addAndMakeVisible(slider);

    label.setText(text, juce::dontSendNotification);
    label.setFont(juce::FontOptions(11.0f, juce::Font::bold));
    label.setJustificationType(juce::Justification::centred);
    label.setColour(juce::Label::textColourId, juce::Colour(0xff9aa5b4));
    m_content.addAndMakeVisible(label);
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

    // Sync limiter lookahead segmented buttons
    int lookaheadIdx = m_lookaheadBox.getSelectedItemIndex();
    m_lookaheadOffBtn.setToggleState(lookaheadIdx == 0, juce::dontSendNotification);
    m_lookahead1Btn.setToggleState(lookaheadIdx == 1, juce::dontSendNotification);
    m_lookahead2Btn.setToggleState(lookaheadIdx == 2, juce::dontSendNotification);

    // Sync MBC release mode segmented buttons
    int releaseModeIdx = m_releaseModeBox.getSelectedItemIndex();
    m_releaseManualBtn.setToggleState(releaseModeIdx == 0, juce::dontSendNotification);
    m_releaseAutoBtn.setToggleState(releaseModeIdx == 1, juce::dontSendNotification);

    // Sync EQ position segmented buttons
    int eqPosIdx = m_eqPositionBox.getSelectedItemIndex();
    m_eqBeforeBtn.setToggleState(eqPosIdx == 0, juce::dontSendNotification);
    m_eqAfterBtn.setToggleState(eqPosIdx == 1, juce::dontSendNotification);

    // Sync AGC Slew Speed segmented buttons
    int slewIdx = m_slewSpeedBox.getSelectedItemIndex();
    m_slewSlowBtn.setToggleState(slewIdx == 0, juce::dontSendNotification);
    m_slewNormalBtn.setToggleState(slewIdx == 1, juce::dontSendNotification);
    m_slewFastBtn.setToggleState(slewIdx == 2, juce::dontSendNotification);

    // Track maximum peak-held limiter gain reduction
    float curLimGr = m_latestState.limiterGainReductionDb;
    if (curLimGr < m_maxHeldLimiterGrDb) {
        m_maxHeldLimiterGrDb = curLimGr;
    }

    // Update Visualizers
    m_toneVisualizer.updateCurve(m_latestState.activeProfile, m_latestState.activeToneSlope, m_latestState.mbcThresholdsDb);
    m_meterRack.updateMeters(m_latestState.mbcGainReductionsDb, m_latestState.activeProfile);

    m_content.repaint();
}

void AutoLevelDJAudioProcessorEditor::paint(juce::Graphics& g) {
    // Backdrop behind content container
    g.fillAll(juce::Colour(0xff0a0c10));
}

void AutoLevelDJAudioProcessorEditor::paintContent(juce::Graphics& g) {
    // Obsidian dark chassis backdrop
    g.fillAll(juce::Colour(0xff0a0c10));

    // Top Header Bar
    juce::Rectangle<int> headerArea(0, 0, 840, 54);
    juce::ColourGradient headerGrad(juce::Colour(0xff161a24), 0.0f, 0.0f,
                                    juce::Colour(0xff0e1118), 0.0f, 54.0f, false);
    g.setGradientFill(headerGrad);
    g.fillRect(headerArea);

    g.setColour(juce::Colour(0xff222938));
    g.drawHorizontalLine(54, 0.0f, 840.0f);

    // Title & Logo
    g.setColour(juce::Colour(0xff00e5ff));
    g.setFont(juce::FontOptions(20.0f, juce::Font::bold));
    g.drawText("AUTOLEVEL DJ", 22, 10, 160, 24, juce::Justification::left);

    // PRO badge
    juce::Rectangle<float> badgeRect(186.0f, 13.0f, 38.0f, 16.0f);
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
    juce::Rectangle<int> mbcCard(20, 236, 800, 198);
    drawCard(mbcCard, "TONE SHAPER");

    // Card 5: Parameters Panel (Bottom Full Width)
    juce::Rectangle<int> ctrlCard(20, 444, 800, 244);
    drawCard(ctrlCard, "MASTER PROCESSOR CONTROLS");

    // Master Output Peak & Limiter Gain Reduction Meter (Right of Card 5)
    float meterBoxX = 726.0f;
    float meterBoxY = 458.0f;
    float meterBoxW = 84.0f;
    float meterBoxH = 222.0f;

    juce::Rectangle<float> meterFrame(meterBoxX, meterBoxY, meterBoxW, meterBoxH);
    juce::ColourGradient meterGrad(juce::Colour(0xff121620), meterBoxX, meterBoxY,
                                  juce::Colour(0xff0b0e14), meterBoxX, meterBoxY + meterBoxH, false);
    g.setGradientFill(meterGrad);
    g.fillRoundedRectangle(meterFrame, 5.0f);
    g.setColour(juce::Colour(0xff222a38));
    g.drawRoundedRectangle(meterFrame, 5.0f, 1.0f);

    // 1. Header
    g.setColour(juce::Colour(0xff8a96a7));
    g.setFont(juce::FontOptions(10.0f, juce::Font::bold));
    g.drawText("OUT / GR", static_cast<int>(meterBoxX), static_cast<int>(meterBoxY + 5.0f),
               static_cast<int>(meterBoxW), 14, juce::Justification::centred);

    // 2. Limiter Gain Reduction Readout Badge (Displays Peak-Held Max GR, Click to Reset)
    float heldGr = m_maxHeldLimiterGrDb;
    float curGr = m_latestState.limiterGainReductionDb;
    juce::Rectangle<float> grBadge(meterBoxX + 6.0f, meterBoxY + 20.0f, meterBoxW - 12.0f, 17.0f);
    if (heldGr < -0.05f) {
        g.setColour(juce::Colour(0xff2a141b));
        g.fillRoundedRectangle(grBadge, 3.0f);
        g.setColour(juce::Colour(0xffff3366));
        g.drawRoundedRectangle(grBadge, 3.0f, 1.0f);
        g.setColour(juce::Colour(0xffff3366));
        g.setFont(juce::FontOptions(9.5f, juce::Font::bold));
        g.drawText(juce::String(heldGr, 1) + " dB", grBadge, juce::Justification::centred);
    } else {
        g.setColour(juce::Colour(0xff10141d));
        g.fillRoundedRectangle(grBadge, 3.0f);
        g.setColour(juce::Colour(0xff1e2634));
        g.drawRoundedRectangle(grBadge, 3.0f, 1.0f);
        g.setColour(juce::Colour(0xff606c7d));
        g.setFont(juce::FontOptions(9.0f, juce::Font::bold));
        g.drawText("0.0 dB GR", grBadge, juce::Justification::centred);
    }

    // 3. Meters Area: from y = 46px to 172px (height = 126px)
    float barTopY = meterBoxY + 46.0f;
    float barH = 126.0f;

    // A. Limiter GR Meter (deflects DOWNWARD from top, on identical 0 to -36 dB scale)
    float grBarX = meterBoxX + 7.0f;
    float grBarW = 11.0f;
    juce::Rectangle<float> grTrough(grBarX, barTopY, grBarW, barH);
    g.setColour(juce::Colour(0xff090b10));
    g.fillRoundedRectangle(grTrough, 2.0f);
    g.setColour(juce::Colour(0xff1a212d));
    g.drawRoundedRectangle(grTrough, 2.0f, 1.0f);

    // Downward deflection fill matching EXACT 36 dB scale
    float normLimGr = std::clamp(-curGr / 36.0f, 0.0f, 1.0f);
    if (normLimGr > 0.005f) {
        float grFillH = normLimGr * (barH - 2.0f);
        juce::Rectangle<float> grFill(grBarX + 1.0f, barTopY + 1.0f, grBarW - 2.0f, grFillH);
        juce::ColourGradient grGrad(juce::Colour(0xffffea00), grBarX, barTopY,
                                    juce::Colour(0xffff3366), grBarX, barTopY + grFillH, false);
        g.setGradientFill(grGrad);
        g.fillRoundedRectangle(grFill, 1.5f);
    }

    // Peak-held line on GR bar (shows max reduction visually on identical 36 dB scale)
    float normHeldGr = std::clamp(-heldGr / 36.0f, 0.0f, 1.0f);
    if (normHeldGr > 0.008f) {
        float heldY = barTopY + 1.0f + normHeldGr * (barH - 3.0f);
        g.setColour(juce::Colour(0xffff3366));
        g.fillRect(grBarX + 1.0f, heldY, grBarW - 2.0f, 2.0f);
    }

    // Label for GR bar below
    float labelY = barTopY + barH + 5.0f;
    g.setColour((curGr < -0.05f) ? juce::Colour(0xffff3366) : juce::Colour(0xff556272));
    g.setFont(juce::FontOptions(8.5f, juce::Font::bold));
    g.drawText("GR", static_cast<int>(grBarX - 1.0f), static_cast<int>(labelY),
               static_cast<int>(grBarW + 2.0f), 12, juce::Justification::centred);

    // B. Master Peak Meters (L & R)
    float lBarX = meterBoxX + 24.0f;
    float rBarX = meterBoxX + 36.0f;
    float peakBarW = 9.0f;

    juce::Rectangle<float> lTrough(lBarX, barTopY, peakBarW, barH);
    juce::Rectangle<float> rTrough(rBarX, barTopY, peakBarW, barH);
    g.setColour(juce::Colour(0xff090b10));
    g.fillRoundedRectangle(lTrough, 2.0f);
    g.fillRoundedRectangle(rTrough, 2.0f);
    g.setColour(juce::Colour(0xff1a212d));
    g.drawRoundedRectangle(lTrough, 2.0f, 1.0f);
    g.drawRoundedRectangle(rTrough, 2.0f, 1.0f);

    // Scale mapping (-36 dBFS to 0 dBFS) - SHARED IDENTICALLY BY GR AND OUTPUT PEAK
    auto dbToY = [barTopY, barH](float db) noexcept -> float {
        float norm = std::clamp((db + 36.0f) / 36.0f, 0.0f, 1.0f);
        return barTopY + (1.0f - norm) * barH;
    };

    auto drawPeakFill = [&g, barTopY, barH](float x, float w, float peakDb) {
        float norm = std::clamp((peakDb + 36.0f) / 36.0f, 0.0f, 1.0f);
        if (norm > 0.01f) {
            float fillH = norm * (barH - 2.0f);
            float fillY = barTopY + barH - 1.0f - fillH;
            juce::Rectangle<float> fill(x + 1.0f, fillY, w - 2.0f, fillH);

            // Three-tier color: Ice-Cyan (< -12 dB), Amber (-12 to -3 dB), Red (> -3 dB)
            juce::Colour topCol = (peakDb > -3.0f) ? juce::Colour(0xffff3366) :
                                  (peakDb > -12.0f) ? juce::Colour(0xffffb300) : juce::Colour(0xff00e5ff);
            juce::Colour botCol = juce::Colour(0xff00e5ff).withAlpha(0.6f);
            juce::ColourGradient grad(topCol, x, fillY, botCol, x, barTopY + barH, false);
            g.setGradientFill(grad);
            g.fillRoundedRectangle(fill, 1.5f);
        }
    };

    drawPeakFill(lBarX, peakBarW, m_latestState.outputPeakDbL);
    drawPeakFill(rBarX, peakBarW, m_latestState.outputPeakDbR);

    // Ceiling indicator line across L & R bars
    float ceilingDb = static_cast<float>(m_ceilingSlider.getValue());
    float ceilY = dbToY(ceilingDb);
    g.setColour(juce::Colour(0xffff3366));
    g.fillRect(lBarX - 1.0f, ceilY - 0.5f, (rBarX + peakBarW - lBarX) + 2.0f, 1.5f);

    // Labels for L & R below bars
    g.setColour(juce::Colour(0xff758394));
    g.setFont(juce::FontOptions(8.5f, juce::Font::bold));
    g.drawText("L", static_cast<int>(lBarX - 1.0f), static_cast<int>(labelY),
               static_cast<int>(peakBarW + 2.0f), 12, juce::Justification::centred);
    g.drawText("R", static_cast<int>(rBarX - 1.0f), static_cast<int>(labelY),
               static_cast<int>(peakBarW + 2.0f), 12, juce::Justification::centred);

    // C. dB Scale Ticks on Right (with intermediate -3 dB and -18 dB notches)
    for (float subT : { -3.0f, -18.0f }) {
        float subY = dbToY(subT);
        g.setColour(juce::Colour(0xff1d2533));
        g.drawHorizontalLine(static_cast<int>(std::round(subY)), rBarX + peakBarW + 2.0f, rBarX + peakBarW + 6.0f);
    }

    std::array<float, 5> ticks = { 0.0f, -6.0f, -12.0f, -24.0f, -36.0f };
    for (float t : ticks) {
        float ty = dbToY(t);
        g.setColour(juce::Colour(0xff252e3e));
        g.drawHorizontalLine(static_cast<int>(std::round(ty)), rBarX + peakBarW + 2.0f, rBarX + peakBarW + 7.0f);

        bool isZero = (std::abs(t) < 0.1f);
        bool isBottom = (std::abs(t + 36.0f) < 0.1f);

        g.setColour(isZero ? juce::Colour(0xffff3366) : juce::Colour(0xff657283));
        g.setFont(juce::FontOptions(8.5f, juce::Font::bold));
        juce::String tStr = isZero ? "0" : juce::String(static_cast<int>(t));

        float textY = isZero ? (ty - 2.0f) :
                      isBottom ? (ty - 9.0f) : (ty - 5.0f);
        g.drawText(tStr, static_cast<int>(meterBoxX + 48.0f), static_cast<int>(textY), 30, 11, juce::Justification::centredLeft);
    }

    // 4. Max Peak Numeric Readout Pod at bottom (recessed dark pod with dedicated margins)
    float maxPeakDb = std::max(m_latestState.outputPeakDbL, m_latestState.outputPeakDbR);
    juce::String peakStr = (maxPeakDb > -50.0f) ? (juce::String(maxPeakDb, 1) + " dBFS") : "---.- dBFS";
    juce::Rectangle<float> peakBadge(meterBoxX + 6.0f, meterBoxY + 195.0f, meterBoxW - 12.0f, 20.0f);
    g.setColour(juce::Colour(0xff090c12));
    g.fillRoundedRectangle(peakBadge, 3.5f);
    g.setColour((maxPeakDb >= ceilingDb - 0.1f) ? juce::Colour(0xffff3366) : juce::Colour(0xff18202d));
    g.drawRoundedRectangle(peakBadge, 3.5f, 1.0f);

    g.setColour((maxPeakDb >= ceilingDb - 0.1f) ? juce::Colour(0xffff3366) : juce::Colour(0xff00e5ff));
    g.setFont(juce::FontOptions(9.5f, juce::Font::bold));
    g.drawText(peakStr, peakBadge, juce::Justification::centred);
}

void AutoLevelDJAudioProcessorEditor::contentMouseDown(const juce::MouseEvent& e) {
    // Click on OUT / GR meter panel resets the peak-held maximum gain reduction
    juce::Rectangle<int> grClickArea(726, 458, 84, 222);
    if (grClickArea.contains(e.getPosition())) {
        m_maxHeldLimiterGrDb = m_latestState.limiterGainReductionDb; // Reset peak hold to current reduction
        m_content.repaint();
    }
}

void AutoLevelDJAudioProcessorEditor::contentMouseMove(const juce::MouseEvent& e) {
    juce::Rectangle<int> grBadgeArea(732, 478, 72, 18);
    if (grBadgeArea.contains(e.getPosition())) {
        m_content.setMouseCursor(juce::MouseCursor::PointingHandCursor);
    } else {
        m_content.setMouseCursor(juce::MouseCursor::NormalCursor);
    }
}

void AutoLevelDJAudioProcessorEditor::resized() {
    // Proportional Aspect-Ratio Vector Scaling (locked 840 x 700 aspect ratio)
    float scale = static_cast<float>(getWidth()) / 840.0f;
    m_content.setBounds(0, 0, 840, 700);
    m_content.setTransform(juce::AffineTransform::scale(scale));
}

void AutoLevelDJAudioProcessorEditor::layoutContent() {
    // Header Buttons
    m_bypassButton.setBounds(840 - 116, 13, 96, 28);
    m_resetButton.setBounds(840 - 326, 13, 200, 28);

    // Limiter lookahead switch, left of the reset button (x = 300 to 506)
    m_lookaheadLabel.setBounds(300, 17, 70, 20);
    m_lookaheadOffBtn.setBounds(372, 17, 40, 20);
    m_lookahead1Btn.setBounds(415, 17, 44, 20);
    m_lookahead2Btn.setBounds(462, 17, 44, 20);

    // Profile Card Controls
    m_pinkNoiseBtn.setBounds(530, 92, 136, 26);
    m_modernMixBtn.setBounds(674, 92, 136, 26);
    m_profileDescLabel.setBounds(524, 120, 292, 16);

    // Tone Curve Visualizer inside Profile Card
    m_toneVisualizer.setBounds(530, 138, 280, 80);

    // Breakdown Freeze button inside Card 2 (AGC Gain Correction)
    m_freezeBreakdownsButton.setBounds(288, 176, 204, 22);

    // AGC Slew Speed segmented row, centered under the freeze button (Card 2 spans 270-510)
    m_slewSpeedLabel.setBounds(306, 202, 40, 20);
    m_slewSlowBtn.setBounds(349, 202, 38, 20);
    m_slewNormalBtn.setBounds(390, 202, 44, 20);
    m_slewFastBtn.setBounds(437, 202, 38, 20);

    // MBC release mode switch inside Card 4 header (x = 392 to 580, where the Air buttons were)
    m_releaseModeLabel.setBounds(392, 242, 60, 20);
    m_releaseManualBtn.setBounds(456, 242, 64, 20);
    m_releaseAutoBtn.setBounds(524, 242, 56, 20);

    // Band EQ position switch inside Card 4 header (x = 594 to 800, where MBC Speed used to be)
    m_eqPositionLabel.setBounds(594, 242, 26, 20);
    m_eqBeforeBtn.setBounds(622, 242, 86, 20);
    m_eqAfterBtn.setBounds(712, 242, 88, 20);

    // 6-Band Meter Rack inside MBC Card, with each band's EQ fader in the right part of its column
    juce::Rectangle<int> rackBounds(26, 266, 788, 160);
    m_meterRack.setBounds(rackBounds);
    for (size_t b = 0; b < autolevel::dsp::Bands::COUNT; ++b) {
        float colX = MultibandMeterRack::columnX(b, static_cast<float>(rackBounds.getWidth()));
        m_eqSliders[b].setBounds(rackBounds.getX() + static_cast<int>(colX) + 62, rackBounds.getY() + 22, 50, 136);
    }

    // Controls: 6 columns x 2 rows in Card 5. Row 1 is the level / AGC side, row 2 the MBC side.
    int row1Y = 462;
    int row2Y = 576;
    int knobW = 92;
    int knobH = 82;
    int colSpacing = 115;
    int startX = 32;

    auto place = [&](juce::Label& label, juce::Slider& slider, int col, int rowY) {
        int cx = startX + col * colSpacing;
        label.setBounds(cx, rowY, knobW, 14);
        slider.setBounds(cx, rowY + 14, knobW, knobH);
    };

    place(m_targetLufsLabel, m_targetLufsSlider, 0, row1Y);
    place(m_maxBoostLabel, m_maxBoostSlider, 1, row1Y);
    place(m_maxCutLabel, m_maxCutSlider, 2, row1Y);
    place(m_levelResponseLabel, m_levelResponseSlider, 3, row1Y);
    place(m_postGainLabel, m_postGainSlider, 4, row1Y);
    place(m_hpfLabel, m_hpfSlider, 5, row1Y);

    place(m_compressionLabel, m_compressionSlider, 0, row2Y);
    place(m_toneSlopeLabel, m_toneSlopeSlider, 1, row2Y);
    place(m_detectorLabel, m_detectorSlider, 2, row2Y);
    place(m_attackLabel, m_attackSlider, 3, row2Y);
    place(m_releaseLabel, m_releaseSlider, 4, row2Y);
    place(m_ceilingLabel, m_ceilingSlider, 5, row2Y);
}
