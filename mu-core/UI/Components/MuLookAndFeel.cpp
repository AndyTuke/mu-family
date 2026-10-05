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
        // Modulator label colours A–H (reuse the ring/knob theme fields by intent)
        case modLabelA:               return t.rings.modA;
        case modLabelB:               return t.rings.modB;
        case modLabelC:               return t.rings.modC;
        case modLabelD:               return t.rings.modD;
        case modLabelE:               return t.knobs.euclidean;
        case modLabelF:               return t.knobs.fxSend;
        case modLabelG:               return t.knobs.prePad;
        case modLabelH:               return t.knobs.pan;
        // Sidebar tab line – runtime colour
        case sidebarTabLine:          return juce::Colours::transparentBlack;
        default:                      return juce::Colours::magenta;
    }
}

// Fixed 8-colour palette indexed by channel (rhythm in mu-clid, layer in mu-tant).
// Order matches the slot index — slot 0 is Green, slot 1 is Blue, etc. Purple is
// intentionally absent (reserved for the global / mixer accent — see
// MuTheme::Global::accent) so a channel border and a mixer border never share a hue.
const juce::Colour MuLookAndFeel::channelPalette[MuLookAndFeel::kChannelPaletteSize] = {
    juce::Colour(0xff4ADC8E),   // 0 Green
    juce::Colour(0xff378ADD),   // 1 Blue
    juce::Colour(0xffEF9F27),   // 2 Yellow / amber
    juce::Colour(0xff8B6B4A),   // 3 Brown
    juce::Colour(0xffD85A30),   // 4 Orange / coral
    juce::Colour(0xff2BB5C5),   // 5 Cyan
    juce::Colour(0xffB8B8B0),   // 6 Silver grey
    juce::Colour(0xffE24B4A),   // 7 Red (moved to last so a fresh 2-rhythm patch picks Blue, not Red)
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
// Matches drawRotarySlider below: same centre, same ring, same sweep.
MuLookAndFeel::RotaryGeometry MuLookAndFeel::getRotaryGeometry(juce::Rectangle<int> sliderBounds) const
{
    const auto  b      = sliderBounds.toFloat();
    const float outerR = juce::jmin(b.getWidth(), b.getHeight()) * 0.5f - 2.0f;
    return { b.getCentre(), outerR * kRotaryRingScale, kRotaryStartAngle, kRotaryEndAngle };
}

// Centred on the disc face, and only for a stepped control: a sweep's value is
// approximate, so its position dot reports it alone. Suppressed on the smallest knobs,
// where a legible digit would crowd the disc — their label carries the meaning instead.
void MuLookAndFeel::drawKnobValueText(juce::Graphics& g, juce::Rectangle<int> sliderBounds,
                                      const juce::String& text, bool isStepped) const
{
    const auto  b      = sliderBounds.toFloat();
    const float outerR = juce::jmin(b.getWidth(), b.getHeight()) * 0.5f - 2.0f;

    if (! isStepped || outerR < kRotaryValueMinRadius)
        return;

    const float faceR = outerR * kRotaryFaceScale;
    g.setColour(colour(valueText));
    g.setFont(juce::Font(juce::FontOptions(juce::jmax(9.0f, outerR * 0.34f), juce::Font::bold)));
    g.drawText(text, juce::Rectangle<float>(b.getCentreX() - faceR, b.getCentreY() - faceR,
                                            faceR * 2.0f, faceR * 2.0f).toNearestInt(),
               juce::Justification::centred, false);
}

// A raised disc lit from the top right, ringed by a glowing arc with a wedge removed
// at the bottom, with the value marked by a single haloed dot. The family's sweep is
// used rather than the angles passed in: every rotary in the family reads the same way
// whether or not its slider carries custom rotary parameters.
//
// Every extent is a fraction of the knob's own radius, never a fixed pixel count, so
// the glow stays inside the component at any of the four knob sizes and at any angle,
// including hard against the ends of the sweep.
int MuLookAndFeel::steppedSegments(const juce::Slider& slider) noexcept
{
    const double interval = slider.getInterval();
    if (interval <= 0.0)
        return -1;   // continuous by construction
    const double segments = (slider.getMaximum() - slider.getMinimum()) / interval;
    return segments <= (double) kMaxSteppedSegments ? (int) std::lround(juce::jmax(0.0, segments)) : -1;
}

void MuLookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int w, int h,
                                      float sliderPos, float /*startAngle*/, float /*endAngle*/,
                                      juce::Slider& slider)
{
    const float cx     = x + w * 0.5f;
    const float cy     = y + h * 0.5f;
    const float outerR = juce::jmin(w, h) * 0.5f - 2.0f;
    const float ringR  = outerR * kRotaryRingScale;
    const float faceR  = outerR * kRotaryFaceScale;
    const float angle  = kRotaryStartAngle + sliderPos * (kRotaryEndAngle - kRotaryStartAngle);

    const auto accent = slider.findColour(juce::Slider::rotarySliderFillColourId);

    // Body shadow: the whole knob casts a soft shadow down-left onto the panel.
    {
        juce::Path body;
        body.addEllipse(cx - ringR, cy - ringR, ringR * 2.0f, ringR * 2.0f);
        juce::DropShadow(juce::Colours::black.withAlpha(kKnobBodyShadowAlpha),
                         (int) juce::jmax(2.0f, outerR * 0.14f),   // ring + blur + offset stay within
                         { -(int) juce::jmax(1.0f, outerR * 0.06f), (int) juce::jmax(1.0f, outerR * 0.08f) })   // the rotary's bounds
            .drawForPath(g, body);
    }

    // Tick marks across the sweep, where there's room for them to stay distinct: one per
    // position on a stepped control (every Nth if they'd crowd), a fixed set on a smooth one.
    if (outerR >= 15.0f)
    {
        const float sweep    = kRotaryEndAngle - kRotaryStartAngle;
        const int   stepped  = steppedSegments(slider);
        const int   segments = stepped >= 0 ? stepped : kSmoothTickCount;
        int stride = 1;
        if (stepped > 0)
        {
            const int fit = juce::jmax(1, (int) (sweep * outerR / kTickMinSpacing));
            stride = (segments + fit - 1) / fit;
        }

        auto drawTick = [&](int i)
        {
            const float a = kRotaryStartAngle + (segments > 0 ? (float) i / (float) segments : 0.0f) * sweep;
            const juce::Point<float> p1(cx + outerR * 0.90f * std::sin(a), cy - outerR * 0.90f * std::cos(a));
            const juce::Point<float> p2(cx + outerR * std::sin(a),         cy - outerR * std::cos(a));
            g.drawLine({ p1, p2 }, 1.0f);
        };

        g.setColour(juce::Colours::white.withAlpha(kTickAlpha));
        for (int i = 0; i <= segments; i += stride)
            drawTick(i);
        if (segments % stride != 0)
            drawTick(segments);   // always mark the end of the range
    }

    // Disc face: cast shadow falling away from the light, then a radial gradient from
    // the lit crown down to a shadowed rim.
    juce::Path discPath;
    discPath.addEllipse(cx - faceR, cy - faceR, faceR * 2.0f, faceR * 2.0f);
    juce::DropShadow(juce::Colours::black.withAlpha(0.65f),
                     (int) juce::jmax(2.0f, outerR * 0.18f),
                     { -(int) (outerR * 0.06f), (int) (outerR * 0.09f) }).drawForPath(g, discPath);

    juce::ColourGradient faceGrad(juce::Colour(0xff3b2d47), cx + faceR * 0.42f, cy - faceR * 0.42f,
                                  juce::Colour(0xff100c16), cx - faceR * 0.75f, cy + faceR * 0.85f, true);
    faceGrad.addColour(0.35, juce::Colour(0xff2d2238));
    faceGrad.addColour(0.70, juce::Colour(0xff1b1423));
    g.setGradientFill(faceGrad);
    g.fillPath(discPath);

    // Specular and occlusion as radial gradients clipped to the disc — a stroked arc
    // has crisp sides and abrupt caps, which reads as a drawn line rather than light
    // falling across a curved surface.
    {
        const juce::Graphics::ScopedSaveState state(g);
        g.reduceClipRegion(discPath);

        const float litX = cx + faceR * 0.50f, litY = cy - faceR * 0.50f, litR = faceR * 1.05f;
        juce::ColourGradient spec(juce::Colours::white.withAlpha(0.20f), litX, litY,
                                  juce::Colours::white.withAlpha(0.0f),  litX + litR, litY, true);
        spec.addColour(0.45, juce::Colours::white.withAlpha(0.06f));
        g.setGradientFill(spec);
        g.fillEllipse(litX - litR, litY - litR, litR * 2.0f, litR * 2.0f);

        const float shX = cx - faceR * 0.45f, shY = cy + faceR * 0.52f, shR = faceR * 0.95f;
        juce::ColourGradient occl(juce::Colours::black.withAlpha(0.40f), shX, shY,
                                  juce::Colours::black.withAlpha(0.0f),  shX + shR, shY, true);
        g.setGradientFill(occl);
        g.fillEllipse(shX - shR, shY - shR, shR * 2.0f, shR * 2.0f);
    }

    // Glowing ring — soft wide passes building to a crisp core line.
    juce::Path ringPath;
    ringPath.addCentredArc(cx, cy, ringR, ringR, 0.0f, kRotaryStartAngle, kRotaryEndAngle, true);
    for (float i = 4.0f; i >= 1.0f; i -= 1.0f)
    {
        g.setColour(accent.withAlpha(0.10f));
        g.strokePath(ringPath, juce::PathStrokeType(i * outerR * 0.09f,
                                                    juce::PathStrokeType::curved,
                                                    juce::PathStrokeType::rounded));
    }
    g.setColour(accent);
    g.strokePath(ringPath, juce::PathStrokeType(juce::jmax(1.2f, outerR * 0.05f),
                                                juce::PathStrokeType::curved,
                                                juce::PathStrokeType::rounded));

    // Position dot: the value indicator, with a smooth halo lighting the ring around it.
    const float dotX = cx + ringR * std::sin(angle);
    const float dotY = cy - ringR * std::cos(angle);
    const float dotR = juce::jmax(2.0f, outerR * 0.065f);

    const float haloR = dotR * 2.6f;
    juce::ColourGradient halo(accent.withAlpha(0.55f), dotX, dotY,
                              accent.withAlpha(0.0f),  dotX + haloR, dotY, true);
    halo.addColour(0.35, accent.withAlpha(0.26f));
    g.setGradientFill(halo);
    g.fillEllipse(dotX - haloR, dotY - haloR, haloR * 2.0f, haloR * 2.0f);

    const float bloomR = dotR * 1.8f;
    juce::ColourGradient bloom(juce::Colours::white.withAlpha(0.40f), dotX, dotY,
                               juce::Colours::white.withAlpha(0.0f),  dotX + bloomR, dotY, true);
    g.setGradientFill(bloom);
    g.fillEllipse(dotX - bloomR, dotY - bloomR, bloomR * 2.0f, bloomR * 2.0f);

    g.setColour(juce::Colours::white);
    g.fillEllipse(dotX - dotR, dotY - dotR, dotR * 2.0f, dotR * 2.0f);
}

