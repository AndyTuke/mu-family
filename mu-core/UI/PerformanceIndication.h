#pragma once
#include <juce_core/juce_core.h>
#include "Control/LiveView.h"
#include "UI/Components/StepIndicator.h"
#include <vector>

// The pure half of Performance mode's screen (docs/design-ui-family.md section 12): what the preset
// indicator, the layer indicator and the status line say, worked out from the live state alone, and the
// hold-to-leave timing. No component or display is touched here, so all of it is unit-tested.
namespace mu_ui::perf
{

// Everything the StepIndicator is told for the preset group.
struct Indication
{
    juce::String lit, ghost;                  // "3/8 Bassline" and "> Acid Line" (or "-> 4 Acid Line" while queued)
    bool         ghostPulses = false;
    std::vector<StepIndicator::Lamp> lamps;   // one per performance item
    std::vector<std::pair<juce::String, juce::String>> hints;   // status-bar text per lamp (name, value)
    std::pair<juce::String, juce::String> hint;                 // status-bar text over the LCD
    juce::String queuedValue;                 // the status bar's "Queued" value, "4 Acid Line (at the next swap point)", or empty
};

// `loadedName` is the name of the preset that is loaded when it is not one of the performance's items.
Indication buildIndication(const mu_core::LiveView& live, const juce::String& loadedName);

// "Layer 3/5" for the sidebar's read-only layer indicator (`selected` is 0-based).
juce::String layerIndicatorText(int selected, int layerCount);

// The layer the editor shows while the mode is on: the live selection, kept inside the layer count.
int followedLayer(int liveSelected, int layerCount) noexcept;

// Press-and-hold timing for leaving the mode. The caller supplies the clock, so tests drive it directly.
class HoldTracker
{
public:
    explicit HoldTracker(double durationMs) noexcept : duration(durationMs) {}

    void   press(double nowMs) noexcept  { holding = true; startMs = nowMs; }
    void   cancel() noexcept             { holding = false; }
    bool   isHolding() const noexcept    { return holding; }
    // 0 when not holding, else the elapsed fraction of the duration, up to 1.
    float  progress(double nowMs) const noexcept
    {
        if (! holding) return 0.0f;
        return (float) juce::jlimit(0.0, 1.0, (nowMs - startMs) / duration);
    }
    bool   finished(double nowMs) const noexcept { return holding && nowMs - startMs >= duration; }

private:
    double duration;
    double startMs = 0.0;
    bool   holding = false;
};

} // namespace mu_ui::perf
