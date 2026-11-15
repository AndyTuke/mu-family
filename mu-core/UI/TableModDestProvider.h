#pragma once

#include "UI/ModulatorEditor.h"    // ModDestProvider
#include "UI/ModTargetDropdown.h"  // mu_mod::populateDropdown

#include <functional>
#include <string>
#include <vector>

// A destination provider built from nothing but a target table (the family standard, Modulation/ModTarget.h): the dropdown, the
// id <-> dropdown-id plumbing and the reverse lookup. A layer type that publishes its targets in the LayerTypeInfo (modTargets /
// numModTargets) gets its provider from here, so a new type's targets appear in the modulator panel with no framework edit.
// A type with its own rules (mu-Tant's stepped-only octave, mu-Clid's effect-send label) builds its own provider instead.
namespace mu_mod
{

// `effectName` (optional) returns the mixer effect bus's current effect, which names the effect-send target; `order` (optional) is a
// display order of row indices for a table whose own order is not the one to show (see DropdownOptions::order).
inline ModDestProvider makeTableDestProvider(const ModTarget* table, int count,
                                             std::function<juce::String()> effectName = {}, std::vector<int> order = {})
{
    ModDestProvider p;
    // Stepped-snap units come from the row (0 = continuous, no snapping).
    p.unitsPerDirection = [table, count](const std::string& id)
    {
        const auto* row = findTarget(table, (std::size_t) count, id);
        return row != nullptr ? row->unitsPerDirection : 0;
    };

    // In Smooth mode, stepped-only targets (octave) are left out of the list.
    p.populate = [table, count, effectName, order](DropdownSelect& dd, int driveChar, bool steppedMode)
    {
        populateDropdown(dd, table, count,
            { driveChar, [steppedMode](const ModTarget& t) { return steppedMode || ! t.steppedOnly; },   // driveChar = the active insert effect
              order, effectName ? effectName() : juce::String() });
    };

    wireTableModDestResolve(p, [table](int i) { return std::string(table[i].id); }, count);
    return p;
}

} // namespace mu_mod
