#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <string_view>
#include <unordered_map>
#include "UI/Components/KnobWithLabel.h"
#include "UI/Components/SegmentControl.h"
#include "UI/Components/DropdownSelect.h"
#include "UI/Components/MuLookAndFeel.h"

namespace juce { class RangedAudioParameter; }
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
    void loadFromRhythm();

    // refresh a single control identified by its APVTS suffix (e.g. "stepsB",
    // "hitsA", "logic", "prePadModeC"). Used by RhythmPanel::parameterChanged to
    // avoid rewriting all 31 knobs/segments on every single parameter change.
    void refreshSuffix(const juce::String& suffix);

    // Bind all euclidean knobs to their modulation destinations for the current rhythm.
    void bindModulationIndicators();

    void resized() override;
    void paint(juce::Graphics&) override;

    // Left/right border inset inside the panel — used by LiteEditor to align
    // controls below the panel with the Steps knobs above.
    static constexpr int kPanelInset = 4;

private:
    using Id = MuLookAndFeel::ColourIds;

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

    // ── Legato + Logic ────────────────────────────────────────────────
    // Pattern legato sits FIRST on the same row as the logic pills, separated
    // by a sub-panel gap. Visually communicates that legato is a per-rhythm
    // sequencer modifier distinct from the per-step logic-combination choice
    // but shares the same horizontal real-estate.
    SegmentControl legatoCtrl { {"Trig","Leg"},
                                SegmentControl::ActiveStyle::General,
                                SegmentControl::DrawStyle::Pills };
    SegmentControl monoCtrl  { {"Poly","Mono"},
                                SegmentControl::ActiveStyle::Warning,
                                SegmentControl::DrawStyle::Pills };
    // Logic dropdown — was a 5-pill SegmentControl; pills crowded the row so
    // converted to a dropdown. IDs are 1-based (JUCE ComboBox convention)
    // and map to APVTS "logic" param via id - 1.
    DropdownSelect logicCtrl;

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
    // logic-row split — Logic dropdown | gap | Legato | gap | Mono. Three sub-panels
    // sized EQUALLY across the row; each control fills its sub-panel so pills spread
    // evenly via SegmentControl's natural width-distribution.
    static constexpr int kLogicMP    = 4;    // matches the local `mP` used in placeRow
    static constexpr int kLogicGapW  = 8;    // sub-panel divider between groups
    // Vertical offset (within the kLogicH band) that shifts the Logic-row buttons +
    // sub-panel rects DOWN so the visible gap above the rects equals the gap below them.
    // Pre-fix the band was top-aligned in the inter-row space, leaving 4 px above and
    // 12 px below the rects — visibly uneven. Both pill Y and rect Y use this offset.
    static constexpr int kLogicVOffset = 3;

    // Euclid-row spacing.
    // kEucKnobGap widened so Steps/Hits/Rotate breathe; growing the Euclid
    // block shrinks the Pad/Insert columns (pW is derived), so the right-hand
    // sub-panels get smaller at the same time — both requested together.
    // kPadKnobGap is now the SINGLE inter-knob gap used for BOTH the Pre/Post
    // Pad pair AND the Insert pair, so their horizontal spacing matches (was
    // 24 for Pad but a full column width ~104 for Insert — too close vs too far).
    static constexpr int kEucKnobGap   = 18;  // inter-knob gap between Steps/Hits/Rotate
    static constexpr int kPadKnobGap   = 48;  // shared gap for the Pad pair AND the Insert pair
    static constexpr int kPadInsertGap = 6;   // gap between Pad sub-panel and Insert sub-panel borders
    // Logic dropdown fitted width — just wide enough for the widest item ("B not A")
    // plus ComboBox chrome (~5 px left pad + ~20 px arrow).
    static constexpr int kLogicDropW   = 88;

    void apvtsSet(const char* suffix, float v);
    void wireCallbacks();
    void updateRangesA(int steps);
    void updateRangesB(int steps);
    void updateRangesC(int steps);

    // Lazily populated cache of "r{N}_{suffix}" → APVTS parameter pointer,
    // keyed by `const char*` suffix (literal storage outlives the panel).
    // Cleared in setRhythm() because the parameter ID depends on rhythmIndex.
    // Eliminates the juce::String concat + APVTS hash lookup on every drag
    // tick of every knob — saw ~50-200ns per tick × 60 Hz × N visible knobs.
    std::unordered_map<std::string_view, juce::RangedAudioParameter*> paramPtrCache;
};
