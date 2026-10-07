#pragma once

#include "UI/Components/DropdownSelect.h"
#include "Persistence/PresetFiles.h"
#include <vector>

namespace mu_ui
{

// Fill a preset selector from `entries` (mu_pp::listPresetsByCategory): item id = position + 1
// in `files`, which receives each entry's file; a heading opens each category when the presets
// span more than one.
inline void fillPresetDropdown(DropdownSelect& dd, std::vector<juce::File>& files,
                               const std::vector<mu_pp::PresetEntry>& entries)
{
    files.clear();
    dd.clear();
    bool multiCat = false;
    for (const auto& e : entries)
        if (e.category.compareIgnoreCase(entries.front().category) != 0) { multiCat = true; break; }

    juce::String currentCat;
    for (const auto& e : entries)
    {
        if (multiCat && e.category != currentCat)
        {
            currentCat = e.category;
            dd.addSectionHeading(currentCat);
        }
        files.push_back(e.file);
        dd.addItem(e.name, (int) files.size());
    }
}

} // namespace mu_ui