// Slide switch in the knob's language: a pill sunk into the panel, a small raised disc
// in the knob-face purple, and the accent on the disc's ring and the active label.
void MuLookAndFeel::drawSlideSwitch(juce::Graphics& g, juce::Rectangle<float> bounds,
                                    float position, juce::Colour accent,
                                    const juce::String& topLabel, const juce::String& bottomLabel,
                                    int selected, bool highlighted)
{
    using mu_ui::sf;

    const float tw = sf(kSlideSwitchTrackW);
    const juce::Rectangle<float> tr(bounds.getX() + sf(2.0f), bounds.getY() + sf(3.0f),
                                    tw, bounds.getHeight() - sf(6.0f));
    const float thR  = tw * 0.5f + sf(1.0f);   // disc sits slightly proud of the track
    const float yTop = tr.getY() + tw * 0.5f;
    const float yBot = tr.getBottom() - tw * 0.5f;
    const float cx   = tr.getCentreX();
    const float cy   = yTop + position * (yBot - yTop);

    // Track: darkest at the top lip, with a faint highlight where light catches the
    // lower edge, plus a thin accent line so it reads as part of the same control.
    juce::Path track;
    track.addRoundedRectangle(tr, tw * 0.5f);
    g.setGradientFill(juce::ColourGradient(juce::Colour(0xff07050a), cx, tr.getY(),
                                           juce::Colour(0xff17121d), cx, tr.getBottom(), false));
    g.fillPath(track);
    g.setColour(juce::Colours::black.withAlpha(0.6f));
    g.strokePath(track, juce::PathStrokeType(1.0f));
    g.setColour(juce::Colours::white.withAlpha(0.07f));
    g.drawLine(cx - tw * 0.3f, tr.getBottom() + 0.5f, cx + tw * 0.3f, tr.getBottom() + 0.5f, 1.0f);
    g.setColour(accent.withAlpha(0.18f));
    g.drawLine(cx, yTop, cx, yBot, juce::jmax(1.0f, tw * 0.18f));

    // Disc: cast shadow, then the knob face's radial gradient lit from the top-right.
    juce::Path disc;
    disc.addEllipse(cx - thR, cy - thR, thR * 2.0f, thR * 2.0f);
    juce::DropShadow(juce::Colours::black.withAlpha(0.8f), (int) juce::jmax(2.0f, thR * 0.7f),
                     { -(int) juce::jmax(1.0f, thR * 0.3f), (int) juce::jmax(1.0f, thR * 0.4f) })
        .drawForPath(g, disc);   // falls down-left, away from the top-right light

    juce::ColourGradient face(juce::Colour(0xff4a3a58), cx + thR * 0.45f, cy - thR * 0.45f,
                              juce::Colour(0xff100c16), cx - thR * 0.8f,  cy + thR * 0.9f, true);
    face.addColour(0.4, juce::Colour(0xff2d2238));
    g.setGradientFill(face);
    g.fillPath(disc);

    // Accent ring on the disc: soft halo passes building to a crisp core, as on the knob.
    const float ringR = thR * 0.78f;
    juce::Path ring;
    ring.addEllipse(cx - ringR, cy - ringR, ringR * 2.0f, ringR * 2.0f);
    const float glow = highlighted ? 0.22f : 0.14f;
    for (float i = 3.0f; i >= 1.0f; i -= 1.0f)
    {
        g.setColour(accent.withAlpha(glow));
        g.strokePath(ring, juce::PathStrokeType(i * thR * 0.22f));
    }
    g.setColour(accent);
    g.strokePath(ring, juce::PathStrokeType(juce::jmax(1.0f, thR * 0.14f)));

    // Labels level with each end: the selected one in the accent, the other dimmed.
    g.setFont(juce::Font(juce::FontOptions{}.withHeight(sf(kKnobLabelFont))));
    const float lx = tr.getRight() + sf(5.0f);
    const float lw = bounds.getRight() - lx;
    const float lh = sf(12.0f);
    const auto  dim = colour(labelText).withAlpha(0.55f);

    g.setColour(selected == 0 ? accent : dim);
    g.drawText(topLabel, juce::Rectangle<float>(lx, yTop - lh * 0.5f, lw, lh),
               juce::Justification::centredLeft, false);
    g.setColour(selected == 1 ? accent : dim);
    g.drawText(bottomLabel, juce::Rectangle<float>(lx, yBot - lh * 0.5f, lw, lh),
               juce::Justification::centredLeft, false);
}

