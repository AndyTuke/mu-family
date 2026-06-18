#include "SidebarItem.h"

SidebarItem::SidebarItem(int idx)
    : index(idx)
{
}

void SidebarItem::setName(const juce::String& n)   { name = n; repaint(); }
void SidebarItem::setColour(juce::Colour c)        { colour = c; repaint(); }

void SidebarItem::setSelected(bool s)
{
    if (selected != s) { selected = s; repaint(); }
}

void SidebarItem::setPendingSwap(bool p)
{
    if (pendingSwap != p) { pendingSwap = p; repaint(); }
}

void SidebarItem::pulse()
{
    pulseAlpha = 0.4f;
    if (! isTimerRunning()) startTimerHz(mu_ui::kUiRefreshHz);
    repaint();
}

void SidebarItem::setMiniVisual(std::unique_ptr<juce::Component> visual)
{
    if (miniVisual != nullptr) removeChildComponent(miniVisual.get());
    miniVisual = std::move(visual);
    if (miniVisual != nullptr)
    {
        miniVisual->setInterceptsMouseClicks(false, false);
        addAndMakeVisible(miniVisual.get());
    }
    resized();
}

void SidebarItem::timerCallback()
{
    // Pulse-ring decay only — the mini-graphic runs its own animation timer.
    if (pulseAlpha > 0.0f)
    {
        // Quadratic ease-out over ~200 ms (6 ticks @ 30 Hz).
        pulseAlpha = juce::jmax(0.0f, pulseAlpha - (0.4f / (0.2f * 30.0f)));
        repaint();
    }
    if (pulseAlpha <= 0.0f)
        stopTimer();
}

juce::Rectangle<int> SidebarItem::badgeBounds() const
{
    using mu_ui::s;
    const int badgeH = s(13);
    const int badgeW = s(28);
    return { getWidth() - badgeW - s(3), s(3), badgeW, badgeH };
}

void SidebarItem::resized()
{
    using mu_ui::s;
    const int w          = getWidth();
    const int nameH      = s(14);
    const int circleSize = juce::jmin(w - s(8), getHeight() - nameH - s(6));
    const int circleX    = (w - circleSize) / 2;
    if (miniVisual != nullptr)
        miniVisual->setBounds(circleX, s(4), juce::jmax(0, circleSize), juce::jmax(0, circleSize));
}

void SidebarItem::paint(juce::Graphics& g)
{
    using Id = MuLookAndFeel::ColourIds;
    using mu_ui::s;
    using mu_ui::sf;

    const int w = getWidth();
    const int h = getHeight();

    g.setColour(MuLookAndFeel::colour(selected ? Id::sidebarItemSelected
                                               : Id::sidebarItemBackground));
    g.fillRect(0, 0, w, h);

    // Expanding pulse ring centred on the mini-graphic — fades + grows on a hit.
    if (pulseAlpha > 0.0f && miniVisual != nullptr)
    {
        const float progress = 1.0f - (pulseAlpha / 0.4f);   // 0 = just fired, 1 = faded
        const float cx   = w * 0.5f;
        const float cy   = miniVisual->getY() + miniVisual->getHeight() * 0.5f;
        const float maxR = (float) std::min(miniVisual->getWidth(), miniVisual->getHeight()) * 0.55f;
        const float r    = progress * maxR;
        const float a    = pulseAlpha / 0.4f;
        if (r >= 1.0f)
        {
            g.setColour(colour.withAlpha(a * 0.9f));
            g.drawEllipse(cx - r, cy - r, r * 2.0f, r * 2.0f, sf(2.0f));
        }
    }

    // Right-edge tab line when selected.
    if (selected)
    {
        g.setColour(colour);
        const int tabW = s(3);
        g.fillRect(w - tabW, 0, tabW, h);
    }

    // Name row below the mini-graphic.
    const int miniBottom = miniVisual != nullptr ? miniVisual->getBottom() : s(4);
    const int nameY      = miniBottom + s(2);
    const int nameRowH   = h - nameY - s(2);
    if (nameRowH > 0)
    {
        g.setColour(colour);
        g.fillEllipse(sf(5.0f), (float) (nameY + (nameRowH - s(6)) / 2), sf(6.0f), sf(6.0f));

        g.setColour(MuLookAndFeel::colour(selected ? Id::valueText : Id::labelText));
        g.setFont(juce::Font(juce::FontOptions{}.withHeight(sf(9.0f))));
        g.drawText(name, s(14), nameY, w - s(18), nameRowH, juce::Justification::centredLeft, true);
    }

    // Pending hot-swap badge — orange pill, top-right, "SWP". Click cancels.
    if (pendingSwap)
    {
        const auto b = badgeBounds();
        g.setColour(juce::Colours::orange.withAlpha(0.85f));
        g.fillRoundedRectangle(b.toFloat(), sf(3.0f));
        g.setColour(juce::Colours::black);
        g.setFont(juce::Font(juce::FontOptions{}.withHeight(sf(8.0f))));
        g.drawText("SWP", b, juce::Justification::centred, false);
    }

    // Bottom separator.
    g.setColour(MuLookAndFeel::colour(Id::segmentInactiveBorder));
    g.drawLine(sf(4.0f), (float) (h - 1), (float) (w - s(4)), (float) (h - 1), 0.5f);
}

void SidebarItem::mouseDown(const juce::MouseEvent& e)
{
    if (pendingSwap && badgeBounds().contains(e.getPosition()))
    {
        if (onCancelPendingSwap) onCancelPendingSwap(index);
        return;
    }
    mouseDownPos = e.getPosition();
    isDragging   = false;
}

void SidebarItem::mouseDrag(const juce::MouseEvent& e)
{
    constexpr int kDragThresholdPx = 4;
    if (! isDragging)
    {
        if (e.getPosition().getDistanceFrom(mouseDownPos) >= kDragThresholdPx)
        {
            isDragging = true;
            if (onDragStart) onDragStart(index, e);
        }
    }
    else if (onDragMove)
    {
        onDragMove(index, e);
    }
}

void SidebarItem::mouseUp(const juce::MouseEvent& e)
{
    if (isDragging)
    {
        if (onDragEnd) onDragEnd(index, e);
        isDragging = false;
    }
    else if (onSelected)
    {
        onSelected(index);
    }
}
