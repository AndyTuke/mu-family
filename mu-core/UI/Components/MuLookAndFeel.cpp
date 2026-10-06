#include "MuLookAndFeel.h"
#include <map>
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
void MuLookAndFeel::drawKnobCastShadow(juce::Graphics& g, juce::Point<float> centre, float ringRadius)
{
    const auto& L = lighting();
    juce::Path body;
    body.addEllipse(centre.x - ringRadius, centre.y - ringRadius, ringRadius * 2.0f, ringRadius * 2.0f);
    const int off = (int) juce::jmax(2.0f, ringRadius * L.knobCastOffset);
    juce::DropShadow(juce::Colours::black.withAlpha(L.shadow(L.knobCastShadow)),
                     (int) juce::jmax(4.0f, ringRadius * L.knobCastBlur), { -off, off + off / 3 })
        .drawForPath(g, body);
}

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
    const auto& L = lighting();

    // Body shadow: the whole knob casts a soft shadow down-left onto the panel.
    {
        juce::Path body;
        body.addEllipse(cx - ringR, cy - ringR, ringR * 2.0f, ringR * 2.0f);
        juce::DropShadow(juce::Colours::black.withAlpha(L.shadow(L.knobBodyShadow)),
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

        g.setColour(juce::Colours::white.withAlpha(L.knobTicks));
        for (int i = 0; i <= segments; i += stride)
            drawTick(i);
        if (segments % stride != 0)
            drawTick(segments);   // always mark the end of the range
    }

    // Disc face: cast shadow falling away from the light, then a radial gradient from
    // the lit crown down to a shadowed rim.
    juce::Path discPath;
    discPath.addEllipse(cx - faceR, cy - faceR, faceR * 2.0f, faceR * 2.0f);
    juce::DropShadow(juce::Colours::black.withAlpha(L.shadow(L.knobDiscShadow)),
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
        juce::ColourGradient spec(juce::Colours::white.withAlpha(L.highlight(L.knobSpecular)), litX, litY,
                                  juce::Colours::white.withAlpha(0.0f),  litX + litR, litY, true);
        spec.addColour(0.45, juce::Colours::white.withAlpha(L.highlight(L.knobSpecularMid)));
        g.setGradientFill(spec);
        g.fillEllipse(litX - litR, litY - litR, litR * 2.0f, litR * 2.0f);

        const float shX = cx - faceR * 0.45f, shY = cy + faceR * 0.52f, shR = faceR * 0.95f;
        juce::ColourGradient occl(juce::Colours::black.withAlpha(L.shadow(L.knobOcclusion)), shX, shY,
                                  juce::Colours::black.withAlpha(0.0f),  shX + shR, shY, true);
        g.setGradientFill(occl);
        g.fillEllipse(shX - shR, shY - shR, shR * 2.0f, shR * 2.0f);
    }

    // Glowing ring — soft wide passes building to a crisp core line.
    juce::Path ringPath;
    ringPath.addCentredArc(cx, cy, ringR, ringR, 0.0f, kRotaryStartAngle, kRotaryEndAngle, true);
    for (float i = 4.0f; i >= 1.0f; i -= 1.0f)
    {
        g.setColour(accent.withAlpha(L.highlight(L.knobRingGlow)));
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
    juce::ColourGradient halo(accent.withAlpha(L.highlight(L.knobDotHalo)), dotX, dotY,
                              accent.withAlpha(0.0f),  dotX + haloR, dotY, true);
    halo.addColour(0.35, accent.withAlpha(L.highlight(L.knobDotHaloMid)));
    g.setGradientFill(halo);
    g.fillEllipse(dotX - haloR, dotY - haloR, haloR * 2.0f, haloR * 2.0f);

    const float bloomR = dotR * 1.8f;
    juce::ColourGradient bloom(juce::Colours::white.withAlpha(L.highlight(L.knobDotBloom)), dotX, dotY,
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
    const auto& L = lighting();

    const float tw = sf(kSlideSwitchTrackW);
    // Track inset from the left so the disc's down-left shadow has room on the panel.
    const juce::Rectangle<float> tr(bounds.getX() + sf(4.0f), bounds.getY() + sf(3.0f),
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
    g.setColour(juce::Colours::black.withAlpha(L.shadow(L.switchTrackShadow)));
    g.strokePath(track, juce::PathStrokeType(1.0f));
    g.setColour(juce::Colours::white.withAlpha(L.highlight(L.switchTrackLip)));
    g.drawLine(cx - tw * 0.3f, tr.getBottom() + 0.5f, cx + tw * 0.3f, tr.getBottom() + 0.5f, 1.0f);
    g.setColour(accent.withAlpha(L.highlight(L.switchTrackGlow)));
    g.drawLine(cx, yTop, cx, yBot, juce::jmax(1.0f, tw * 0.18f));

    // Disc: cast shadow, then the knob face's radial gradient lit from the top-right.
    juce::Path disc;
    disc.addEllipse(cx - thR, cy - thR, thR * 2.0f, thR * 2.0f);
    // Cast shadow, falling down-left away from the top-right light: a soft outer shadow
    // that lands on the panel beside the near-black track, plus a crisp contact shadow.
    juce::DropShadow(juce::Colours::black.withAlpha(L.shadow(L.switchShadow)), (int) juce::jmax(3.0f, thR * 0.75f),
                     { -(int) juce::jmax(2.0f, thR * 0.45f), (int) juce::jmax(2.0f, thR * 0.6f) })
        .drawForPath(g, disc);
    juce::DropShadow(juce::Colours::black.withAlpha(L.shadow(L.switchContact)), (int) juce::jmax(2.0f, thR * 0.3f),
                     { -(int) juce::jmax(1.0f, thR * 0.25f), (int) juce::jmax(1.0f, thR * 0.35f) })
        .drawForPath(g, disc);

    juce::ColourGradient face(juce::Colour(0xff4a3a58), cx + thR * 0.45f, cy - thR * 0.45f,
                              juce::Colour(0xff100c16), cx - thR * 0.8f,  cy + thR * 0.9f, true);
    face.addColour(0.4, juce::Colour(0xff2d2238));
    g.setGradientFill(face);
    g.fillPath(disc);

    // Accent ring on the disc: soft halo passes building to a crisp core, as on the knob.
    const float ringR = thR * 0.78f;
    juce::Path ring;
    ring.addEllipse(cx - ringR, cy - ringR, ringR * 2.0f, ringR * 2.0f);
    const float glow = L.highlight(highlighted ? L.switchRingGlowHover : L.switchRingGlow);
    for (float i = 3.0f; i >= 1.0f; i -= 1.0f)
    {
        g.setColour(accent.withAlpha(glow));
        g.strokePath(ring, juce::PathStrokeType(i * thR * 0.22f));
    }
    g.setColour(accent);
    g.strokePath(ring, juce::PathStrokeType(juce::jmax(1.0f, thR * 0.14f)));

    // Labels level with each end: the selected one in the accent, the other dimmed.
    g.setFont(juce::Font(juce::FontOptions{}.withHeight(sf(kKnobLabelFont))));
    const float lx = tr.getRight() + sf(3.0f);
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

// Brushed-metal grain: fine horizontal streaks with a per-scanline tone, generated once
// per size from a fixed seed (same grain every run) and cached — panels behind animated
// components repaint every frame, so the texture must not be rebuilt each time.
static const juce::Image& brushedGrain(int w, int h)
{
    static std::map<std::pair<int, int>, juce::Image> cache;
    auto& img = cache[{ w, h }];
    if (img.isNull() && w > 0 && h > 0)
    {
        img = juce::Image(juce::Image::ARGB, w, h, true);
        juce::Random rng(0x6d75);
        juce::Image::BitmapData bd(img, juce::Image::BitmapData::writeOnly);
        for (int y = 0; y < h; ++y)
        {
            const float row = rng.nextFloat() * 2.0f - 1.0f;   // each scanline's own tone
            float streak = 0.0f;
            for (int x = 0; x < w; ++x)
            {
                // Heavily smoothed noise along x stretches it into long horizontal streaks.
                streak = streak * 0.93f + (rng.nextFloat() * 2.0f - 1.0f) * 0.07f;
                const float v = juce::jlimit(-1.0f, 1.0f, row * 0.55f + streak * 4.0f);
                bd.setPixelColour(x, y, v >= 0.0f ? juce::Colours::white.withAlpha(v)
                                                  : juce::Colours::black.withAlpha(-v));
            }
        }
    }
    return img;
}

void MuLookAndFeel::drawMetalFinish(juce::Graphics& g, const juce::Path& shape,
                                    juce::Rectangle<float> r, float brush, float bands)
{
    const auto& L = lighting();
    const juce::Graphics::ScopedSaveState state(g);
    g.reduceClipRegion(shape);
    const auto ri = r.getSmallestIntegerContainer();
    g.setOpacity(L.highlight(brush));
    g.drawImageAt(brushedGrain(ri.getWidth(), ri.getHeight()), ri.getX(), ri.getY());
    g.setOpacity(1.0f);

    const float sheen = L.highlight(bands);
    juce::ColourGradient grad(juce::Colours::white.withAlpha(0.0f), r.getTopRight(),
                              juce::Colours::white.withAlpha(0.0f), r.getBottomLeft(), false);
    grad.addColour(0.18, juce::Colours::white.withAlpha(sheen));
    grad.addColour(0.34, juce::Colours::white.withAlpha(0.0f));
    grad.addColour(0.55, juce::Colours::black.withAlpha(L.shadow(bands)));
    grad.addColour(0.72, juce::Colours::white.withAlpha(sheen * 0.6f));
    grad.addColour(0.88, juce::Colours::white.withAlpha(0.0f));
    g.setGradientFill(grad);
    g.fillRect(r);
}

void MuLookAndFeel::drawAccentPanel(juce::Graphics& g, juce::Rectangle<float> r,
                                    juce::Colour accent, float cornerSize)
{
    const auto& L = lighting();
    juce::Path shape;
    shape.addRoundedRectangle(r, cornerSize);

    // Wash: the accent at a whisper over whatever the panel sits on.
    g.setColour(accent.withAlpha(L.panelTint));
    g.fillPath(shape);

    // Metal: brushed grain plus soft diagonal reflection bands, lit from the top right.
    drawMetalFinish(g, shape, r, L.panelBrush, L.panelSheen);

    // Highlight: a radial glow anchored on the top-right corner, clipped to the panel.
    {
        const juce::Graphics::ScopedSaveState state(g);
        g.reduceClipRegion(shape);
        const auto  corner = r.getTopRight();
        const float reach  = juce::jmin(std::hypot(r.getWidth(), r.getHeight()) * L.panelHighlightReach,
                                        mu_ui::sf(L.panelHighlightMaxPx));
        juce::ColourGradient glow(accent.withAlpha(L.highlight(L.panelHighlight)), corner.x, corner.y,
                                  accent.withAlpha(0.0f), corner.x - reach, corner.y, true);
        glow.addColour(0.4, accent.withAlpha(L.highlight(L.panelHighlight) * 0.35f));
        g.setGradientFill(glow);
        g.fillRect(r);
    }

    // Painted border: an accent band on the panel's edge, the metal's grain showing through
    // the paint, lit from the top right, with a darker line where the paint ends.
    juce::Path band;
    juce::PathStrokeType(L.panelPaintWidth).createStrokedPath(band, shape);
    const auto bandBounds = band.getBounds();
    g.setColour(accent.withAlpha(L.panelPaintOpacity));
    g.fillPath(band);
    drawMetalFinish(g, band, bandBounds, L.panelPaintGrain, 0.0f);
    g.setGradientFill(juce::ColourGradient(juce::Colours::white.withAlpha(L.highlight(L.panelPaintSheen)), bandBounds.getTopRight(),
                                           juce::Colours::black.withAlpha(L.shadow(L.panelPaintSheen)), bandBounds.getBottomLeft(), false));
    g.fillPath(band);
    g.setColour(accent.darker(0.6f).withAlpha(L.shadow(L.panelPaintEdge)));
    g.strokePath(band, juce::PathStrokeType(0.6f));
}

void MuLookAndFeel::drawRecessedScreen(juce::Graphics& g, juce::Rectangle<float> r)
{
    const auto& L = lighting();
    // Soft inner shading: a few nested strokes, fading inward from the edge.
    for (int i = 0; i < 4; ++i)
    {
        const float fade = 1.0f - (float) i / 4.0f;
        g.setGradientFill(juce::ColourGradient(juce::Colours::black.withAlpha(L.shadow(L.screenBezelShade) * fade), r.getTopRight(),
                                               juce::Colours::white.withAlpha(L.highlight(L.screenBezelLight) * fade), r.getBottomLeft(), false));
        g.drawRect(r.reduced((float) i), 1.0f);
    }
}

bool MuLookAndFeel::isMetal(juce::Component& c)
{
    auto* mlf = dynamic_cast<MuLookAndFeel*>(&c.getLookAndFeel());
    return mlf != nullptr && mlf->isMetalStyle();
}

juce::Colour MuLookAndFeel::appAccent(juce::Component& c)
{
    auto* mlf = dynamic_cast<MuLookAndFeel*>(&c.getLookAndFeel());
    return (mlf != nullptr && mlf->hasAppAccent) ? mlf->appAccentColour : colour(globalAccent);
}

juce::Colour MuLookAndFeel::lcdLitColour(juce::Component& c)
{
    auto* mlf = dynamic_cast<MuLookAndFeel*>(&c.getLookAndFeel());
    return (mlf != nullptr && mlf->hasAppAccent) ? mlf->appAccentColour.brighter(0.6f) : lcdLitColour();
}

juce::Colour MuLookAndFeel::lampBase()
{
    return colour(panelBackground).darker(lighting().lampBaseDarken);
}

juce::Colour MuLookAndFeel::lampColour(juce::Colour clr, float lit)
{
    return lampBase().interpolatedWith(clr, lit);
}

void MuLookAndFeel::drawLamp(juce::Graphics& g, const juce::Path& shape, const juce::AffineTransform& t,
                             juce::Colour lens, juce::Point<float> centre, float hotRadius)
{
    const auto& L = lighting();
    g.setGradientFill(juce::ColourGradient(lens.brighter(L.highlight(L.lampHot)), centre.x, centre.y,
                                           lens.darker(L.lampHot), centre.x + hotRadius, centre.y, true));
    g.fillPath(shape, t);
}

bool MuLookAndFeel::lcdCombo(juce::ComboBox& box)
{
    return (bool) box.getProperties()["muLcd"] || isMetal(box);
}

void MuLookAndFeel::drawEngravedText(juce::Graphics& g, const juce::String& text, juce::Rectangle<int> area,
                                     juce::Justification just, juce::Colour ink, bool ellipsis)
{
    const auto& L = lighting();
    g.setColour(juce::Colours::black.withAlpha(L.shadow(L.engraveCut)));
    g.drawText(text, area.toFloat().translated(-0.5f, 1.0f), just, ellipsis);
    g.setColour(ink);
    g.drawText(text, area, just, ellipsis);
}

float MuLookAndFeel::namePlateWidth(const juce::String& text, float h)
{
    const juce::Font f(juce::FontOptions{}.withHeight(h * 0.78f));
    return juce::GlyphArrangement::getStringWidth(f, text) + h * 2.2f;
}

void MuLookAndFeel::drawTitledPanel(juce::Graphics& g, juce::Rectangle<float> r,
                                    const juce::String& title, juce::Colour accent)
{
    drawAccentPanel(g, r, accent);
    const float h = mu_ui::sf((float) kNamePlateH);
    drawNamePlate(g, { r.getX() + mu_ui::sf(8.0f), r.getY() + mu_ui::sf(5.0f), namePlateWidth(title, h), h }, title);
}

void MuLookAndFeel::drawCentredNamePlate(juce::Graphics& g, juce::Rectangle<float> span, const juce::String& text)
{
    const float h = mu_ui::sf((float) kNamePlateH);
    const float w = namePlateWidth(text, h);
    drawNamePlate(g, { span.getCentreX() - w * 0.5f, span.getCentreY() - h * 0.5f, w, h }, text);
}

void MuLookAndFeel::drawLcdGlass(juce::Graphics& g, juce::Rectangle<float> r, juce::Colour lit, bool backlit)
{
    const auto& L     = lighting();
    const auto  glass = colour(panelBackground).darker(L.lcdGlassDarken);
    if (backlit)
        g.setGradientFill(juce::ColourGradient(glass.interpolatedWith(lit, L.lcdBacklight * 1.4f), r.getCentreX(), r.getCentreY(),
                                               glass.interpolatedWith(lit, L.lcdBacklight * 0.6f), r.getX(), r.getY(), true));
    else
        g.setColour(glass);
    g.fillRect(r);
}

void MuLookAndFeel::drawLcdFront(juce::Graphics& g, juce::Rectangle<float> r)
{
    const auto& L = lighting();
    g.setGradientFill(juce::ColourGradient(juce::Colours::white.withAlpha(L.highlight(L.lcdGlare)), r.getTopRight(),
                                           juce::Colours::transparentWhite, r.getCentre(), false));
    g.fillRect(r);
    drawRecessedScreen(g, r);
}

juce::Colour MuLookAndFeel::lcdLitColour()
{
    return colour(segmentActiveBorder).brighter(0.6f);
}

juce::Font MuLookAndFeel::lcdFont(float height)
{
    return juce::Font(juce::FontOptions{}.withName(juce::Font::getDefaultMonospacedFontName()).withHeight(height));
}

void MuLookAndFeel::drawNamePlate(juce::Graphics& g, juce::Rectangle<float> r, const juce::String& text)
{
    const auto& L = lighting();
    const float h = r.getHeight();
    juce::Path shape;
    shape.addRoundedRectangle(r, h * 0.22f);

    // Cast shadow, falling down-left away from the top-right light.
    juce::DropShadow(juce::Colours::black.withAlpha(L.shadow(L.namePlateShadow)),
                     (int) juce::jmax(2.0f, h * 0.3f), { -1, 1 })
        .drawForPath(g, shape);

    // Face: dark enamel, lit at the top right.
    const auto face = colour(panelBackground).darker(L.namePlateDarken);
    g.setGradientFill(juce::ColourGradient(face.brighter(L.highlight(L.namePlateSheen)), r.getTopRight(),
                                           face.darker(L.namePlateSheen), r.getBottomLeft(), false));
    g.fillPath(shape);

    // Bevelled edge: light top-right, shade bottom-left.
    g.setGradientFill(juce::ColourGradient(juce::Colours::white.withAlpha(L.highlight(L.namePlateEdgeLight)), r.getTopRight(),
                                           juce::Colours::black.withAlpha(L.shadow(L.namePlateEdgeShade)), r.getBottomLeft(), false));
    g.strokePath(shape, juce::PathStrokeType(1.0f));

    // Screw heads: a small domed disc with a slot, one at each end.
    const float sr = h * 0.14f;
    for (float sx : { r.getX() + h * 0.42f, r.getRight() - h * 0.42f })
    {
        const float sy = r.getCentreY();
        g.setGradientFill(juce::ColourGradient(colour(labelText).withAlpha(L.highlight(L.namePlateScrew)), sx + sr, sy - sr,
                                               juce::Colours::black.withAlpha(L.shadow(L.namePlateScrew)), sx - sr, sy + sr, false));
        g.fillEllipse(sx - sr, sy - sr, sr * 2.0f, sr * 2.0f);
        g.setColour(juce::Colours::black.withAlpha(L.shadow(L.namePlateScrew)));
        g.drawLine(sx - sr * 0.7f, sy + sr * 0.7f, sx + sr * 0.7f, sy - sr * 0.7f, 0.8f);
    }

    // Engraved text: a dark cut offset down-left, the lettering over it.
    g.setFont(juce::Font(juce::FontOptions{}.withHeight(h * 0.78f)));
    g.setColour(juce::Colours::black.withAlpha(L.shadow(L.namePlateEngrave)));
    g.drawText(text, r.translated(-0.5f, 1.0f), juce::Justification::centred, false);
    g.setColour(colour(labelText).brighter(0.25f));
    g.drawText(text, r, juce::Justification::centred, false);
}

void MuLookAndFeel::drawRaisedSubPanelShadow(juce::Graphics& g, juce::Rectangle<float> r, float cornerSize)
{
    const auto& L = lighting();
    juce::Path shape;
    shape.addRoundedRectangle(r, cornerSize);
    juce::DropShadow(juce::Colours::black.withAlpha(L.shadow(L.subPanelShadow)),
                     (int) mu_ui::sf(6.0f), { -(int) mu_ui::sf(2.0f), (int) mu_ui::sf(3.0f) })
        .drawForPath(g, shape);
}

// The face of a raised sub-panel: lifted a touch above the panel, lit from the top
// right, its edge catching the light top-right and falling into shade bottom-left.
void MuLookAndFeel::drawRaisedSubPanel(juce::Graphics& g, juce::Rectangle<float> r,
                                       juce::Colour accent, float cornerSize)
{
    const auto& L = lighting();
    juce::Path shape;
    shape.addRoundedRectangle(r, cornerSize);

    g.setColour(colour(panelBackground).brighter(0.08f).withAlpha(0.9f));
    g.fillPath(shape);
    g.setColour(juce::Colours::white.withAlpha(L.highlight(L.subPanelFace)));
    g.fillPath(shape);
    g.setGradientFill(juce::ColourGradient(juce::Colours::white.withAlpha(L.highlight(L.subPanelSheen)), r.getTopRight(),
                                           juce::Colours::transparentWhite, r.getBottomLeft(), false));
    g.fillPath(shape);
    drawMetalFinish(g, shape, r, L.subPanelBrush, L.subPanelBands);

    // Bevelled edge: light top-right, shade bottom-left.
    juce::Path edge;
    edge.addRoundedRectangle(r.reduced(1.0f), juce::jmax(0.0f, cornerSize - 1.0f));
    g.setGradientFill(juce::ColourGradient(juce::Colours::white.withAlpha(L.highlight(L.subPanelEdgeLight)), r.getTopRight(),
                                           juce::Colours::black.withAlpha(L.shadow(L.subPanelEdgeShade)), r.getBottomLeft(), false));
    g.strokePath(edge, juce::PathStrokeType(1.5f));

    g.setColour(accent.withAlpha(L.subPanelOutline));
    g.strokePath(shape, juce::PathStrokeType(1.0f));
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
                                  juce::ComboBox& box)
{
    // LCD look (DropdownSelect::setLcdStyle): dark glass, lit arrow, glare and bezel; the
    // combo's own label draws the value over it in the lit colour.
    if (lcdCombo(box))
    {
        const juce::Rectangle<float> r(0.0f, 0.0f, (float) w, (float) h);
        drawLcdGlass(g, r, colour(segmentActiveBorder), false);
        const float arrowSize = h * 0.3f;
        const float arrowX = w - arrowSize * 1.7f;
        const float arrowY = (h - arrowSize * 0.6f) * 0.5f;
        juce::Path arrow;
        arrow.addTriangle(arrowX, arrowY, arrowX + arrowSize, arrowY, arrowX + arrowSize * 0.5f, arrowY + arrowSize * 0.6f);
        g.setColour(lcdLitColour(box));
        g.fillPath(arrow);
        drawLcdFront(g, r);
        return;
    }

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
    if (lcdCombo(box)) label.setColour(juce::Label::textColourId, lcdLitColour(box));
    label.setFont(lcdCombo(box) ? lcdFont(11.0f) : juce::Font(juce::FontOptions{}.withHeight(12.0f)));
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

        // Metal style: plain text labels are engraved into the metal. Labels with their own
        // background (badges, banners) and a ComboBox's value (an LCD) stay flat.
        const bool onMetal = metalStyle
                          && label.findColour(juce::Label::backgroundColourId).isTransparent()
                          && dynamic_cast<juce::ComboBox*>(label.getParentComponent()) == nullptr;
        if (onMetal)
            drawEngravedText(g, label.getText(), label.getLocalBounds().reduced(2, 0),
                             label.getJustificationType(), label.findColour(juce::Label::textColourId));
        else
            g.drawFittedText(label.getText(), label.getLocalBounds().reduced(2, 0),
                             label.getJustificationType(), 1, 1.0f);
    }
}
