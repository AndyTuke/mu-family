#include "NudgeInput.h"

NudgeInput::NudgeInput(const juce::String& lbl, int minV, int maxV, int defaultV)
    : label(lbl), minVal(minV), maxVal(maxV), currentValue(defaultV)
{
}

void NudgeInput::setValue(int v, bool notify)
{
    v = juce::jlimit(minVal, maxVal, v);
    if (v == currentValue) return;
    currentValue = v;
    repaint();
    if (notify && onChange) onChange(currentValue);
}

void NudgeInput::nudge(int delta)
{
    setValue(currentValue + delta * stepSize, true);
}

void NudgeInput::resized()
{
    const int w = getWidth(), h = getHeight();
    const int arrowW = 16;
    const int stepH  = showStepBtns ? 14 : 0;
    const int lblH   = (!showStepBtns && !label.isEmpty()) ? 12 : 0;
    const int dispH  = h - stepH - lblH;

    upArrowBounds   = { w - arrowW, 0,       arrowW, dispH / 2 };
    downArrowBounds = { w - arrowW, dispH/2, arrowW, dispH / 2 };
    displayBounds   = { 0, 0, w - arrowW, dispH };
    labelBounds     = { 0, dispH, w - arrowW, lblH };

    if (showStepBtns)
    {
        const int btnW = (w - arrowW) / 3;
        step1Bounds  = { 0,      dispH, btnW,     stepH };
        step5Bounds  = { btnW,   dispH, btnW,     stepH };
        step10Bounds = { btnW*2, dispH, w-arrowW - btnW*2, stepH };
    }
}

void NudgeInput::paint(juce::Graphics& g)
{
    using Id = MuClidLookAndFeel::ColourIds;

    // Display area
    g.setColour(MuClidLookAndFeel::colour(Id::segmentInactiveBg));
    g.fillRoundedRectangle(displayBounds.toFloat(), 3.0f);
    g.setColour(MuClidLookAndFeel::colour(Id::segmentInactiveBorder));
    g.drawRoundedRectangle(displayBounds.toFloat().reduced(0.5f), 3.0f, 1.0f);

    g.setColour(MuClidLookAndFeel::colour(Id::valueText));
    g.setFont(juce::Font(juce::FontOptions{}.withHeight(13.0f)));
    g.drawText(juce::String(currentValue), displayBounds, juce::Justification::centred, false);

    // Label below display
    g.setColour(MuClidLookAndFeel::colour(Id::labelText));
    g.setFont(juce::Font(juce::FontOptions{}.withHeight(9.0f)));

    // Up/down arrows
    auto drawArrow = [&](juce::Rectangle<int> bounds, bool up)
    {
        g.setColour(MuClidLookAndFeel::colour(Id::labelText));
        const float cx = (float)bounds.getCentreX();
        const float cy = (float)bounds.getCentreY();
        const float sz = 4.0f;
        juce::Path p;
        if (up)
        {
            p.startNewSubPath(cx - sz, cy + sz * 0.5f);
            p.lineTo(cx + sz, cy + sz * 0.5f);
            p.lineTo(cx, cy - sz * 0.5f);
        }
        else
        {
            p.startNewSubPath(cx - sz, cy - sz * 0.5f);
            p.lineTo(cx + sz, cy - sz * 0.5f);
            p.lineTo(cx, cy + sz * 0.5f);
        }
        p.closeSubPath();
        g.fillPath(p);
    };
    drawArrow(upArrowBounds, true);
    drawArrow(downArrowBounds, false);

    // Label drawn below the display when step buttons are hidden
    if (!label.isEmpty() && !showStepBtns && labelBounds.getHeight() > 0)
    {
        g.setColour(MuClidLookAndFeel::colour(Id::labelText));
        g.setFont(juce::Font(juce::FontOptions{}.withHeight(9.0f)));
        g.drawText(label, labelBounds, juce::Justification::centred, false);
    }

    if (showStepBtns)
    {
        auto drawStep = [&](juce::Rectangle<int> b, const juce::String& txt, bool active)
        {
            g.setColour(active ? MuClidLookAndFeel::colour(Id::segmentActiveBg)
                               : MuClidLookAndFeel::colour(Id::segmentInactiveBg));
            g.fillRect(b);
            g.setColour(MuClidLookAndFeel::colour(Id::segmentInactiveBorder));
            g.drawRect(b, 1);
            g.setColour(active ? MuClidLookAndFeel::colour(Id::segmentActiveBorder)
                               : MuClidLookAndFeel::colour(Id::segmentInactiveText));
            g.setFont(juce::Font(juce::FontOptions{}.withHeight(9.0f)));
            g.drawText(txt, b, juce::Justification::centred, false);
        };
        drawStep(step1Bounds,  "1",  stepSize == 1);
        drawStep(step5Bounds,  "5",  stepSize == 5);
        drawStep(step10Bounds, "10", stepSize == 10);
    }
}

NudgeInput::HitZone NudgeInput::getZone(juce::Point<int> p) const
{
    if (upArrowBounds.contains(p))   return HitZone::Up;
    if (downArrowBounds.contains(p)) return HitZone::Down;
    if (showStepBtns)
    {
        if (step1Bounds.contains(p))  return HitZone::Step1;
        if (step5Bounds.contains(p))  return HitZone::Step5;
        if (step10Bounds.contains(p)) return HitZone::Step10;
    }
    if (displayBounds.contains(p))   return HitZone::Display;
    return HitZone::None;
}

void NudgeInput::mouseDown(const juce::MouseEvent& e)
{
    switch (getZone(e.getPosition()))
    {
        case HitZone::Up:    nudge(+1); break;
        case HitZone::Down:  nudge(-1); break;
        case HitZone::Step1:  stepSize = 1;  repaint(); break;
        case HitZone::Step5:  stepSize = 5;  repaint(); break;
        case HitZone::Step10: stepSize = 10; repaint(); break;
        default: break;
    }
}

void NudgeInput::mouseDoubleClick(const juce::MouseEvent& e)
{
    if (getZone(e.getPosition()) == HitZone::Display)
        showEditor();
}

void NudgeInput::showEditor()
{
    auto* editor = new juce::TextEditor();
    editor->setBounds(displayBounds);
    editor->setText(juce::String(currentValue));
    editor->selectAll();
    addAndMakeVisible(editor);
    editor->grabKeyboardFocus();

    // Shared flag prevents double-deletion when onReturnKey triggers onFocusLost synchronously.
    // callAsync defers delete until the callback stack has fully unwound.
    auto dismissed = std::make_shared<bool>(false);

    editor->onReturnKey = [this, editor, dismissed]
    {
        if (*dismissed) return;
        *dismissed = true;
        int v = editor->getText().getIntValue();
        removeChildComponent(editor);
        juce::MessageManager::callAsync([editor] { delete editor; });
        setValue(v, true);
    };
    editor->onFocusLost = [this, editor, dismissed]
    {
        if (*dismissed) return;
        *dismissed = true;
        removeChildComponent(editor);
        juce::MessageManager::callAsync([editor] { delete editor; });
    };
    editor->onEscapeKey = [this, editor, dismissed]
    {
        if (*dismissed) return;
        *dismissed = true;
        removeChildComponent(editor);
        juce::MessageManager::callAsync([editor] { delete editor; });
    };
}
