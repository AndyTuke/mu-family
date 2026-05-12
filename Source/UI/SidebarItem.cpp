#include "SidebarItem.h"

SidebarItem::SidebarItem(int index)
    : rhythmIndex(index)
{
    addAndMakeVisible(miniCircle);
    miniCircle.setInterceptsMouseClicks(false, false);
    startTimerHz(30);
}

void SidebarItem::setRhythm(const Rhythm* r, juce::Colour colour)
{
    rhythm = r;
    rhythmColour = colour;
    miniCircle.setPatterns(r ? r->genA.getStepTypes() : std::vector<StepType>{},
                           r ? r->genB.getStepTypes() : std::vector<StepType>{},
                           r ? r->genC.getStepTypes() : std::vector<StepType>{});
    repaint();
}

void SidebarItem::setSelected(bool s)
{
    selected = s;
    repaint();
}

void SidebarItem::setPendingSwap(bool p)
{
    if (pendingSwap != p)
    {
        pendingSwap = p;
        repaint();
    }
}

void SidebarItem::setPlayState(PluginProcessor::RhythmPlayState* state,
                                const juce::Atomic<float>*         beatFrac,
                                const juce::Atomic<bool>*           playing)
{
    playState = state;
    miniCircle.setPlayState(state, beatFrac, playing, rhythmColour);
}

void SidebarItem::timerCallback()
{
    // Issue #43: edge-detect via monotonic counter. Previous code read a one-shot
    // bool flag that the big RhythmCircle cleared in its own timer, so whichever
    // of the two timers fired second saw `false` and the sidebar pulse never fired.
    if (playState)
    {
        const int currentHitCount = playState->hitCount.get();
        if (currentHitCount != lastHitCount)
        {
            lastHitCount = currentHitCount;
            pulseAlpha   = 0.4f;
        }
    }

    // #252: re-read the rhythm's pattern every tick so edits in the main
    // EuclideanPanel (hits / rotation / steps / insert / pad) propagate to
    // the sidebar mini-circle. setRhythm is only called at construction /
    // reassign, so without this poll the sidebar shows a stale snapshot.
    if (rhythm)
    {
        auto patA = rhythm->genA.getStepTypes();
        auto patB = rhythm->genB.getStepTypes();
        auto patC = rhythm->genC.getStepTypes();
        if (patA != cachedPatA || patB != cachedPatB || patC != cachedPatC)
        {
            cachedPatA = std::move(patA);
            cachedPatB = std::move(patB);
            cachedPatC = std::move(patC);
            miniCircle.setPatterns(cachedPatA, cachedPatB, cachedPatC);
        }
    }

    if (pulseAlpha > 0.0f)
    {
        // Quadratic ease-out decay over ~200ms (6 ticks at 30Hz)
        pulseAlpha = juce::jmax(0.0f, pulseAlpha - (0.4f / (0.2f * 30.0f)));
        repaint();
    }
}

void SidebarItem::resized()
{
    const int w          = getWidth();
    const int nameH      = 14;
    const int circleSize = juce::jmin(w - 8, getHeight() - nameH - 6);
    const int circleX    = (w - circleSize) / 2;
    miniCircle.setBounds(circleX, 4, circleSize, circleSize);
}

