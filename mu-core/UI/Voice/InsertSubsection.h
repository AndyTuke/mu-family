#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <atomic>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include "UI/Components/KnobWithLabel.h"
#include "UI/Components/DropdownSelect.h"
#include "UI/Components/MuLookAndFeel.h"
#include "Audio/InsertSlotConfig.h"

namespace juce { class RangedAudioParameter; }
class ProcessorBase;

// Shared per-channel insert-effect panel — identical across mu-products. An
// algorithm dropdown + 4 generic slot knobs whose labels/ranges/skews come from
// mu_ui::kInsertAlgoSlots. Reads/writes the channel's insert params from APVTS
// via the product's prefix (`r0_…`, `v0_…`). Product-specific extras are
// optional hooks the product wires:
//   - mod-arc indicators (isSlotModulated / slotModValue / isPlaying)
//   - GR meter source for Compressor/Limiter (getInsertGR)
//   - the algo-switch multi-write wrapper (runBulkChange) — mu-clid wraps it in
//     its hot-swap loading guard + resync; products without that just call fn().
class InsertSubsection : public juce::Component,
                         private juce::Timer
{
public:
    InsertSubsection(ProcessorBase& processor, juce::String channelPrefix);  // prefix "r" / "v" / …
    ~InsertSubsection() override { stopTimer(); }

    void setChannel(int idx);
    void loadFromChannel();
    void refreshSuffix(const juce::String& suffix);
    void refreshModulatedIndicators();

    void resized() override;

    std::function<void(const juce::String& name, const juce::String& value)> onStatusUpdate;
    std::function<void(int insertAlgo)> onInsertAlgorithmChanged;

    // ── Optional product hooks (null → feature off) ──────────────────────────
    std::function<bool(int slot)>  isSlotModulated;
    std::function<float(int slot)> slotModValue;            // actual (denormalised) modulated value
    std::function<bool()>          isPlaying;
    std::function<const std::atomic<float>*()> getInsertGR; // Comp/Limiter GR meter source
    std::function<void(std::function<void()>)> runBulkChange;  // wrap the 5-write algo switch

private:
    using Id = MuLookAndFeel::ColourIds;

    ProcessorBase& proc;
    juce::String   prefix;          // e.g. "r" / "v"
    int            channelIndex = -1;

    float insertSnapshots     [mu_ui::kInsertAlgoCount][mu_ui::kInsertSlotCount] = {{0.0f}};
    bool  insertSnapshotValid [mu_ui::kInsertAlgoCount] = {};

    DropdownSelect insertAlgo;
    KnobWithLabel  insertParam1 { "P1", Id::knobInsertPad };
    KnobWithLabel  insertParam2 { "P2", Id::knobInsertPad };
    KnobWithLabel  insertParam3 { "P3", Id::knobInsertPad };
    KnobWithLabel  insertParam4 { "P4", Id::knobInsertPad };

    juce::String paramFullId(const char* suffix) const;   // prefix + idx + "_" + suffix
    float        readSlot(int slot) const;                // normalised slot value from APVTS
    int          currentAlgo() const;                     // drvChar from APVTS
    bool         validChannel() const;
    void apvtsSet(const char* suffix, float v);
    void wireCallbacks();
    void configureInsertAlgorithm(int charId);

    std::unordered_map<std::string_view, juce::RangedAudioParameter*> paramPtrCache;

    void timerCallback() override { refreshModulatedIndicators(); }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(InsertSubsection)
};
