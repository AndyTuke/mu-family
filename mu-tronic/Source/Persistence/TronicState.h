#pragma once

#include "Persistence/LayerState.h"
#include "Sequencer/LayerTypeInfo.h"
#include "MuLimits.h"

#include <juce_data_structures/juce_data_structures.h>

// mu-Tronic's saved shape (docs/mu-tronic/design.md 3.4). The family's composed state, plus one attribute per slot:
//
//   <MuTronicState format="2" numVoices="16" voiceColours="...">
//     <Globals> rows: mixer, FX, master </Globals>
//     <Slots>
//       <Slot idx="3" type="Arp" name="Lead"> rows + extras </Slot>
//       <Slot idx="4" type="Empty"/>              (an absent <Slot> is an Empty layer too)
//     </Slots>
//   </MuTronicState>
//
// `numVoices` and `voiceColours` keep the family's saved names. A slot's type is saved by NAME, never by number
// (layerTypeName), and an absent or unknown type is Empty ("missing means default"). `name` is written only when
// the user typed one; its absence means the default name.
namespace mu_tronic
{

inline constexpr const char* kStateType           = "MuTronicState";
inline constexpr const char* kFullPresetTag       = "MuTronicPreset";
inline constexpr const char* kFullPresetExtension = "muTronic";
inline constexpr const char* kAppName             = "muTronic";   // the settings file and content folder

// The parameter-id prefix the composed state reserves for a slot's own rows. No parameter carries it until a
// layer type declares its rows; the per-type prefixes (r a v t) replace it when the first type is registered.
inline juce::String slotPrefix(int slot) { return "mtrn" + juce::String(slot) + "_"; }

inline juce::StringArray slotPrefixes()
{
    juce::StringArray out;
    for (int s = 0; s < mu_limits::kMaxLayers; ++s) out.add(slotPrefix(s));
    return out;
}

// The type a slot node names; Empty for an absent node, attribute or unknown name.
inline mu_core::LayerTypeId slotNodeType(const juce::ValueTree& slotNode)
{
    const auto name = slotNode.getProperty("type").toString();
    return mu_core::layerTypeFromName(name.toRawUTF8());
}

} // namespace mu_tronic
