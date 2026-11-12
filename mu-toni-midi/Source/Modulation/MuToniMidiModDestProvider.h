#pragma once

#include "UI/TableModDestProvider.h"       // mu-core: the table-driven destination provider
#include "Modulation/MuToniMidiModDest.h"

// The modulator panel's destination dropdown for the table in MuToniMidiModDest.h (UI only: the audio path never includes this).
namespace mu_toni_midi
{

inline ModDestProvider makeModDestProvider()
{
    return mu_mod::makeTableDestProvider(kModDestTable, kNumModDests);   // no insert effect: the dropdown is handed insert algorithm 0
}

} // namespace mu_toni_midi
