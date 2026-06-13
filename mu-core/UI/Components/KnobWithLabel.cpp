#include "KnobWithLabel.h"

KnobWithLabel::KnobWithLabel(const juce::String& label,
                             MuLookAndFeel::ColourIds categoryColour)
    : labelText(label), knobColour(categoryColour)
{
    slider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
    slider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    // Scroll-wheel events from the DAW (timeline scroll during playback) would
    // otherwise change knob values on hover. Disable to prevent accidental edits.
    slider.setScrollWheelEnabled(false);
    slider.setColour(juce::Slider::rotarySliderFillColourId,
                     MuLookAndFeel::colour(knobColour));
    slider.setColour(juce::Slider::rotarySliderOutlineColourId,
                     MuLookAndFeel::colour(MuLookAndFeel::segmentInactiveBorder));
    slider.setDoubleClickReturnValue(false, slider.getMinimum());

    slider.onValueChange = [this]
    {
        repaint();  // refresh value text in dead zone
        if (settingRange) return;   // suppress callbacks from setRange clip
        if (onStatusUpdate)
            onStatusUpdate(labelText, slider.getTextFromValue(slider.getValue()));
        if (onValueChanged)
            onValueChanged(slider.getValue());
    };

    slider.addMouseListener(this, false);
    addAndMakeVisible(slider);
}

void KnobWithLabel::setRange(double min, double max, double step)
{
    // JUCE's Slider::setRange clips the current value to the new range
    // and fires onValueChange even when the caller would have wanted dontSendNotification.
    // Programmatic range refreshes (EuclideanPanel::updateRangesA/B/C) would otherwise
    // cascade through APVTS into repeated updatePattern() calls; the sequencer's
    // "absorb current step" snapshot logic then drops audible hits while the user
    // drags the steps knob. Suppress the callback here — the owning panel reloads
    // its values from the canonical APVTS state right after.
    settingRange = true;
    slider.setRange(min, max, step);
    settingRange = false;
}

void KnobWithLabel::setValue(double v, juce::NotificationType n)
{
    slider.setValue(v, n);
}

double KnobWithLabel::getValue() const
{
    return slider.getValue();
}

void KnobWithLabel::setLabel(const juce::String& newLabel)
{
    labelText = newLabel;
    repaint();
}

void KnobWithLabel::setIsModulated(bool b)
{
    if (isModulated != b) { isModulated = b; repaint(); }
}

void KnobWithLabel::setModulatedNorm(float norm01)
{
    if (! std::isnan(norm01))
        norm01 = juce::jlimit(0.0f, 1.0f, norm01);
    if (modulatedNorm != norm01 && ! (std::isnan(modulatedNorm) && std::isnan(norm01)))
    {
        modulatedNorm = norm01;
        repaint();
    }
}

void KnobWithLabel::setModulatedActual(float actualValue) noexcept
{
    // Route the actual value through the slider's own proportion-of-length so
    // the arc respects the slider's skew (setSkewFactor / setSkewFactorFromMidPoint)
    // by construction — same path as the needle's baseAngle in paintOverChildren.
    // Without this, a snapshot pre-normalised via a different curve (e.g. pure
    // log) than the slider (e.g. midPoint skew) makes the arc cross over the
    // needle: appearing as a "negative" indicator below the midPoint and
    // "positive" above. Original bug visible on Filter Cutoff at low values
    // (mod ring drew counter-clockwise from the needle) vs high values (drew
    // clockwise) — same mod amount, different visual.
    if (std::isnan(actualValue))
    {
        setModulatedNorm(std::numeric_limits<float>::quiet_NaN());
        return;
    }
    setModulatedNorm((float) slider.valueToProportionOfLength((double) actualValue));
}

void KnobWithLabel::resized()
{
    using mu_ui::s;
    const int labelH = s(MuLookAndFeel::kKnobLabelH);
    const int topPad = s(MuLookAndFeel::kKnobTopPad);
    slider.setBounds(0, topPad, getWidth(), getHeight() - labelH - topPad);
}

void KnobWithLabel::mouseDoubleClick(const juce::MouseEvent&)
{
    showInlineEditor();
}

