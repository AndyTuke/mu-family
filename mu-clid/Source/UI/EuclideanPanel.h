#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <string_view>
#include <unordered_map>
#include "UI/Components/KnobWithLabel.h"
#include "UI/Components/SegmentControl.h"
#include "UI/Components/SlideSwitch.h"
#include "UI/Components/DropdownSelect.h"
#include "UI/Components/MuLookAndFeel.h"
#include "Sequencer/HitGenerator.h"

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

    // The Logic dropdown is wired here but placed by the host (beside the rhythm circle),
    // so the three Euclid rows can share the panel's height equally. The host adds it as
    // its own child and sizes it to kLogicDropW x kLogicDropH.
    DropdownSelect& getLogicControl() noexcept { return logicCtrl; }

    // Logic dropdown size: the narrowest that still fits the widest item ("B not A")
    // beside the ComboBox chrome (6 px left pad + arrow, 24 px in all).
    // Logic modes (APVTS "logic" index order) as logic symbols: A∨B, A∧B, A⊕B, A∧¬B, B∧¬A.
    static constexpr int kNumLogicModes = 5;
    static constexpr const char* kLogicSymbols[kNumLogicModes] = {
        "A \xe2\x88\xa8 B", "A \xe2\x88\xa7 B", "A \xe2\x8a\x95 B", "A\xe2\x88\xa7\xc2\xac" "B", "B\xe2\x88\xa7\xc2\xac" "A" };
    static constexpr int kLogicDropW = 64;
    static constexpr int kLogicDropH = 15;

    void resized() override;
    void paint(juce::Graphics&) override;

    // Left/right border inset inside the panel — used by LiteEditor to align
    // controls below the panel with the Steps knobs above.
    static constexpr int kPanelInset = MuLookAndFeel::kSpaceXS;

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
    SlideSwitch    prePadModeA  { "Pad", "Mute", Id::knobPrePad  };
    SlideSwitch    postPadModeA { "Pad", "Mute", Id::knobPostPad };
    KnobWithLabel insertStA    { "Insert Start",  Id::knobInsertPad };
    KnobWithLabel insertLenA   { "Insert Length", Id::knobInsertPad };
    SlideSwitch    insertModeA  { "Pad", "Mute", Id::knobInsertPad };

    // ── Rhythm voice behaviour + Logic ────────────────────────────────
    // Legato and Mono are per-rhythm voice behaviour, not padding, so they live in their
    // own column between the Euclid knobs and the Pad sub-panel, stacked one above the other.
    SlideSwitch legatoCtrl { "Trig", "Leg",  Id::segmentActiveBorder  };
    SlideSwitch monoCtrl   { "Poly", "Mono", Id::segmentWarningBorder };
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
    SlideSwitch    prePadModeB  { "Pad", "Mute", Id::knobPrePad  };
    SlideSwitch    postPadModeB { "Pad", "Mute", Id::knobPostPad };
    KnobWithLabel insertStB    { "Insert Start",  Id::knobInsertPad };
    KnobWithLabel insertLenB   { "Insert Length", Id::knobInsertPad };
    SlideSwitch    insertModeB  { "Pad", "Mute", Id::knobInsertPad };

    // ── Euclid C (Accent) ────────────────────────────────────────────────────
    KnobWithLabel stepsC      { "Steps",         Id::knobLevel     };
    KnobWithLabel hitsC       { "Hits",          Id::knobLevel     };
    KnobWithLabel rotC        { "Rotate",        Id::knobLevel     };
    KnobWithLabel prePadC      { "Pre Pad",       Id::knobPrePad    };
    KnobWithLabel postPadC     { "Post Pad",      Id::knobPostPad   };
    SlideSwitch    prePadModeC  { "Pad", "Mute", Id::knobPrePad  };
    SlideSwitch    postPadModeC { "Pad", "Mute", Id::knobPostPad };
    KnobWithLabel insertStC    { "Insert Start",  Id::knobInsertPad };
    KnobWithLabel insertLenC   { "Insert Length", Id::knobInsertPad };
    SlideSwitch    insertModeC  { "Pad", "Mute", Id::knobInsertPad };

    static constexpr int kOuter   = MuLookAndFeel::kSpaceXS;
    // Left margin: wider than kOuter so the boxes and plates clear the panel's corner screws
    // (screw reach from the panel edge, less the RhythmPanel inset round this component).
    static constexpr int kOuterL  = juce::jmax(kOuter, MuLookAndFeel::kScrewedChannelGap);

    // Rows, top to bottom: [name plate | plate gap | raised boxes], kRowGap apart, with kOuter
    // above the first and below the last, so the gaps between rows, between boxes and the
    // panel margins all match. Any leftover pixel is split above and below.
    static constexpr int kPlateH     = MuLookAndFeel::kNamePlateH;
    static constexpr int kPlateW     = 58;   // one width for all three plates
    static constexpr int kPlateGap   = 2;
    static constexpr int kRowGap     = MuLookAndFeel::kSpaceXS;
    static constexpr int kRowsInnerH = MuLookAndFeel::kEuclidInnerH - 2 * kOuter;
    static constexpr int kBoxH       = (kRowsInnerH - 3 * (kPlateH + kPlateGap) - 2 * kRowGap) / 3;
    static constexpr int kRowPitch   = kPlateH + kPlateGap + kBoxH + kRowGap;
    static constexpr int kRowsTop    = kOuter + (kRowsInnerH - (3 * kRowPitch - kRowGap)) / 2;
    static constexpr int rowY(int i) { return kRowsTop + i * kRowPitch; }
    static constexpr int boxY(int i) { return rowY(i) + kPlateH + kPlateGap; }

    // Euclid-row spacing.
    // kEucKnobGap widened so Steps/Hits/Rotate breathe; growing the Euclid
    // block shrinks the Pad/Insert columns (pW is derived), so the right-hand
    // sub-panels get smaller at the same time — both requested together.
    // kPadKnobGap is now the SINGLE inter-knob gap used for BOTH the Pre/Post
    // Pad pair AND the Insert pair, so their horizontal spacing matches (was
    // 24 for Pad but a full column width ~104 for Insert — too close vs too far).
    static constexpr int kEucKnobGap   = MuLookAndFeel::kKnobGapRow;  // inter-knob gap between Steps/Hits/Rotate
    static constexpr int kPadKnobGap   = MuLookAndFeel::kKnobGapPair;  // shared gap for the Pad pair AND the Insert pair
    static constexpr int kPadInsertGap = kRowGap;   // gap between the boxes in a row, matching the gap between rows
    // Pad / Insert sub-panels: each Pad/Mute slide switch sits beside its knob, so the
    // knobs get the sub-panel's full height and run at Size 1.
    static constexpr int kSwitchGap    = MuLookAndFeel::kSpaceS;      // knob to its own switch
    static constexpr int kInsKnobGap   = MuLookAndFeel::kKnobGapRow;  // between the two insert knobs
    // Column for the Legato / Mono switches, on the far right after the Insert sub-panel;
    // its width comes out of the Pad and Insert sub-panels.
    static constexpr int kModeColPad   = MuLookAndFeel::kSpaceM;
    static constexpr int kModeColW     = kModeColPad * 2 + MuLookAndFeel::kSlideSwitchW;
    static constexpr int kModeSwGap    = MuLookAndFeel::kSpaceL;      // between the two switches

    void apvtsSet(const char* suffix, float v);
    void wireCallbacks();
    // Fit each ring's knob ranges to its current layout (step count, pad budget, insert
    // bounds — see HitGenerator). Read from the rhythm, so call after the APVTS write.
    void updateRangesA();
    void updateRangesB();
    void updateRangesC();
    // sfx = the ring's APVTS suffixes { prePad, postPad, insLen, insSt } (string literals).
    void updateRanges(const HitGenerator& g, const char* const (&sfx)[4],
                      KnobWithLabel& hits, KnobWithLabel& rot,
                      KnobWithLabel& pre, KnobWithLabel& post,
                      KnobWithLabel& insSt, KnobWithLabel& insLen);

    // Insert Start is stored relative to the Euclid section; the knob shows the absolute
    // step the insert begins on, counted from 1. This is the shown-minus-stored offset
    // for ring 0 / 1 / 2 (A / B / Accent).
    int insertStartDisplayOffset(int ring) const;

    // Plain-English descriptions of the Legato / Mono choices for the status bar.
    static juce::String legatoExplanation(int modeIndex);
    static juce::String monoExplanation(int modeIndex);

    // Plain-English description of a Pad/Mute choice for the status bar.
    enum class PadZone { Start, End, Insert };
    static juce::String padModeExplanation(PadZone zone, int modeIndex);

    // Lazily populated cache of "r{N}_{suffix}" → APVTS parameter pointer,
    // keyed by `const char*` suffix (literal storage outlives the panel).
    // Cleared in setRhythm() because the parameter ID depends on rhythmIndex.
    // Eliminates the juce::String concat + APVTS hash lookup on every drag
    // tick of every knob — saw ~50-200ns per tick × 60 Hz × N visible knobs.
    std::unordered_map<std::string_view, juce::RangedAudioParameter*> paramPtrCache;
};
