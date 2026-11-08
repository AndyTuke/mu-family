#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "UI/Components/MuLookAndFeel.h"
#include "UI/StatusSink.h"

#include <functional>

namespace mu_on
{

// The Hat's tune link: a line drawn under the Closed Tune and Open Tune knobs that joins them, with a lamp in the middle that is
// clicked to switch the link on or off. While it is on, turning one of the two knobs turns the other by the same amount (the
// owner does that; this only draws the line and holds the click). The state is the owner's: isOn / setOn read and write it.
class HatTuneLink : public juce::Component, public juce::SettableTooltipClient
{
public:
    std::function<bool()>     isOn;
    std::function<void(bool)> setOn;

    HatTuneLink() { setMouseCursor(juce::MouseCursor::PointingHandCursor); setTooltip("Link the two tunes: turning one turns the other with it"); }

    void paint(juce::Graphics& g) override
    {
        const bool on = isOn && isOn();
        const auto b = getLocalBounds().toFloat();
        const float left = b.getX() + 3.0f, right = b.getRight() - 3.0f, y = b.getBottom() - 5.0f;
        const auto line = on ? MuLookAndFeel::colour(MuLookAndFeel::segmentActiveBorder) : MuLookAndFeel::colour(MuLookAndFeel::labelText);
        juce::Path bracket;
        bracket.startNewSubPath(left, b.getY());
        bracket.lineTo(left, y);
        bracket.lineTo(right, y);
        bracket.lineTo(right, b.getY());
        g.setColour(line.withAlpha(on ? 1.0f : 0.55f));
        g.strokePath(bracket, juce::PathStrokeType(1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // The lamp: a dark lens on the line, lit when the link is on.
        const auto lamp = juce::Rectangle<float>(11.0f, 11.0f).withCentre({ b.getCentreX(), y });
        g.setColour(juce::Colour(0xff0d0d10));
        g.fillEllipse(lamp.expanded(2.0f));
        g.setColour(on ? line : line.withAlpha(0.25f));
        g.fillEllipse(lamp);
        if (on)
        {
            g.setColour(line.withAlpha(0.35f));
            g.drawEllipse(lamp.expanded(2.5f), 1.5f);
        }
    }

    void mouseDown(const juce::MouseEvent&) override
    {
        if (! setOn) return;
        setOn(! (isOn && isOn()));
        repaint();
        mu_ui::reportStatus(*this, "Hat Tune Link", isOn && isOn() ? "On" : "Off");
    }
    void mouseEnter(const juce::MouseEvent&) override { mu_ui::reportStatus(*this, "Hat Tune Link", isOn && isOn() ? "On" : "Off"); }
};

} // namespace mu_on