void KnobWithLabel::showInlineEditor()
{
    using mu_ui::s;
    using mu_ui::sf;
    const int   labelH  = s(MuLookAndFeel::kKnobLabelH);
    const int   topPad  = s(MuLookAndFeel::kKnobTopPad);
    const float sliderH = (float)(getHeight() - labelH - topPad);
    const float radius  = juce::jmin((float)getWidth(), sliderH) * 0.5f - sf(2.0f);
    const float cy      = (float)topPad + sliderH * 0.5f;
    const int   valueY  = (int)(cy + radius * 0.75f) - s(5);

    inlineEditor = std::make_unique<juce::TextEditor>();
    auto* ed = inlineEditor.get();

    ed->setFont(juce::Font(juce::FontOptions{}.withHeight(sf(MuLookAndFeel::kKnobValueFont))));
    ed->setJustification(juce::Justification::centred);
    ed->setColour(juce::TextEditor::backgroundColourId,
                  MuLookAndFeel::colour(MuLookAndFeel::panelBackground));
    ed->setColour(juce::TextEditor::textColourId,
                  MuLookAndFeel::colour(MuLookAndFeel::labelText));
    ed->setColour(juce::TextEditor::outlineColourId,
                  MuLookAndFeel::colour(MuLookAndFeel::knobEuclidean));
    ed->setBounds(1, valueY, getWidth() - 2, s(12));
    ed->setText(slider.getTextFromValue(slider.getValue()), false);
    ed->selectAll();

    // Deleting the TextEditor synchronously from inside its own callback causes
    // re-entrant destruction (the destructor fires focusLost → onFocusLost → commit).
    // Instead: null all callbacks first to break re-entrancy, then schedule deletion.
    juce::Component::SafePointer<KnobWithLabel> safeThis(this);
    auto asyncClose = [safeThis]
    {
        if (safeThis)
        {
            safeThis->inlineEditor.reset();
            safeThis->repaint();
        }
    };

    auto commitValue = [this, ed, asyncClose]
    {
        const double v = slider.getValueFromText(ed->getText());
        ed->onReturnKey = nullptr;
        ed->onEscapeKey = nullptr;
        ed->onFocusLost = nullptr;
        slider.setValue(juce::jlimit(slider.getMinimum(), slider.getMaximum(), v));
        juce::MessageManager::callAsync(asyncClose);
    };
    auto cancelEdit = [ed, asyncClose]
    {
        ed->onReturnKey = nullptr;
        ed->onEscapeKey = nullptr;
        ed->onFocusLost = nullptr;
        juce::MessageManager::callAsync(asyncClose);
    };

    ed->onReturnKey = commitValue;
    ed->onEscapeKey = cancelEdit;
    ed->onFocusLost = commitValue;

    addAndMakeVisible(ed);
    ed->grabKeyboardFocus();
}

void KnobWithLabel::mouseEnter(const juce::MouseEvent&)
{
    if (onStatusUpdate)
        onStatusUpdate(labelText, slider.getTextFromValue(slider.getValue()));
}

void KnobWithLabel::paint(juce::Graphics& g)
{
    using mu_ui::s;
    using mu_ui::sf;
    const int labelH = s(MuLookAndFeel::kKnobLabelH);

    // Label below knob
    g.setFont(juce::Font(juce::FontOptions{}.withHeight(sf(MuLookAndFeel::kKnobLabelFont))));
    g.setColour(MuLookAndFeel::colour(MuLookAndFeel::labelText));
    g.drawText(labelText,
               juce::Rectangle<int>(0, getHeight() - labelH, getWidth(), labelH),
               juce::Justification::centred, true);

    // Value text in the dead zone (5–7 o'clock gap at the bottom of the arc)
    const int   topPad  = s(MuLookAndFeel::kKnobTopPad);
    const float sliderH = (float)(getHeight() - labelH - topPad);
    const float radius  = juce::jmin((float)getWidth(), sliderH) * 0.5f - sf(2.0f);
    const float cy      = (float)topPad + sliderH * 0.5f;
    const int   valueY  = (int)(cy + radius * 0.75f) - s(5);

    g.setFont(juce::Font(juce::FontOptions{}.withHeight(sf(MuLookAndFeel::kKnobValueFont))));
    g.setColour(MuLookAndFeel::colour(MuLookAndFeel::valueText));
    g.drawText(slider.getTextFromValue(slider.getValue()),
               0, valueY, getWidth(), s(MuLookAndFeel::kKnobValueH),
               juce::Justification::centred, true);
}

void KnobWithLabel::bindModulation(const char*             destId,
                                   const ModulationMatrix* matrix,
                                   std::function<float()>  liveValueFn,
                                   bool                    normMode)
{
    modDestId    = destId ? destId : "";
    modMatrix    = matrix;
    modLiveValue = std::move(liveValueFn);
    modNormMode  = normMode;
    hasModBind   = true;
    if (!isTimerRunning()) startTimerHz(30);
}

void KnobWithLabel::clearModBinding() noexcept
{
    hasModBind   = false;
    modMatrix    = nullptr;
    modLiveValue = nullptr;
    modDestId.clear();
    setIsModulated(false);
    setModulatedNorm(std::numeric_limits<float>::quiet_NaN());
    if (!grSource) stopTimer();
}

