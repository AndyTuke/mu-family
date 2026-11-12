#pragma once

#include "Modulation/MuTilateModDest.h"
#include "UI/Components/DropdownSelect.h"
#include "UI/TableModDestProvider.h"   // mu-core: the table-driven destination provider

// The modulation-destination provider the shared modulator panel uses for Phrase layers: the target table's rows in the
// shared dropdown, resolved through the same table (kept out of Modulation/ so the table itself stays UI-free).
namespace mu_tilate
{

inline ModDestProvider makeModDestProvider()
{
    return mu_mod::makeTableDestProvider(kModDestTable, kNumModDests);
}

} // namespace mu_tilate
