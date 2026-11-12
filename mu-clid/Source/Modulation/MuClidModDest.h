#pragma once

#include "UI/TableModDestProvider.h"               // mu-core: the table-driven destination provider
#include "Modulation/ModulationDestinations.h"      // mu-clid kTable

#include <string>

// mu-clid's modulation-destination provider — drives the ModulatorEditor /
// ModMatrixPanel destination dropdowns. Lifted out of the inline header
// definition so mu-core's ModulatorEditor no longer depends on mu-clid's
// kTable. The provider's three callbacks share the kTable as their single
// source of truth so a renamed entry stays consistent across populate /
// resolveId / findDropdownId.

namespace mu_clid
{

// `effectName` returns the mixer effect bus's current effect, which names the effect-send target. The shared table provider does the
// rest: the table's stepped-only row (Pitch Octave: no smooth octave glide), the retired rows skipped, and mu-Clid's display order.
inline ModDestProvider makeModDestProvider(std::function<juce::String()> effectName)
{
    static const std::vector<int> kOrder(std::begin(ModDest::kDropdownOrder), std::end(ModDest::kDropdownOrder));
    return mu_mod::makeTableDestProvider(ModDest::kTable, ModDest::kTableSize, std::move(effectName), kOrder);
}

} // namespace mu_clid
