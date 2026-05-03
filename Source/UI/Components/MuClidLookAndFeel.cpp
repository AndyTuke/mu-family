#include "MuClidLookAndFeel.h"

juce::Colour MuClidLookAndFeel::colour(ColourIds id) noexcept
{
    switch (id)
    {
        // Backgrounds
        case windowBackground:        return juce::Colour(0xff1c1c1b);
        case panelBackground:         return juce::Colour(0xff232322);
        case sidebarBackground:       return juce::Colour(0xff1a1a19);
        case sidebarItemBackground:   return juce::Colour(0xff252524);
        case sidebarItemSelected:     return juce::Colour(0xff2d2d2b);
        case overlayBackground:       return juce::Colour(0xff111110);
        // Knob categories
        case knobEuclidean:           return juce::Colour(0xff7F77DD);
        case knobPadding:             return juce::Colour(0xff1D9E75);
        case knobInsertPad:           return juce::Colour(0xffD4537E);
        case knobLevel:               return juce::Colour(0xffEF9F27);
        case knobFxSend:              return juce::Colour(0xffD85A30);
        case knobReverb:              return juce::Colour(0xff378ADD);
        case knobPan:                 return juce::Colour(0xff888780);
        case knobModulation:          return juce::Colour(0xffD4537E);
        case knobPrePad:              return juce::Colour(0xff2BB5C5);
        case knobPostPad:             return juce::Colour(0xff1D9E75);
        // Rings
        case ringEuclidA:             return juce::Colour(0xff7F77DD);
        case ringEuclidB:             return juce::Colour(0xffD85A30);
        case ringEuclidC:             return juce::Colour(0xffEF9F27);
        case ringModA:                return juce::Colour(0xff1D9E75);
        case ringModB:                return juce::Colour(0xffEF9F27);
        case ringModC:                return juce::Colour(0xffD4537E);
        case ringModD:                return juce::Colour(0xff378ADD);
        case ringInactive:            return juce::Colour(0xff333332);
        case ringPrePad:              return juce::Colour(0xff2BB5C5);
        case ringPostPad:             return juce::Colour(0xff1D9E75);
        case ringInsertPad:           return juce::Colour(0xffD4537E);
        // Segment control
        case segmentActiveBg:         return juce::Colour(0xff3C3489);
        case segmentActiveBorder:     return juce::Colour(0xff7F77DD);
        case segmentPositiveBg:       return juce::Colour(0xff085041);
        case segmentPositiveBorder:   return juce::Colour(0xff1D9E75);
        case segmentWarningBg:        return juce::Colour(0xff854F0B);
        case segmentWarningBorder:    return juce::Colour(0xffEF9F27);
        case segmentInactiveBg:       return juce::Colour(0xff2a2a2a);
        case segmentInactiveBorder:   return juce::Colour(0xff444444);
        case segmentInactiveText:     return juce::Colour(0xff888888);
        // StepEditor
        case stepEditorBar:           return juce::Colour(0xff1D9E75);
        case stepEditorZeroLine:      return juce::Colour(0xff555554);
        case stepEditorBackground:    return juce::Colour(0xff1e1e1d);
        case stepEditorGridLine:      return juce::Colour(0xff333332);
        // LFOEditor
        case lfoEditorBackground:     return juce::Colour(0xff1e1e1d);
        case lfoEditorCurve:          return juce::Colour(0xff1D9E75);
        case lfoEditorCurveFill:      return juce::Colour(0x301D9E75);
        case lfoEditorPoint:          return juce::Colour(0xffffffff);
        case lfoEditorPointHover:     return juce::Colour(0xff7F77DD);
        case lfoEditorHandle:         return juce::Colour(0xff888780);
        case lfoEditorZeroLine:       return juce::Colour(0xff444444);
        case lfoEditorPlayhead:       return juce::Colour(0xffD4537E);
        // VU meter
        case vuMeterLow:              return juce::Colour(0xff1D9E75);
        case vuMeterMid:              return juce::Colour(0xffEF9F27);
        case vuMeterClip:             return juce::Colour(0xffE24B4A);
        case vuMeterPeakHold:         return juce::Colour(0xffffffff);
        case vuMeterBackground:       return juce::Colour(0xff111110);
        // Sample bar
        case sampleBarNoSample:       return juce::Colour(0xff444444);
        case sampleBarLoaded:         return juce::Colour(0xff999999);
        case sampleBarMissing:        return juce::Colour(0xffEF9F27);
        case sampleBarBackground:     return juce::Colour(0xff1a1a19);
        // Status bar
        case statusBarBackground:     return juce::Colour(0xff141413);
        case statusBarText:           return juce::Colour(0xff888780);
        case statusBarValue:          return juce::Colour(0xffcccccc);
        // General text
        case labelText:               return juce::Colour(0xff888780);
        case valueText:               return juce::Colour(0xffcccccc);
        case headingText:             return juce::Colour(0xffe8e8e6);
        case mutedText:               return juce::Colour(0xff555554);
        // Buttons
        case addButtonBorder:         return juce::Colour(0xff555554);
        case addButtonText:           return juce::Colour(0xff888780);
        case addButtonHoverBg:        return juce::Colour(0xff2a2a28);
        // Sidebar tab line – runtime colour
        case sidebarTabLine:          return juce::Colours::transparentBlack;
        default:                      return juce::Colours::magenta;
    }
}

