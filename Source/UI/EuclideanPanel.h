#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "Components/KnobWithLabel.h"
#include "Components/SegmentControl.h"
#include "Components/MuClidLookAndFeel.h"

class PluginProcessor;

// Euclidean controls for one rhythm.
// Routes all mutations through APVTS; fires onPatternChanged for UI refresh only.
class EuclideanPanel : public juce::Component
{
public:
    explicit EuclideanPanel(PluginProcessor& p);

    void setRhythm(int rhythmIndex);

    std::function<void(const juce::String& name, const juce::String& value)> onStatusUpdate;
    std::function<void()> onPatternChanged;

    void setRhythmColour(juce::Colour c);

    void resized() override;
    void paint(juce::Graphics&) override;

private:
    using Id = MuClidLookAndFeel::ColourIds;

    PluginProcessor& proc;
    int rhythmIndex = -1;

    juce::Colour rhythmColour { juce::Colours::transparentBlack };

    // ── Euclid A ─────────────────────────────────────────────────────────────
    KnobWithLabel stepsA      { "Steps",         Id::knobEuclidean };
    KnobWithLabel hitsA       { "Hits",          Id::knobEuclidean };
    KnobWithLabel rotA        { "Rotate",        Id::knobEuclidean };
    KnobWithLabel prePadA      { "Pre Pad",       Id::knobPrePad    };
    KnobWithLabel postPadA     { "Post Pad",      Id::knobPostPad   };
    SegmentControl prePadModeA  { {"Pad","Mute"}, SegmentControl::ActiveStyle::Warning };
    SegmentControl postPadModeA { {"Pad","Mute"}, SegmentControl::ActiveStyle::Warning };
    KnobWithLabel insertStA    { "Insert Start",  Id::knobInsertPad };
    KnobWithLabel insertLenA   { "Insert Length", Id::knobInsertPad };
    SegmentControl insertModeA  { {"Pad","Mute"}, SegmentControl::ActiveStyle::Warning };

    // ── Logic ─────────────────────────────────────────────────────────────────
    SegmentControl logicCtrl { {"OR","AND","XOR","A Only","B Only"},
                               SegmentControl::ActiveStyle::General,
                               SegmentControl::DrawStyle::Pills };

    // ── Euclid B ─────────────────────────────────────────────────────────────
    KnobWithLabel stepsB      { "Steps",         Id::knobEuclidean };
    KnobWithLabel hitsB       { "Hits",          Id::knobEuclidean };
    KnobWithLabel rotB        { "Rotate",        Id::knobEuclidean };
    KnobWithLabel prePadB      { "Pre Pad",       Id::knobPrePad    };
    KnobWithLabel postPadB     { "Post Pad",      Id::knobPostPad   };
    SegmentControl prePadModeB  { {"Pad","Mute"}, SegmentControl::ActiveStyle::Warning };
    SegmentControl postPadModeB { {"Pad","Mute"}, SegmentControl::ActiveStyle::Warning };
    KnobWithLabel insertStB    { "Insert Start",  Id::knobInsertPad };
    KnobWithLabel insertLenB   { "Insert Length", Id::knobInsertPad };
    SegmentControl insertModeB  { {"Pad","Mute"}, SegmentControl::ActiveStyle::Warning };

    // ── Euclid C (Accent) ────────────────────────────────────────────────────
    KnobWithLabel stepsC      { "Steps",         Id::knobLevel     };
    KnobWithLabel hitsC       { "Hits",          Id::knobLevel     };
    KnobWithLabel rotC        { "Rotate",        Id::knobLevel     };
    KnobWithLabel prePadC      { "Pre Pad",       Id::knobPrePad    };
    KnobWithLabel postPadC     { "Post Pad",      Id::knobPostPad   };
    SegmentControl prePadModeC  { {"Pad","Mute"}, SegmentControl::ActiveStyle::Warning };
    SegmentControl postPadModeC { {"Pad","Mute"}, SegmentControl::ActiveStyle::Warning };
    KnobWithLabel insertStC    { "Insert Start",  Id::knobInsertPad };
    KnobWithLabel insertLenC   { "Insert Length", Id::knobInsertPad };
    SegmentControl insertModeC  { {"Pad","Mute"}, SegmentControl::ActiveStyle::Warning };

    static constexpr int kLogicH  = 24;
    static constexpr int kSwitchH = 14;
    static constexpr int kOuter   = 4;
    static constexpr int kLabelH  = 10;

    void apvtsSet(const char* suffix, float v);
    void wireCallbacks();
    void loadFromRhythm();
    void updateRangesA(int steps);
    void updateRangesB(int steps);
    void updateRangesC(int steps);
};
