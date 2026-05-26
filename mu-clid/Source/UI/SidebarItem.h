#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "RhythmCircle.h"
#include "UI/Components/MuClidLookAndFeel.h"
#include "Sequencer/Rhythm.h"
#include "Plugin/PluginProcessor.h"

// One entry in the RhythmSidebar. Shows a small RhythmCircle, colour dot, and rhythm name.
// Right-edge tab line when selected. Background flashes with rhythm colour on hit.
class SidebarItem : public juce::Component, private juce::Timer
{
public:
    explicit SidebarItem(int index);
    ~SidebarItem() override { stopTimer(); }

    void setRhythm(const Rhythm* r, juce::Colour colour);
    void setSelected(bool s);

    // Connect to PluginProcessor play-state for animations.
    // state is non-const because RhythmCircle reads hitCount (monotonic counter, #43).
    void setPlayState(PluginProcessor::RhythmPlayState* state,
                      const std::atomic<float>*         beatFrac,
                      const std::atomic<bool>*           playing);

    // #642 — used by the timer to fetch modulated euclid overrides so the mini-circle
    // reflects pattern modulation. Optional; if not set the circle just shows base state.
    void setProcessor(const PluginProcessor* p) { proc = p; }

    void setPendingSwap(bool p);

    std::function<void(int)> onSelected;
    std::function<void(int)> onCancelPendingSwap;
    std::function<void(int, juce::MouseEvent const&)> onDragStart;
    std::function<void(int, juce::MouseEvent const&)> onDragMove;
    std::function<void(int, juce::MouseEvent const&)> onDragEnd;

    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp  (const juce::MouseEvent&) override;
    void resized() override;

private:
    int           rhythmIndex;
    const Rhythm* rhythm = nullptr;
    juce::Colour  rhythmColour { juce::Colours::transparentBlack };
    bool          selected = false;

    juce::Point<int> mouseDownPos;
    bool             isDragging = false;

    PluginProcessor::RhythmPlayState* playState = nullptr;
    const PluginProcessor*            proc      = nullptr;   // #642 mod overrides accessor
    float pulseAlpha   = 0.0f;
    int   lastHitCount = 0;  // Issue #43: edge-detect against playState->hitCount
    bool  pendingSwap  = false;

    RhythmCircle miniCircle;

    // poll a cheap POD signature instead of allocating + comparing three
    // std::vector<StepType> snapshots every 30 Hz tick. Was ~720 vector allocs/sec
    // across all sidebar slots just to detect change.
    HitGenerator::Signature lastSigA {}, lastSigB {}, lastSigC {};
    bool                    lastSigValid = false;
    // #642: track the modulation overrides applied to the mini-circle so the timer
    // can detect modulation-driven changes (without a full pattern re-render every tick).
    EuclidOverrides         lastAppliedOverrides {};

    void timerCallback() override;
};
