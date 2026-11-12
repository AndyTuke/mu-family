#pragma once

#include "UI/ModulatorEditor.h"    // ModDestProvider
#include "UI/ModTargetDropdown.h"  // mu_mod::populateDropdown

#include <string>

// A destination provider built from nothing but a target table (the family standard, Modulation/ModTarget.h): the dropdown, the
// id <-> dropdown-id plumbing and the reverse lookup. A layer type that publishes its targets in the LayerTypeInfo (modTargets /
// numModTargets) gets its provider from here, so a new type's targets appear in the modulator panel with no framework edit.
// A type with its own rules (mu-Tant's stepped-only octave, mu-Clid's effect-send label) builds its own provider instead.
namespace mu_mod
{

inline ModDestProvider makeTableDestProvider(const ModTarget* table, int count)
{
    ModDestProvider p;
    // Stepped-snap units come from the row (0 = continuous, no snapping).
    p.unitsPerDirection = [table, count](const std::string& id)
    {
        for (int i = 0; i < count; ++i)
            if (table[i].param != nullptr && id == table[i].id) return table[i].unitsPerDirection;
        return 0;
    };

    // In Smooth mode, stepped-only targets (octave) are left out of the list.
    p.populate = [table, count](DropdownSelect& dd, int driveChar, bool steppedMode)
    {
        populateDropdown(dd, table, count,
            { driveChar, [steppedMode](const ModTarget& t) { return steppedMode || ! t.steppedOnly; } });   // driveChar = the active insert effect
    };

    wireTableModDestResolve(p, [table](int i) { return std::string(table[i].id); }, count);
    return p;
}

} // namespace mu_mod
