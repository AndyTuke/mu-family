#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "MuLookAndFeel.h"
#include <functional>
#include <vector>

// A read-only "n of N" indicator: an LCD with lit lettering and a ghost continuation beside it, and
// optionally a row of lamps, one per item (docs/design-ui-family.md 12.2). It has no buttons and no
// click handling, so it serves the Performance mode preset group now and the layer / clip rows later.
// Pure display: the owner decides the texts and the lamp states and pushes them in.
class StepIndicator : public juce::Component,
                      private juce::Timer
{
public:
    // Empty = no item there, Available = playable, Current = playing, Queued = waiting for its
    // boundary (pulses), Fault = missing file or another product's item (steady red).
    enum class Lamp { Empty, Available, Current, Queued, Fault };

    StepIndicator();
    ~StepIndicator() override;

    // The lettering: `lit` in the app colour, `ghost` faintly beside it (pulsing when `ghostPulses`).
    void setText(const juce::String& lit, const juce::String& ghost, bool ghostPulses);
    // One lamp per entry, in order; an empty vector shows the LCD alone.
    void setLamps(const std::vector<Lamp>& states);
    // What the status bar shows while the pointer is over lamp `i` (name + value); the LCD uses setHint.
    void setLampHint(int i, const juce::String& name, const juce::String& value);
    void setHint(const juce::String& name, const juce::String& value);
    std::function<void(const juce::String& name, const juce::String& value)> onStatusUpdate;

    const juce::String& getLitText()   const noexcept { return litText; }
    const juce::String& getGhostText() const noexcept { return ghostText; }
    int                 getNumLamps()  const noexcept { return (int) lamps.size(); }
    Lamp                getLamp(int i) const noexcept { return lamps[(size_t) i]; }

    // ── Pure helpers (tested without a display) ─────────────────────────────
    // The LCD and lamp rectangles for a given width: lamps keep their pitch at the right, the LCD
    // takes the rest less the gap.
    struct Layout { juce::Rectangle<int> lcd; std::vector<juce::Rectangle<int>> lamps; };
    static Layout computeLayout(juce::Rectangle<int> bounds, int numLamps);
    // How the LCD's width is shared between the lit lettering and its ghost: both whole when they fit,
    // else the lit part keeps at least 60 % of the width and the ghost gets what is left.
    // `minGhostW`: when less than this is left for the ghost it is omitted and the lit text takes the width (no stub).
    static void splitText(float avail, float litMeasured, float ghostMeasured, float gap, float& litW, float& ghostW,
                          float minGhostW = 0.0f);
    // A smooth 0..1 pulse at `hz` for a time in milliseconds (0 at t = 0).
    static float pulse(double timeMs, float hz) noexcept;
    // The lit amount (Lighting lampOff .. lampOn + lampPlayhead) a lamp shows now; `flash` adds the
    // playhead brightness for a lamp that has just become current.
    static float litFor(Lamp state, double timeMs, bool flash) noexcept;

    void paint(juce::Graphics&) override;
    void resized() override {}
    void mouseMove(const juce::MouseEvent&) override;
    void mouseEnter(const juce::MouseEvent& e) override { mouseMove(e); }

private:
    void timerCallback() override { repaint(); updateTimer(); }
    void updateTimer();
    int  lampAt(juce::Point<int> p) const;

    juce::String litText, ghostText;
    bool ghostPulses = false;
    std::vector<Lamp> lamps;
    std::vector<std::pair<juce::String, juce::String>> lampHints;
    std::pair<juce::String, juce::String> hint;
    std::vector<double> flashUntil;   // per lamp: when its "just became current" flash ends
};
