#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "Components/KnobWithLabel.h"
#include "Components/SegmentControl.h"
#include "Components/MuClidLookAndFeel.h"
#include "../Sequencer/Rhythm.h"

// Euclidean controls for one rhythm.
// Layout (top→bottom): Euclid A (single row, 8 controls) | 24px logic pills | Euclid B (single row)
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
    KnobWithLabel stepsA      { "Steps",         Id::knobEuclidean };
    KnobWithLabel hitsA       { "Hits",          Id::knobEuclidean };
    KnobWithLabel rotA        { "Rotate",        Id::knobEuclidean };
    KnobWithLabel prePadA     { "Pre Pad",       Id::knobPadding   };
    KnobWithLabel postPadA    { "Post Pad",      Id::knobPadding   };
    KnobWithLabel insertStA   { "Insert Start",  Id::knobInsertPad };
    KnobWithLabel insertLenA  { "Insert Length", Id::knobInsertPad };
    SegmentControl insertModeA { {"Pad","Mute"}, SegmentControl::ActiveStyle::Warning };

    // ── Logic ─────────────────────────────────────────────────────────────────
    SegmentControl logicCtrl { {"OR","AND","XOR","A Only","B Only"},
                               SegmentControl::ActiveStyle::General,
                               SegmentControl::DrawStyle::Pills };

    // ── Euclid B ─────────────────────────────────────────────────────────────
    KnobWithLabel stepsB      { "Steps",         Id::knobEuclidean };
    KnobWithLabel hitsB       { "Hits",          Id::knobEuclidean };
    KnobWithLabel rotB        { "Rotate",        Id::knobEuclidean };
    KnobWithLabel prePadB     { "Pre Pad",       Id::knobPadding   };
    KnobWithLabel postPadB    { "Post Pad",      Id::knobPadding   };
    KnobWithLabel insertStB   { "Insert Start",  Id::knobInsertPad };
    KnobWithLabel insertLenB  { "Insert Length", Id::knobInsertPad };
    SegmentControl insertModeB { {"Pad","Mute"}, SegmentControl::ActiveStyle::Warning };

    Rhythm* rhythm = nullptr;

    static constexpr int kLogicH  = 24;
    static constexpr int kMaxRowH = 90;

    void wireCallbacks();
    void loadFromRhythm();
    void updateRangesA(int steps);
    void updateRangesB(int steps);
};
