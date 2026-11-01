#pragma once

#include "Audio/AlgorithmNames.h"   // mu-core: the family's filter list, in display order

#include <juce_core/juce_core.h>

#include <vector>

// The layer filter's choices: Off, then the family's filter types in the order every mu product shows them. The choice index is what
// the parameter stores; the code is what mu-core's MultiModeFilter takes (-1 for Off).
namespace mu_tilate
{

struct FilterChoice
{
    juce::String name;
    int          code = -1;
};

inline const std::vector<FilterChoice>& filterChoices()
{
    static const std::vector<FilterChoice> list = []
    {
        std::vector<FilterChoice> v;
        v.push_back({ "Off", -1 });
        mu_audio::populateFilterTypeDropdown([&v](const char* name, int id) { v.push_back({ name, id - 1 }); });   // the item id is the code + 1
        return v;
    }();
    return list;
}

// The MultiModeFilter code for a choice index, or -1 (Off) for Off and for an index out of range.
inline int filterCodeOfChoice(int choice) noexcept
{
    const auto& c = filterChoices();
    return choice >= 0 && choice < (int) c.size() ? c[(size_t) choice].code : -1;
}

} // namespace mu_tilate
