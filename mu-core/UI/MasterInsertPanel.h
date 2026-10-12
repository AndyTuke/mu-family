#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <array>
#include <atomic>
#include <functional>
#include "UI/Components/DropdownSelect.h"
#include "UI/Components/KnobWithLabel.h"
#include "UI/Components/MuLookAndFeel.h"
#include "Audio/InsertSlotConfig.h"

namespace mu_ui
{
// The master insert column: two boxes, "Insert 1" and "Insert 2", each an algorithm selector over a
// 2 x 2 grid of the four generic slot knobs (labels, ranges and value text come from the shared
// per-algorithm table). Callback-only: it knows no processor. The owner wires the two callbacks and
// pushes state in with the setters, which never fire a callback. Parameter values are the stored
// normalised 0..1 values (the same numbers the engine's insert parameters hold). Width is
// MuLookAndFeel::kMixerInsertPanelW. Knobs that do not apply are greyed out (the Vocoder's P2 to P4
// with a noise wave) and the Compressor / Limiter show their gain reduction.
class MasterInsertPanel : public juce::Component
{
public:
    static constexpr int kSlots  = 2;
    static constexpr int kParams = kInsertSlotCount;   // 4

    MasterInsertPanel();

    // User changes only. A new algorithm restores that algorithm's last values (or its defaults),
    // fires onAlgorithm first, then onParam for each of the four parameters.
    std::function<void(int slot, int algorithm)>                onAlgorithm;
    std::function<void(int slot, int param, float normalised)>  onParam;

    // Push state in (no callbacks). setAlgorithm re-configures the knobs for the algorithm from the
    // values held for the slot; call setParam afterwards to load the stored values.
    void  setAlgorithm(int slot, int algorithm);
    void  setParam(int slot, int param, float normalised);
    int   getAlgorithm(int slot) const;
    float getParam(int slot, int param) const;

    // The gain-reduction meter of a slot's Compressor / Limiter (shown on knob P2 for those two
    // algorithms only). The source must outlive the panel; null shows no meter.
    void  setGainReductionSource(int slot, const std::atomic<float>* source);

    DropdownSelect& getAlgorithmSelect(int slot)  { return slots[(size_t) slot].algo; }
    KnobWithLabel&  getKnob(int slot, int param)  { return slots[(size_t) slot].knobs[(size_t) param]; }

    // Height of the two boxes with their plates, at the current display size.
    static int preferredHeight();

    void resized() override;
    void paint(juce::Graphics&) override;

private:
    using Id = MuLookAndFeel::ColourIds;

    struct Slot
    {
        DropdownSelect algo;
        std::array<KnobWithLabel, kParams> knobs { KnobWithLabel("P1", Id::knobInsertPad),
                                                   KnobWithLabel("P2", Id::knobInsertPad),
                                                   KnobWithLabel("P3", Id::knobInsertPad),
                                                   KnobWithLabel("P4", Id::knobInsertPad) };
        int   algorithm = 0;
        const std::atomic<float>* gainReduction = nullptr;   // offered by the owner
        const std::atomic<float>* shownGainReduction = nullptr;   // what knob P2 currently reads
        float norm[kParams] = {};
        // Per-algorithm memory of the last actual values, so cycling back restores them.
        float snapshot[kInsertAlgoCount][kParams] = {};
        bool  snapshotValid[kInsertAlgoCount] = {};
    };

    void configureKnobs(int slot);
    void applyDependencies(int slot);
    void userChangedAlgorithm(int slot, int newAlgorithm);
    juce::Rectangle<int> boxBounds(int slot) const;

    std::array<Slot, kSlots> slots;
};
} // namespace mu_ui
