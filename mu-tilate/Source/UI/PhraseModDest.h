#pragma once

#include "Modulation/MuTilateModDest.h"
#include "UI/Components/DropdownSelect.h"
#include "UI/ModTargetDropdown.h"   // mu-core: the shared destination-dropdown builder
#include "UI/ModulatorEditor.h"     // mu-core: ModDestProvider + wireTableModDestResolve

// The modulation-destination provider the shared modulator panel uses for Phrase layers: the target table's rows in the
// shared dropdown, resolved through the same table (kept out of Modulation/ so the table itself stays UI-free).
namespace mu_tilate
{

inline ModDestProvider makeModDestProvider()
{
    ModDestProvider p;
    p.unitsPerDirection = [](const std::string&) { return 0; };   // no stepped-snap graphic

    p.populate = [](DropdownSelect& dd, int driveChar, bool /*steppedMode*/)
    {
        mu_mod::DropdownOptions options;
        options.insertAlgo = driveChar;         // the active insert effect
        options.sectionSubmenus = true;         // some thirty targets: one submenu per section
        mu_mod::populateDropdown(dd, kModDestTable, options);
    };

    wireTableModDestResolve(p, [](int i) { return std::string(kModDestTable[i].id); }, kNumModDests);
    return p;
}

} // namespace mu_tilate
