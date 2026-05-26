#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <string_view>
#include <unordered_map>
#include "UI/Components/KnobWithLabel.h"
#include "UI/Components/DropdownSelect.h"
#include "UI/Components/MuClidLookAndFeel.h"
#include "Audio/InsertSlotConfig.h"

namespace juce { class RangedAudioParameter; }
class PluginProcessor;

// Per-rhythm insert effect panel. Stage 36: a single 4-slot generic config
// (Param1..Param4) replaces the prior 9 named fields. The 14 algorithms each
// label and range their slots through mu_ui::kInsertAlgoSlots — adding a new
// algorithm is one row in that table + one InsertProcessor dispatch entry.
class InsertSubsection : public juce::Component
{
public:
    explicit InsertSubsection(PluginProcessor& p);

    void setRhythm(int ri);
    void loadFromRhythm();
    void refreshSuffix(const juce::String& suffix);
    void refreshModulatedIndicators();

    void resized() override;

    std::function<void(const juce::String& name, const juce::String& value)> onStatusUpdate;
    std::function<void(int insertAlgo)> onInsertAlgorithmChanged;

private:
    using Id = MuClidLookAndFeel::ColourIds;

    PluginProcessor& proc;
    int rhythmIndex = -1;

    // A/B-style per-algorithm snapshot: when the user switches algos we save
    // the current 4 slot values so cycling back restores them instead of
    // reverting to mu_ui::kInsertAlgoDefaults. Stored as ACTUAL values so the
    // restore path can re-normalise via actualToNorm into APVTS.
    float insertSnapshots     [14][mu_ui::kInsertSlotCount] = {{0.0f}};
    bool  insertSnapshotValid [14] = {};

    DropdownSelect insertAlgo;
    // 4 generic Param knobs. Labels / ranges / formatters are driven each
    // algo switch by mu_ui::configureKnobFromSlot — no hard-coded labels here.
    KnobWithLabel  insertParam1 { "P1", Id::knobInsertPad };
    KnobWithLabel  insertParam2 { "P2", Id::knobInsertPad };
    KnobWithLabel  insertParam3 { "P3", Id::knobInsertPad };
    KnobWithLabel  insertParam4 { "P4", Id::knobInsertPad };

    void apvtsSet(const char* suffix, float v);
    void wireCallbacks();
    void configureInsertAlgorithm(int charId);

    std::unordered_map<std::string_view, juce::RangedAudioParameter*> paramPtrCache;
};
