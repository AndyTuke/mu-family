#include "MuLookAndFeel.h"
#include "MuTheme.h"

// every colour now lives in the MuTheme singleton (grouped by category for
// the future Theme Editor). This switch just maps the legacy ColourIds tokens to
// the corresponding Theme field, so every existing call site keeps working with
// no change while a Theme Editor can edit values from one place.
juce::Colour MuLookAndFeel::colour(ColourIds id) noexcept
{
    const auto& t = MuTheme::current();
    switch (id)
    {
        // Backgrounds
        case windowBackground:        return t.backgrounds.window;
        case panelBackground:         return t.backgrounds.panel;
        case sidebarBackground:       return t.backgrounds.sidebar;
        case sidebarItemBackground:   return t.backgrounds.sidebarItem;
        case sidebarItemSelected:     return t.backgrounds.sidebarItemSelected;
        case overlayBackground:       return t.backgrounds.overlay;
        case backgroundDialog:        return t.backgrounds.dialog;
        case backgroundModalDim:      return t.backgrounds.modalDim;
        case backgroundFxRowDim:      return t.backgrounds.fxRowDim;
        case backgroundMixerStripDim: return t.backgrounds.mixerStripDim;
        // Knob categories
        case knobEuclidean:           return t.knobs.euclidean;
        case knobPadding:             return t.knobs.postPad;       // legacy alias
        case knobInsertPad:           return t.knobs.insertPad;
        case knobLevel:               return t.knobs.level;
        case knobFxSend:              return t.knobs.fxSend;
        case knobReverb:              return t.knobs.reverb;
        case knobPan:                 return t.knobs.pan;
        case knobModulation:          return t.knobs.modulation;
        case knobPrePad:              return t.knobs.prePad;
        case knobPostPad:             return t.knobs.postPad;
        // Rings
        case ringEuclidA:             return t.rings.euclidA;
        case ringEuclidB:             return t.rings.euclidB;
        case ringEuclidC:             return t.rings.euclidC;
        case ringModA:                return t.rings.modA;
        case ringModB:                return t.rings.modB;
        case ringModC:                return t.rings.modC;
        case ringModD:                return t.rings.modD;
        case ringInactive:            return t.rings.inactive;
        case ringPrePad:              return t.rings.prePad;
        case ringPostPad:             return t.rings.postPad;
        case ringInsertPad:           return t.rings.insertPad;
        // Segment control
        case segmentActiveBg:         return t.segments.activeBg;
        case segmentActiveBorder:     return t.segments.activeBorder;
        case segmentPositiveBg:       return t.segments.positiveBg;
        case segmentPositiveBorder:   return t.segments.positiveBorder;
        case segmentWarningBg:        return t.segments.warningBg;
        case segmentWarningBorder:    return t.segments.warningBorder;
        case segmentInactiveBg:       return t.segments.inactiveBg;
        case segmentInactiveBorder:   return t.segments.inactiveBorder;
        case segmentInactiveText:     return t.segments.inactiveText;
        // StepEditor
        case stepEditorBar:           return t.stepEditor.bar;
        case stepEditorZeroLine:      return t.stepEditor.zeroLine;
        case stepEditorBackground:    return t.stepEditor.background;
        case stepEditorGridLine:      return t.stepEditor.gridLine;
        // LFOEditor
        case lfoEditorBackground:     return t.lfoEditor.background;
        case lfoEditorCurve:          return t.lfoEditor.curve;
        case lfoEditorCurveFill:      return t.lfoEditor.curveFill;
        case lfoEditorPoint:          return t.lfoEditor.point;
        case lfoEditorPointHover:     return t.lfoEditor.pointHover;
        case lfoEditorHandle:         return t.lfoEditor.handle;
        case lfoEditorZeroLine:       return t.lfoEditor.zeroLine;
        case lfoEditorPlayhead:       return t.lfoEditor.playhead;
        // VU meter (legacy tokens kept for back-compat — map to closest Theme zone)
        case vuMeterLow:              return t.vuMeter.green;
        case vuMeterMid:              return t.vuMeter.yellow;
        case vuMeterClip:             return t.vuMeter.red;
        case vuMeterPeakHold:         return t.vuMeter.peakHold;
        case vuMeterBackground:       return t.vuMeter.background;
        case vuMeterGreen:            return t.vuMeter.green;
        case vuMeterYellow:           return t.vuMeter.yellow;
        case vuMeterRed:              return t.vuMeter.red;
        case vuMeterClipFlash:        return t.vuMeter.clipFlash;
        // Sample bar
        case sampleBarNoSample:       return t.sampleBar.noSample;
        case sampleBarLoaded:         return t.sampleBar.loaded;
        case sampleBarMissing:        return t.sampleBar.missing;
        case sampleBarBackground:     return t.sampleBar.background;
        case sampleBarMissingWarning: return t.sampleBar.missingWarning;
        // Status bar
        case statusBarBackground:     return t.statusBar.background;
        case statusBarText:           return t.statusBar.text;
        case statusBarValue:          return t.statusBar.value;
        // General text
        case labelText:               return t.text.label;
        case valueText:               return t.text.value;
        case headingText:             return t.text.heading;
        case mutedText:               return t.text.muted;
        case textBright:              return t.text.bright;
        case textDisabledButton:      return t.text.disabledButton;
        // Buttons
        case addButtonBorder:         return t.buttons.addBorder;
        case addButtonText:           return t.buttons.addText;
        case addButtonHoverBg:        return t.buttons.addHoverBg;
        // Transport tinted backgrounds
        case transportWhileStoppedBg: return t.transport.whileStoppedBg;
        case transportWhilePlayingBg: return t.transport.whilePlayingBg;
        // Knob overlay indicators
        case indicatorModulationTint: return t.indicators.modulationTint;
        case indicatorGRTint:         return t.indicators.grTint;
        case indicatorGRMeterBg:      return t.indicators.grMeterBg;
        case indicatorGRMeterBar:     return t.indicators.grMeterBar;
        // Mixer extras
        case mixerInactiveNameBg:     return t.mixer.inactiveNameBg;
        // Global / non-rhythm accent (mixer borders, etc.)
        case globalAccent:            return t.global.accent;
        // Sidebar tab line – runtime colour
        case sidebarTabLine:          return juce::Colours::transparentBlack;
        default:                      return juce::Colours::magenta;
    }
}