void SidebarItem::paint(juce::Graphics& g)
{
    using Id = MuClidLookAndFeel::ColourIds;

    const int w = getWidth();
    const int h = getHeight();

    g.setColour(MuClidLookAndFeel::colour(
        selected ? Id::sidebarItemSelected : Id::sidebarItemBackground));
    g.fillRect(0, 0, w, h);

    // Expanding pulse ring centred on miniCircle — fades and grows on hit.
    if (pulseAlpha > 0.0f)
    {
        const float progress = 1.0f - (pulseAlpha / 0.4f);   // 0 = just fired, 1 = faded
        const float cx = w * 0.5f;
        const float cy = miniCircle.getY() + miniCircle.getHeight() * 0.5f;
        const float maxR = static_cast<float>(std::min(miniCircle.getWidth(),
                                                        miniCircle.getHeight())) * 0.55f;
        const float r = progress * maxR;
        const float a = pulseAlpha / 0.4f;  // 1.0 at peak, 0.0 when done
        if (r >= 1.0f)
        {
            g.setColour(rhythmColour.withAlpha(a * 0.9f));
            g.drawEllipse(cx - r, cy - r, r * 2.0f, r * 2.0f, 2.0f);
        }
    }

    // Right-edge tab line when selected
    if (selected)
    {
        g.setColour(rhythmColour);
        g.fillRect(w - 3, 0, 3, h);
    }

    // Name row below circle
    const int nameY = miniCircle.getBottom() + 2;
    const int nameRowH = h - nameY - 2;
    if (nameRowH > 0)
    {
        g.setColour(rhythmColour);
        g.fillEllipse(5.0f, (float)(nameY + (nameRowH - 6) / 2), 6.0f, 6.0f);

        const juce::String name = rhythm ? juce::String(rhythm->name) : "---";
        g.setColour(MuClidLookAndFeel::colour(selected ? Id::valueText : Id::labelText));
        g.setFont(juce::Font(juce::FontOptions{}.withHeight(9.0f)));
        g.drawText(name, 14, nameY, w - 18, nameRowH, juce::Justification::centredLeft, true);
    }

    // Pending hot-swap badge: orange pill in top-right with "SWP" text.
    // Clicking it (see mouseDown) cancels the staged swap.
    if (pendingSwap)
    {
        constexpr int badgeH = 13;
        constexpr int badgeW = 28;
        const int badgeX = w - badgeW - 3;
        const int badgeY = 3;
        g.setColour(juce::Colours::orange.withAlpha(0.85f));
        g.fillRoundedRectangle((float)badgeX, (float)badgeY, (float)badgeW, (float)badgeH, 3.0f);
        g.setColour(juce::Colours::black);
        g.setFont(juce::Font(juce::FontOptions{}.withHeight(8.0f)));
        g.drawText("SWP", badgeX, badgeY, badgeW, badgeH, juce::Justification::centred, false);
    }

    // Bottom separator
    g.setColour(MuClidLookAndFeel::colour(Id::segmentInactiveBorder));
    g.drawLine(4.0f, (float)(h - 1), (float)(w - 4), (float)(h - 1), 0.5f);
}

void SidebarItem::mouseDown(const juce::MouseEvent& e)
{
    // Cancel staged swap if the pending badge was clicked.
    if (pendingSwap)
    {
        constexpr int badgeH = 13;
        constexpr int badgeW = 28;
        const int badgeX = getWidth() - badgeW - 3;
        const juce::Rectangle<int> badgeRect { badgeX, 3, badgeW, badgeH };
        if (badgeRect.contains(e.getPosition()))
        {
            if (onCancelPendingSwap) onCancelPendingSwap(rhythmIndex);
            return;
        }
    }
    mouseDownPos = e.getPosition();
    isDragging = false;
}

void SidebarItem::mouseDrag(const juce::MouseEvent& e)
{
    constexpr int kDragThresholdPx = 4;
    if (!isDragging)
    {
        if (e.getPosition().getDistanceFrom(mouseDownPos) >= kDragThresholdPx)
        {
            isDragging = true;
            if (onDragStart) onDragStart(rhythmIndex, e);
        }
    }
    else
    {
        if (onDragMove) onDragMove(rhythmIndex, e);
    }
}

void SidebarItem::mouseUp(const juce::MouseEvent& e)
{
    if (isDragging)
    {
        if (onDragEnd) onDragEnd(rhythmIndex, e);
        isDragging = false;
    }
    else
    {
        if (onSelected) onSelected(rhythmIndex);
    }
}
