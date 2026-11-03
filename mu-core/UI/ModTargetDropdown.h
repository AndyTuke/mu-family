#pragma once

#include "Modulation/ModTarget.h"
#include "UI/Components/DropdownSelect.h"
#include "Audio/InsertSlotConfig.h"     // kInsertAlgoSlots — per-algorithm insert slot labels
#include "Audio/AlgorithmNames.h"       // kInsertAlgorithmNames

#include <cstring>
#include <functional>
#include <vector>

// Fill a modulation-destination dropdown from a product's target table (the family standard,
// Modulation/ModTarget.h). One shared builder, so every product's list behaves the same:
//   - item id = row index + 1 (the family contract; saved assignments resolve by id string,
//     so a hidden row never shifts another row's id)
//   - a section heading whenever the section changes; reserved rows (param == nullptr) skipped
//   - the shared insert slots (insert.p1..p4) appear only when an insert effect is active,
//     labelled with that effect's slot names under the effect's name (hidden slots omitted)
//   - the mixer's effect send (sendEff) is labelled with the effect bus's current effect
namespace mu_mod
{

struct DropdownOptions
{
    int                                   insertAlgo = 0;   // active insert effect (0 = none)
    std::function<bool(const ModTarget&)> show;             // optional extra filter (e.g. stepped-only targets)
    std::vector<int>                      order;            // display order of row indices (empty = table order)
    juce::String                          effectSendName;   // effect bus's current effect ("" = keep the table label)
};

inline void populateDropdown(DropdownSelect& dd, const ModTarget* table, int count,
                             const DropdownOptions& options = {})
{
    const bool algoActive = options.insertAlgo > 0 && options.insertAlgo < mu_audio::kInsertAlgorithmCount;
    juce::String currentHeading;
    bool first = true;

    // Add one row: filter it, swap in insert-slot labels, open a heading when the section changes.
    auto addRow = [&](int i)
    {
        if (i < 0 || i >= count) return;
        const auto& t = table[i];
        if (t.param == nullptr || (options.show && ! options.show(t))) return;

        juce::String label   = t.label;
        juce::String heading = t.section != nullptr ? t.section : "";
        if (std::strncmp(t.id, "insert.p", 8) == 0)
        {
            const int slot = t.id[8] - '1';
            if (! algoActive || slot < 0 || slot >= mu_ui::kInsertSlotCount) return;
            const auto& sl = mu_ui::kInsertAlgoSlots[options.insertAlgo][slot];
            if (sl.label == nullptr) return;   // this effect doesn't use the slot
            label   = sl.label;
            heading = mu_audio::kInsertAlgorithmNames[options.insertAlgo];
        }
        else if (options.effectSendName.isNotEmpty() && std::strcmp(t.param, "sendEff") == 0)
            label = options.effectSendName + " Send";

        if (heading.isNotEmpty() && (first || heading != currentHeading))
            dd.addSectionHeading(heading);
        currentHeading = heading;
        first = false;
        dd.addItem(label, i + 1);
    };

    if (options.order.empty())
        for (int i = 0; i < count; ++i) addRow(i);
    else
        for (int i : options.order) addRow(i);
}

template <std::size_t N>
inline void populateDropdown(DropdownSelect& dd, const ModTarget (&table)[N], const DropdownOptions& options = {})
{
    populateDropdown(dd, table, (int) N, options);
}

} // namespace mu_mod