void KnobWithLabel::setGRSource(const std::atomic<float>* gr)
{
    grSource  = gr;
    grDisplay = 0.0f;
    if (gr) startTimerHz(30);
    else if (!hasModBind) stopTimer();
    repaint();
}

void KnobWithLabel::timerCallback()
{
    if (hasModBind)
    {
        bool assigned = false;
        if (modMatrix)
            for (const auto& a : modMatrix->getAssignments())
                if (a.destinationId == modDestId) { assigned = true; break; }
        setIsModulated(assigned);
        const float kNaN = std::numeric_limits<float>::quiet_NaN();
        const float live = (assigned && modLiveValue) ? modLiveValue() : kNaN;
        if (modNormMode) setModulatedNorm(live);
        else             setModulatedActual(live);
    }

    const float incoming = grSource ? grSource->load() : 0.0f;
    const float prev     = grDisplay;
    grDisplay = (incoming > grDisplay)
              ? incoming
              : kGRRelease * grDisplay + (1.0f - kGRRelease) * incoming;
    grDisplay = juce::jlimit(0.0f, 1.0f, grDisplay);
    const bool wasVisible = prev      > 0.005f;
    const bool nowVisible = grDisplay > 0.005f;
    if (wasVisible != nowVisible || std::abs(grDisplay - prev) > 0.001f)
        repaint();
}

void KnobWithLabel::paintOverChildren(juce::Graphics& g)
{
    // Modulation indicator ring and GR arc overlay.
    if (! isModulated && std::isnan(modulatedNorm) && grDisplay <= 0.005f) return;

    using mu_ui::sf;
    const auto sb = slider.getBounds().toFloat();
    const float cx = sb.getCentreX();
    const float cy = sb.getCentreY();
    const float radius = juce::jmin(sb.getWidth(), sb.getHeight()) * 0.5f - sf(2.0f);
    constexpr float startAngle = juce::MathConstants<float>::pi * 1.25f;  // matches juce::Slider rotary defaults
    constexpr float endAngle   = juce::MathConstants<float>::pi * 2.75f;

    const auto modCol = MuLookAndFeel::colour(MuLookAndFeel::indicatorModulationTint);

    // Static "this knob is modulated" outer ring.
    if (isModulated)
    {
        const float outerOff = sf(2.0f);
        g.setColour(modCol.withAlpha(0.55f));
        g.drawEllipse(cx - radius - outerOff, cy - radius - outerOff,
                      (radius + outerOff) * 2.0f, (radius + outerOff) * 2.0f, sf(1.2f));
    }

    // Live arc tracking the modulated value — originates at the knob's current
    // set position and sweeps to the modulator's current position. Uses
    // `Slider::valueToProportionOfLength` so the base angle respects the
    // slider's skew (setSkewFactor / setSkewFactorFromMidPoint) and lines up
    // visually with the needle. Without this, a skewed knob (e.g. log cutoff)
    // sees the arc start at the LINEAR proportional position — which can be
    // far from the visible needle, so the arc draws "through" the needle
    // creating the illusion of an indicator on both sides.
    if (! std::isnan(modulatedNorm))
    {
        const float setNorm = (float) slider.valueToProportionOfLength(slider.getValue());
        const float baseAngle = startAngle + setNorm        * (endAngle - startAngle);
        const float modAngle  = startAngle + modulatedNorm  * (endAngle - startAngle);
        const float arcR = radius + sf(4.0f);
        juce::Path arc;
        arc.addCentredArc(cx, cy, arcR, arcR, 0.0f,
                          juce::jmin(baseAngle, modAngle), juce::jmax(baseAngle, modAngle), true);
        g.setColour(modCol.withAlpha(0.85f));
        g.strokePath(arc, juce::PathStrokeType(sf(1.5f), juce::PathStrokeType::curved,
                                                     juce::PathStrokeType::rounded));
    }

    // GR arc — orange arc sweeping from the max end (5 o'clock) backward
    // proportional to current gain reduction. 1.0 = 24 dB GR = full arc.
    if (grDisplay > 0.005f)
    {
        const float grArcStart = endAngle - grDisplay * (endAngle - startAngle);
        const float arcR = radius + sf(4.0f);
        juce::Path grArc;
        grArc.addCentredArc(cx, cy, arcR, arcR, 0.0f,
                            grArcStart, endAngle, true);
        g.setColour(MuLookAndFeel::colour(MuLookAndFeel::indicatorGRTint).withAlpha(0.85f));
        g.strokePath(grArc, juce::PathStrokeType(sf(2.5f), juce::PathStrokeType::curved,
                                                       juce::PathStrokeType::rounded));
    }
}
