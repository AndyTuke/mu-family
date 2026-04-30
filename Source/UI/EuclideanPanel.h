#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "Components/KnobWithLabel.h"
#include "Components/SegmentControl.h"
#include "Components/MuClidLookAndFeel.h"
#include "../Sequencer/Rhythm.h"

// Euclidean controls for one rhythm.
// Layout (top→bottom): Euclid A (single row, 8 controls) | 24px logic pills | Euclid B (single row, 8 controls) | Euclid C accent (single row, 8 controls)
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
    KnobWithLabel prePadA     { "Pre Pad",       Id::knobPrePad    };
    KnobWithLabel postPadA    { "Post Pad",      Id::knobPostPad   };
    KnobWithLabel insertStA   { "Insert Start",  Id::knobInsertPad };
    KnobWithLabel insertLenA  { "Insert Length", Id::knobInsertPad };
    SegmentControl insertModeA { {"I","M"}, SegmentControl::ActiveStyle::Warning };

    // ── Logic ─────────────────────────────────────────────────────────────────
    SegmentControl logicCtrl { {"OR","AND","XOR","A Only","B Only"},
                               SegmentControl::ActiveStyle::General,
                               SegmentControl::DrawStyle::Pills };

    // ── Euclid B ─────────────────────────────────────────────────────────────
    KnobWithLabel stepsB      { "Steps",         Id::knobEuclidean };
    KnobWithLabel hitsB       { "Hits",          Id::knobEuclidean };
    KnobWithLabel rotB        { "Rotate",        Id::knobEuclidean };
    KnobWithLabel prePadB     { "Pre Pad",       Id::knobPrePad    };
    KnobWithLabel postPadB    { "Post Pad",      Id::knobPostPad   };
    KnobWithLabel insertStB   { "Insert Start",  Id::knobInsertPad };
    KnobWithLabel insertLenB  { "Insert Length", Id::knobInsertPad };
    SegmentControl insertModeB { {"I","M"}, SegmentControl::ActiveStyle::Warning };

    // ── Euclid C (Accent) — same controls as A and B ──────────────────────────
    KnobWithLabel stepsC      { "Steps",         Id::knobLevel     };
    KnobWithLabel hitsC       { "Hits",          Id::knobLevel     };
    KnobWithLabel rotC        { "Rotate",        Id::knobLevel     };
    KnobWithLabel prePadC     { "Pre Pad",       Id::knobPrePad    };
    KnobWithLabel postPadC    { "Post Pad",      Id::knobPostPad   };
    KnobWithLabel insertStC   { "Insert Start",  Id::knobInsertPad };
    KnobWithLabel insertLenC  { "Insert Length", Id::knobInsertPad };
    SegmentControl insertModeC { {"I","M"}, SegmentControl::ActiveStyle::Warning };

    Rhythm* rhythm = nullptr;

    static constexpr int kLogicH  = 24;
    static constexpr int kMaxRowH = 90;

    void wireCallbacks();
    void loadFromRhythm();
    void updateRangesA(int steps);
    void updateRangesB(int steps);
    void updateRangesC(int steps);
};