void MuLookAndFeel::drawAccentPanel(juce::Graphics& g, juce::Rectangle<float> r,
                                    juce::Colour accent, float cornerSize)
{
    juce::Path shape;
    shape.addRoundedRectangle(r, cornerSize);

    // Wash: the accent at a whisper over whatever the panel sits on.
    g.setColour(accent.withAlpha(kPanelTintAlpha));
    g.fillPath(shape);

    // Highlight: a radial glow anchored on the top-right corner, clipped to the panel.
    {
        const juce::Graphics::ScopedSaveState state(g);
        g.reduceClipRegion(shape);
        const auto  corner = r.getTopRight();
        const float reach  = juce::jmin(std::hypot(r.getWidth(), r.getHeight()) * kPanelHighlightReach,
                                        mu_ui::sf(kPanelHighlightMaxPx));
        juce::ColourGradient glow(accent.withAlpha(kPanelHighlightAlpha), corner.x, corner.y,
                                  accent.withAlpha(0.0f), corner.x - reach, corner.y, true);
        glow.addColour(0.4, accent.withAlpha(kPanelHighlightAlpha * 0.35f));
        g.setGradientFill(glow);
        g.fillRect(r);
    }

    g.setColour(accent);
    g.strokePath(shape, juce::PathStrokeType(kPanelOutlineWidth));
}