const juce::Colour MuClidLookAndFeel::rhythmPalette[30] = {
    juce::Colour(0xff7F77DD), juce::Colour(0xff1D9E75), juce::Colour(0xffD4537E),
    juce::Colour(0xffEF9F27), juce::Colour(0xffD85A30), juce::Colour(0xff378ADD),
    juce::Colour(0xffE24B4A), juce::Colour(0xff56C4A0), juce::Colour(0xffA36BC9),
    juce::Colour(0xffF4C842), juce::Colour(0xff4E9FD9), juce::Colour(0xffE87B51),
    juce::Colour(0xff6DD87A), juce::Colour(0xffC46B8E), juce::Colour(0xffAAA3E8),
    juce::Colour(0xff52B8E0), juce::Colour(0xffF08060), juce::Colour(0xff7ECF60),
    juce::Colour(0xffD07AAA), juce::Colour(0xffFADA6A), juce::Colour(0xff5AADCF),
    juce::Colour(0xffEB9040), juce::Colour(0xff88D888), juce::Colour(0xffB880C0),
    juce::Colour(0xffC8C068), juce::Colour(0xff7088C8), juce::Colour(0xffE86868),
    juce::Colour(0xff60C898), juce::Colour(0xffE898B8), juce::Colour(0xff98A8D8),
};

MuClidLookAndFeel::MuClidLookAndFeel()
{
    setColour(juce::ResizableWindow::backgroundColourId, colour(windowBackground));
    setColour(juce::Slider::rotarySliderFillColourId,    colour(knobEuclidean));
    setColour(juce::Slider::rotarySliderOutlineColourId, colour(segmentInactiveBorder));
    setColour(juce::Slider::thumbColourId,               colour(valueText));
    setColour(juce::TextButton::buttonColourId,          colour(segmentInactiveBg));
    setColour(juce::TextButton::buttonOnColourId,        colour(segmentActiveBg));
    setColour(juce::TextButton::textColourOffId,         colour(labelText));
    setColour(juce::TextButton::textColourOnId,          colour(segmentActiveBorder));
    setColour(juce::ComboBox::backgroundColourId,        colour(segmentInactiveBg));
    setColour(juce::ComboBox::outlineColourId,           colour(segmentInactiveBorder));
    setColour(juce::ComboBox::textColourId,              colour(valueText));
    setColour(juce::ComboBox::arrowColourId,             colour(labelText));
    setColour(juce::Label::textColourId,                 colour(labelText));
    setColour(juce::PopupMenu::backgroundColourId,       colour(panelBackground));
    setColour(juce::PopupMenu::textColourId,             colour(valueText));
    setColour(juce::PopupMenu::highlightedBackgroundColourId, colour(segmentActiveBg));
    setColour(juce::PopupMenu::highlightedTextColourId,  colour(segmentActiveBorder));
    setColour(juce::ScrollBar::thumbColourId,            colour(segmentInactiveBorder));
    setColour(juce::TextEditor::backgroundColourId,      colour(segmentInactiveBg));
    setColour(juce::TextEditor::outlineColourId,         colour(segmentInactiveBorder));
    setColour(juce::TextEditor::focusedOutlineColourId,  colour(knobEuclidean));
    setColour(juce::TextEditor::textColourId,            colour(valueText));
    setColour(juce::CaretComponent::caretColourId,       colour(valueText));
}