// Fixed 8-colour rhythm palette. Order matches the rhythm slot index — slot 0
// is Green, slot 1 is Red etc. Purple is intentionally absent (reserved for
// the global / mixer accent — see MuTheme::Global::accent) so a rhythm border
// and a mixer border never share a hue.
const juce::Colour MuLookAndFeel::rhythmPalette[MuLookAndFeel::kRhythmPaletteSize] = {
    juce::Colour(0xff4ADC8E),   // 0 Green
    juce::Colour(0xffE24B4A),   // 1 Red
    juce::Colour(0xff378ADD),   // 2 Blue
    juce::Colour(0xffEF9F27),   // 3 Yellow / amber
    juce::Colour(0xff8B6B4A),   // 4 Brown
    juce::Colour(0xffD85A30),   // 5 Orange / coral
    juce::Colour(0xff2BB5C5),   // 6 Cyan
    juce::Colour(0xffB8B8B0),   // 7 Silver grey
};

MuLookAndFeel::MuLookAndFeel()
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
void MuLookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int w, int h,
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

void MuLookAndFeel::drawButtonBackground(juce::Graphics& g, juce::Button& button,
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

void MuLookAndFeel::drawButtonText(juce::Graphics& g, juce::TextButton& button,
                                    bool /*isOver*/, bool /*isDown*/)
{
    bool on = button.getToggleState();
    g.setColour(on ? colour(segmentActiveBorder) : colour(segmentInactiveText));
    g.setFont(juce::Font(juce::FontOptions{}.withHeight(12.0f)));
    g.drawText(button.getButtonText(), button.getLocalBounds(),
               juce::Justification::centred, true);
}

void MuLookAndFeel::drawComboBox(juce::Graphics& g, int w, int h, bool /*isDown*/,
                                  int /*bx*/, int /*by*/, int /*bw*/, int /*bh*/,
                                  juce::ComboBox& /*box*/)
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

void MuLookAndFeel::positionComboBoxText(juce::ComboBox& box, juce::Label& label)
{
    label.setBounds(6, 0, box.getWidth() - 24, box.getHeight());
    label.setFont(juce::Font(juce::FontOptions{}.withHeight(12.0f)));
}

void MuLookAndFeel::drawLabel(juce::Graphics& g, juce::Label& label)
{
    if (!label.isBeingEdited())
    {
        g.setColour(label.findColour(juce::Label::textColourId));
        g.setFont(label.getFont());
        g.drawFittedText(label.getText(), label.getLocalBounds().reduced(2, 0),
                         label.getJustificationType(), 1, 1.0f);
    }
}