void MuLookAndFeel::drawButtonBackground(juce::Graphics& g, juce::Button& button,
                                          const juce::Colour& bg,
                                          bool isOver, bool isDown)
{
    auto bounds = button.getLocalBounds().toFloat().reduced(0.5f);
    const bool on = button.getToggleState();

    // Use bg directly — JUCE passes button.findColour(buttonColourId or buttonOnColourId),
    // so per-button setColour() calls (e.g. the transport play button's state colours) are
    // respected here rather than overridden by hardcoded segment colours.
    auto bgColour = bg;
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
    const bool on = button.getToggleState();
    // Respect per-button text colour overrides (e.g. playBtn.setColour(textColourOffId, ...)).
    g.setColour(on ? button.findColour(juce::TextButton::textColourOnId)
                   : button.findColour(juce::TextButton::textColourOffId));
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
    // Fill background — needed for labels used as coloured pills/banners
    // (e.g. the preset hot-swap "SWP" badge, demo-mode banner). JUCE default
    // for backgroundColourId is transparentBlack, so this is a no-op for the
    // typical text-only label.
    g.fillAll(label.findColour(juce::Label::backgroundColourId));

    if (!label.isBeingEdited())
    {
        g.setColour(label.findColour(juce::Label::textColourId));
        g.setFont(label.getFont());
        g.drawFittedText(label.getText(), label.getLocalBounds().reduced(2, 0),
                         label.getJustificationType(), 1, 1.0f);
    }
}
