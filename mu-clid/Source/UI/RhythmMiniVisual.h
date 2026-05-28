#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "RhythmCircle.h"
#include "Sequencer/Rhythm.h"
#include "Sequencer/HitGenerator.h"
#include "Plugin/PluginProcessor.h"

// mu-clid's per-layer sidebar graphic — the mini RhythmCircle bound to a rhythm.
// It is the product-specific Component injected into the shared mu-core
// SidebarItem (which owns the surrounding chrome). Polls the rhythm's pattern +
// modulation each tick so the circle reflects edits, and fires `onHit` on a step
// hit so the parent item can flash its pulse ring. (Extracted from the former
// mu-clid SidebarItem when the sidebar was lifted to mu-core.)
class RhythmMiniVisual : public juce::Component, private juce::Timer
{
public:
    RhythmMiniVisual(PluginProcessor& proc, int rhythmIndex);
    ~RhythmMiniVisual() override { stopTimer(); }

    std::function<void()> onHit;   // step hit → parent SidebarItem pulses

    void resized() override;

private:
    PluginProcessor& proc;
    int              rhythmIndex;
    RhythmCircle     miniCircle;

    int  lastHitCount = 0;
    HitGenerator::Signature lastSigA {}, lastSigB {}, lastSigC {};
    bool                    lastSigValid = false;
    EuclidOverrides         lastAppliedOverrides {};

    void timerCallback() override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RhythmMiniVisual)
};
