#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "Components/KnobWithLabel.h"
#include "Components/SegmentControl.h"
#include "Components/MuClidLookAndFeel.h"
#include "../Sequencer/Rhythm.h"

// Euclidean controls for one rhythm.
// Layout (top→bottom): Euclid A (2 knob rows) | 20px logic pills | Euclid B (2 knob rows)
// Modifies Rhythm data directly; fires onPatternChanged after each mutation.
class EuclideanPanel : public juce::Component
{
public:
    EuclideanPanel();

    void setRhythm(Rhythm* r);

    std::function<void(const juce::String& name, const juce::String& value)> onStatusUpdate;
    std::function<void()> onPatternChanged;

    void resized() override;
    void paint(juce::Graphics&) override;

private:
    using Id = MuClidLookAndFeel::ColourIds;

    // ── Euclid A ─────────────────────────────────────────────────────────────
    // Row 1
    KnobWithLabel stepsA      { "STEPS", Id::knobEuclidean };
    KnobWithLabel hitsA       { "HITS",  Id::knobEuclidean };
    KnobWithLabel rotA        { "ROT",   Id::knobEuclidean };
    KnobWithLabel prePadA     { "PRE",   Id::knobPadding   };
    // Row 2
    KnobWithLabel postPadA    { "POST",  Id::knobPadding   };
    KnobWithLabel insertStA   { "I.ST",  Id::knobInsertPad };
    KnobWithLabel insertLenA  { "I.LEN", Id::knobInsertPad };
    SegmentControl insertModeA { {"PAD","MUTE"}, SegmentControl::ActiveStyle::Warning };

    // ── Logic ─────────────────────────────────────────────────────────────────
    SegmentControl logicCtrl { {"OR","AND","XOR","A","B"},
                               SegmentControl::ActiveStyle::General,
                               SegmentControl::DrawStyle::Pills };

    // ── Euclid B ─────────────────────────────────────────────────────────────
    // Row 1
    KnobWithLabel stepsB      { "STEPS", Id::knobEuclidean };
    KnobWithLabel hitsB       { "HITS",  Id::knobEuclidean };
    KnobWithLabel rotB        { "ROT",   Id::knobEuclidean };
    KnobWithLabel prePadB     { "PRE",   Id::knobPadding   };
    // Row 2
    KnobWithLabel postPadB    { "POST",  Id::knobPadding   };
    KnobWithLabel insertStB   { "I.ST",  Id::knobInsertPad };
    KnobWithLabel insertLenB  { "I.LEN", Id::knobInsertPad };
    SegmentControl insertModeB { {"PAD","MUTE"}, SegmentControl::ActiveStyle::Warning };

    Rhythm* rhythm = nullptr;

    static constexpr int kLogicH = 20;
    static constexpr int kMaxRowH = 50;

    void wireCallbacks();
    void loadFromRhythm();
    void updateRangesA(int steps);
    void updateRangesB(int steps);
};
