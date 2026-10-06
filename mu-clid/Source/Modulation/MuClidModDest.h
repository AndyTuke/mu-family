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
        // The shared builder in mu-Clid's display order (kTable rows; headings come from each
        // row's section): Euclid A / B / C, Pitch, Filter, Amp, then the active insert's slots.
        // Pitch Octave is stepped-only (no smooth octave glide); Amp Release (row 3) is retired.
        static const std::vector<int> kOrder = {
            16, 17, 27, 28, 29, 30,          // Euclid A: Hits, Rotate, Pre Pad, Post Pad, Insert Start, Insert Length
            18, 19, 31, 32, 33, 34,          // Euclid B
            22, 23, 35, 36, 37, 38,          // Euclid C
            20, 9, 24,                       // Pitch: Octave, Semitones, Env Depth
            4, 5, 6, 7, 8, 44,               // Filter: Cutoff, Resonance, Env Attack / Decay / Depth, Low Cut
            25, 0, 1, 2, 26,                 // Amp: Level, Attack, Decay, Sustain, Accent
            10, 11, 12, 13,                  // Insert P1..P4 (labelled per active effect)
        };
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
