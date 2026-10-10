#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

// A small status lamp with a label beside it (e.g. the transport bar's MIDI "Clock" lamp): a
// round lens lit in a state colour, built on MuLookAndFeel's lamp helpers. Hovering reports the
// lamp's name and status text to the status bar. Steady in every state — it never blinks.
class StatusLamp : public juce::Component
{
public:
    void setLabel(const juce::String& text);

    // The lamp's colour and how lit it is (Lighting lampOff / lampDim / lampOn); repaints only
    // when it changes.
    void setState(juce::Colour clr, float lit);

    // The status-bar line for this lamp: `name` and the current `status`. Sent on hover.
    void setStatus(const juce::String& name, const juce::String& status) { statusName = name; statusText = status; }
    std::function<void(const juce::String& name, const juce::String& status)> onStatusUpdate;

    void paint(juce::Graphics&) override;
    void mouseEnter(const juce::MouseEvent&) override;

private:
    juce::String label, statusName, statusText;
    juce::Colour colour;
    float        lit = 0.0f;
};
