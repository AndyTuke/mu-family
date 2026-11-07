#pragma once

#include "UI/ModulatorEditor.h"           // mu-core: ModDestProvider + wireTableModDestResolve
#include "UI/Components/DropdownSelect.h"
#include "UI/ModTargetDropdown.h"         // mu-core: the shared destination-dropdown builder
#include "Modulation/MuToniMidiModDest.h"

// The modulator panel's destination dropdown for the table in MuToniMidiModDest.h (UI only: the audio path never includes this).
namespace mu_toni_midi
{

inline ModDestProvider makeModDestProvider()
{
    ModDestProvider p;
    p.unitsPerDirection = [](const std::string&) { return 0; };
    p.populate = [](DropdownSelect& dd, int /*driveChar*/, bool /*steppedMode*/)
    {
        mu_mod::populateDropdown(dd, kModDestTable, { 0 });   // no insert effect to list
    };
    wireTableModDestResolve(p, [](int i) { return std::string(kModDestTable[i].id); }, kNumModDests);
    return p;
}

} // namespace mu_toni_midi
