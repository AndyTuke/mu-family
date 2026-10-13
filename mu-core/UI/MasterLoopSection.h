#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "Plugin/ProcessorBase.h"
#include "Plugin/HotSwap.h"
#include "UI/Components/DropdownSelect.h"
#include <vector>

// Shared transport-bar "Master Loop" sub-pane — picks the master-loop length
// (Off / 16..256 steps via the `mstrLoop` APVTS param) and shows the current
// step counter while playing. Mounted into the shared mu-core TransportBar via
// setLoopSection(). Product-agnostic: it reads the length + live step from
// ProcessorBase::getMasterLoopSteps()/getMasterLoopCurrentStep() (each product
// derives those from its own engine — mu-clid's sequencer, mu-tant's beat pos).
//
// Optionally a second group, "Swap:", picks where a queued preset change takes effect (the shared
// `mstr_swap` parameter, mu_hotswap::SwapTiming; saved with the preset). The product opts in with
// setSwapTimingChoices() and lists only the timings it can honour; with none (the default) the group
// is hidden and the pane stays loop-only.
class MasterLoopSection : public juce::Component,
                          public juce::AudioProcessorValueTreeState::Listener,
                          private juce::Timer
{
public:
    explicit MasterLoopSection(ProcessorBase& proc);
    ~MasterLoopSection() override;

    // Unscaled sizes (wrap in mu_ui::s at use).
    static constexpr int kPad        = 6;
    static constexpr int kLabelGap   = 2;
    static constexpr int kLoopLabelW = 30;
    static constexpr int kLoopDropW  = 76;
    static constexpr int kLoopCountW = 50;
    static constexpr int kGroupGap   = 6;
    static constexpr int kSwapLabelW = 32;
    static constexpr int kSwapDropW  = 96;

    // One timing the product offers, with its own wording ("On rhythm loop" in mu-Clid, "On voice loop" in mu-Tant).
    struct SwapChoice { mu_hotswap::SwapTiming timing; juce::String text; };

    // Opt in to the Swap group. Call once, before mounting; the dropdown shows exactly these. An empty list
    // keeps the group hidden.
    void setSwapTimingChoices(std::vector<SwapChoice> choices);

    // Width (in baseline pixels, pre-scale) the pane needs for what is showing. Pass to TransportBar::setLoopSection().
    int getPreferredWidth() const noexcept;

    // Status-bar wiring — fired when the user picks a new loop length or swap timing.
    std::function<void(const juce::String& name, const juce::String& value)> onStatusUpdate;

    void resized() override;

private:
    ProcessorBase& proc;

    juce::Label    loopLabel;
    DropdownSelect loopDropdown;
    juce::Label    loopStepLabel;

    juce::Label             swapLabel;
    DropdownSelect          swapDropdown;
    std::vector<SwapChoice> swapChoices;

    void timerCallback() override;
    void syncLoopDropdownFromApvts();
    void syncSwapDropdownFromApvts();
    void parameterChanged(const juce::String& parameterID, float newValue) override;

    // The choice id a stored timing shows as: itself when offered, else the nearest the product offers
    // (never written back to the parameter).
    int swapIdFor(mu_hotswap::SwapTiming stored) const;

    // Last values shown in loopStepLabel — the timer only rebuilds the string when
    // one changes, so a 30 Hz tick doesn't allocate a juce::String every frame.
    int lastShownStep = -1;
    int lastShownSteps = -1;
};
