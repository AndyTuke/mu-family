#include "KnobWithLabel.h"

KnobWithLabel::KnobWithLabel(const juce::String& label,
                             MuClidLookAndFeel::ColourIds categoryColour)
    : labelText(label), knobColour(categoryColour)
{
    slider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
    slider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    slider.setColour(juce::Slider::rotarySliderFillColourId,
                     MuClidLookAndFeel::colour(knobColour));
    slider.setColour(juce::Slider::rotarySliderOutlineColourId,
                     MuClidLookAndFeel::colour(MuClidLookAndFeel::segmentInactiveBorder));
    slider.setDoubleClickReturnValue(false, slider.getMinimum());

    slider.onValueChange = [this]
    {
        repaint();  // refresh value text in dead zone
        if (settingRange) return;   // #360/#382: suppress callbacks from setRange clip
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
    // #360/#382: JUCE's Slider::setRange clips the current value to the new range
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

void KnobWithLabel::resized()
{
    const int labelH = 14;
    const int topPad = 4;  // room for modulation ring/arc drawn above the slider circle
    slider.setBounds(0, topPad, getWidth(), getHeight() - labelH - topPad);
}

void KnobWithLabel::mouseDoubleClick(const juce::MouseEvent&)
{
    showInlineEditor();
}

void KnobWithLabel::showInlineEditor()
{
    const int   labelH  = 14;
    const int   topPad  = 4;
    const float sliderH = (float)(getHeight() - labelH - topPad);
    const float radius  = juce::jmin((float)getWidth(), sliderH) * 0.5f - 2.0f;
    const float cy      = (float)topPad + sliderH * 0.5f;
    const int   valueY  = (int)(cy + radius * 0.75f) - 5;

    inlineEditor = std::make_unique<juce::TextEditor>();
    auto* ed = inlineEditor.get();

    ed->setFont(juce::Font(juce::FontOptions{}.withHeight(8.0f)));
    ed->setJustification(juce::Justification::centred);
    ed->setColour(juce::TextEditor::backgroundColourId,
                  MuClidLookAndFeel::colour(MuClidLookAndFeel::panelBackground));
    ed->setColour(juce::TextEditor::textColourId,
                  MuClidLookAndFeel::colour(MuClidLookAndFeel::labelText));
    ed->setColour(juce::TextEditor::outlineColourId,
                  MuClidLookAndFeel::colour(MuClidLookAndFeel::knobEuclidean));
    ed->setBounds(1, valueY, getWidth() - 2, 12);
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
    const int labelH = 14;

    // Label below knob
    g.setFont(juce::Font(juce::FontOptions{}.withHeight(10.0f)));
    g.setColour(MuClidLookAndFeel::colour(MuClidLookAndFeel::labelText));
    g.drawText(labelText,
               juce::Rectangle<int>(0, getHeight() - labelH, getWidth(), labelH),
               juce::Justification::centred, true);

    // Value text in the dead zone (5–7 o'clock gap at the bottom of the arc)
    const int   topPad  = 4;
    const float sliderH = (float)(getHeight() - labelH - topPad);
    const float radius  = juce::jmin((float)getWidth(), sliderH) * 0.5f - 2.0f;
    const float cy      = (float)topPad + sliderH * 0.5f;
    const int   valueY  = (int)(cy + radius * 0.75f) - 5;

    g.setFont(juce::Font(juce::FontOptions{}.withHeight(8.0f)));
    g.setColour(MuClidLookAndFeel::colour(MuClidLookAndFeel::valueText));
    g.drawText(slider.getTextFromValue(slider.getValue()),
               0, valueY, getWidth(), 11,
               juce::Justification::centred, true);
}

void KnobWithLabel::setGRSource(const juce::Atomic<float>* gr)
{
    grSource  = gr;
    grDisplay = 0.0f;
    if (gr) startTimerHz(30);
    else    stopTimer();
    repaint();
}

void KnobWithLabel::timerCallback()
{
    const float incoming = grSource ? grSource->get() : 0.0f;
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
    // Issue #133: modulation indicator + #246: GR arc.
    if (! isModulated && std::isnan(modulatedNorm) && grDisplay <= 0.005f) return;

    const auto sb = slider.getBounds().toFloat();
    const float cx = sb.getCentreX();
    const float cy = sb.getCentreY();
    const float radius = juce::jmin(sb.getWidth(), sb.getHeight()) * 0.5f - 2.0f;
    constexpr float startAngle = juce::MathConstants<float>::pi * 1.25f;  // matches juce::Slider rotary defaults
    constexpr float endAngle   = juce::MathConstants<float>::pi * 2.75f;

    const auto modCol = juce::Colour(0xff89e0ff);  // soft cyan tint

    // Static "this knob is modulated" outer ring.
    if (isModulated)
    {
        g.setColour(modCol.withAlpha(0.55f));
        g.drawEllipse(cx - radius - 2.0f, cy - radius - 2.0f,
                      (radius + 2.0f) * 2.0f, (radius + 2.0f) * 2.0f, 1.2f);
    }

    // Live arc tracking the modulated value — originates at the knob's current set position
    // and sweeps clockwise (positive mod) or anti-clockwise (negative mod) from that point.
    if (! std::isnan(modulatedNorm))
    {
        const double range = slider.getMaximum() - slider.getMinimum();
        const float setNorm = (range > 0.0)
            ? juce::jlimit(0.0f, 1.0f, (float)((slider.getValue() - slider.getMinimum()) / range))
            : 0.0f;
        const float baseAngle = startAngle + setNorm      * (endAngle - startAngle);
        const float modAngle  = startAngle + modulatedNorm * (endAngle - startAngle);
        juce::Path arc;
        arc.addCentredArc(cx, cy, radius + 4.0f, radius + 4.0f, 0.0f,
                          juce::jmin(baseAngle, modAngle), juce::jmax(baseAngle, modAngle), true);
        g.setColour(modCol.withAlpha(0.85f));
        g.strokePath(arc, juce::PathStrokeType(1.5f, juce::PathStrokeType::curved,
                                                     juce::PathStrokeType::rounded));
    }

    // #246: GR arc — orange arc sweeping from the max end (5 o'clock) backward
    // proportional to current gain reduction. 1.0 = 24 dB GR = full arc.
    if (grDisplay > 0.005f)
    {
        const float grArcStart = endAngle - grDisplay * (endAngle - startAngle);
        juce::Path grArc;
        grArc.addCentredArc(cx, cy, radius + 4.0f, radius + 4.0f, 0.0f,
                            grArcStart, endAngle, true);
        g.setColour(juce::Colour(0xffff6633).withAlpha(0.85f));
        g.strokePath(grArc, juce::PathStrokeType(2.5f, juce::PathStrokeType::curved,
                                                       juce::PathStrokeType::rounded));
    }
}