//==============================================================================
void MuClidLookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int w, int h,
                                          float sliderPos, float startAngle, float endAngle,
                                          juce::Slider& slider)
{
    const float cx = x + w * 0.5f;
    const float cy = y + h * 0.5f;
    const float radius = juce::jmin(w, h) * 0.5f - 2.0f;
    const float trackWidth = juce::jmax(2.0f, radius * 0.12f);
    const float angle = startAngle + sliderPos * (endAngle - startAngle);

    auto trackColour  = slider.findColour(juce::Slider::rotarySliderOutlineColourId);
    auto fillColour   = slider.findColour(juce::Slider::rotarySliderFillColourId);

    // Background track
    juce::Path bg;
    bg.addCentredArc(cx, cy, radius, radius, 0.0f, startAngle, endAngle, true);
    g.setColour(trackColour);
    g.strokePath(bg, juce::PathStrokeType(trackWidth, juce::PathStrokeType::curved,
                                           juce::PathStrokeType::rounded));

    // Filled arc
    juce::Path arc;
    arc.addCentredArc(cx, cy, radius, radius, 0.0f, startAngle, angle, true);
    g.setColour(fillColour);
    g.strokePath(arc, juce::PathStrokeType(trackWidth, juce::PathStrokeType::curved,
                                            juce::PathStrokeType::rounded));

    // Centre dot
    const float dotRadius = juce::jmax(2.0f, radius * 0.12f);
    g.setColour(fillColour);
    g.fillEllipse(cx - dotRadius, cy - dotRadius, dotRadius * 2.0f, dotRadius * 2.0f);

    // Pointer line
    const float pointerLength = radius * 0.6f;
    const float pointerWidth  = juce::jmax(1.5f, radius * 0.06f);
    juce::Path pointer;
    pointer.startNewSubPath(0.0f, -radius);
    pointer.lineTo(0.0f, -(radius - pointerLength));
    auto xf = juce::AffineTransform::rotation(angle).translated(cx, cy);
    g.setColour(fillColour.brighter(0.3f));
    g.strokePath(pointer, juce::PathStrokeType(pointerWidth), xf);
}

void MuClidLookAndFeel::drawButtonBackground(juce::Graphics& g, juce::Button& button,
                                              const juce::Colour& /*bg*/,
                                              bool isOver, bool isDown)
{
    auto bounds = button.getLocalBounds().toFloat().reduced(0.5f);
    bool on = button.getToggleState();

    auto bgColour = on ? colour(segmentActiveBg) : colour(segmentInactiveBg);
    if (isDown) bgColour = bgColour.brighter(0.15f);
    else if (isOver) bgColour = bgColour.brighter(0.08f);

    auto borderColour = on ? colour(segmentActiveBorder) : colour(segmentInactiveBorder);

    g.setColour(bgColour);
    g.fillRoundedRectangle(bounds, 3.0f);
    g.setColour(borderColour);
    g.drawRoundedRectangle(bounds, 3.0f, 1.0f);
}

void MuClidLookAndFeel::drawButtonText(juce::Graphics& g, juce::TextButton& button,
                                        bool /*isOver*/, bool /*isDown*/)
{
    bool on = button.getToggleState();
    g.setColour(on ? colour(segmentActiveBorder) : colour(segmentInactiveText));
    g.setFont(juce::Font(juce::FontOptions{}.withHeight(12.0f)));
    g.drawText(button.getButtonText(), button.getLocalBounds(),
               juce::Justification::centred, true);
}

void MuClidLookAndFeel::drawComboBox(juce::Graphics& g, int w, int h, bool /*isDown*/,
                                      int /*bx*/, int /*by*/, int /*bw*/, int /*bh*/,
                                      juce::ComboBox& box)
{
    auto bounds = juce::Rectangle<float>(0, 0, (float)w, (float)h).reduced(0.5f);
    g.setColour(colour(segmentInactiveBg));
    g.fillRoundedRectangle(bounds, 3.0f);
    g.setColour(colour(segmentInactiveBorder));
    g.drawRoundedRectangle(bounds, 3.0f, 1.0f);

    // Arrow
    const float arrowSize = h * 0.35f;
    const float arrowX = w - arrowSize * 1.5f;
    const float arrowY = (h - arrowSize * 0.6f) * 0.5f;
    juce::Path arrow;
    arrow.startNewSubPath(arrowX, arrowY);
    arrow.lineTo(arrowX + arrowSize, arrowY);
    arrow.lineTo(arrowX + arrowSize * 0.5f, arrowY + arrowSize * 0.6f);
    arrow.closeSubPath();
    g.setColour(colour(labelText));
    g.fillPath(arrow);
}

void MuClidLookAndFeel::positionComboBoxText(juce::ComboBox& box, juce::Label& label)
{
    label.setBounds(6, 0, box.getWidth() - 24, box.getHeight());
    label.setFont(juce::Font(juce::FontOptions{}.withHeight(12.0f)));
}

void MuClidLookAndFeel::drawLabel(juce::Graphics& g, juce::Label& label)
{
    if (!label.isBeingEdited())
    {
        g.setColour(label.findColour(juce::Label::textColourId));
        g.setFont(label.getFont());
        g.drawFittedText(label.getText(), label.getLocalBounds().reduced(2, 0),
                         label.getJustificationType(), 1, 1.0f);
    }
}
