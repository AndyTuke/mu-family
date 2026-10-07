#pragma once

#include "UI/ModulatorEditor.h"                     // mu-core: ModDestProvider
#include "UI/Components/DropdownSelect.h"
#include "Modulation/ModulationDestinations.h"      // mu-clid kTable
#include "UI/ModTargetDropdown.h"                 // mu-core: the shared destination-dropdown builder

#include <string>

// mu-clid's modulation-destination provider — drives the ModulatorEditor /
// ModMatrixPanel destination dropdowns. Lifted out of the inline header
// definition so mu-core's ModulatorEditor no longer depends on mu-clid's
// kTable. The provider's three callbacks share the kTable as their single
// source of truth so a renamed entry stays consistent across populate /
// resolveId / findDropdownId.

namespace mu_clid
{

inline ModDestProvider makeModDestProvider()
{
    ModDestProvider p;

    p.populate = [](DropdownSelect& dd, int driveChar, bool steppedMode)
    {
        // The shared builder in mu-Clid's display order (ModDest::kDropdownOrder).
        // Pitch Octave is stepped-only (no smooth octave glide); Amp Release (row 3) is retired.
        static const std::vector<int> kOrder(std::begin(ModDest::kDropdownOrder), std::end(ModDest::kDropdownOrder));
        mu_mod::populateDropdown(dd, ModDest::kTable,
            { driveChar,
              [steppedMode](const mu_mod::ModTarget& t) { return steppedMode || std::strcmp(t.id, "pitch.octave") != 0; },
              kOrder });
    };

    wireTableModDestResolve(p,
        [](int i) { return std::string(ModDest::kTable[i].id); },
        ModDest::kTableSize);

    return p;
}

} // namespace mu_clid
