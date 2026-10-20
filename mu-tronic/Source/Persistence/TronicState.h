#pragma once

#include "Persistence/LayerState.h"
#include "Sequencer/LayerTypeInfo.h"
#include "MuLimits.h"

#include <juce_data_structures/juce_data_structures.h>

#include <vector>

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
//
// A slot node holds the rows of its LIVE type only, ids without the type's prefix: the type attribute selects the
// prefix on load, and the other families' prefixes keep their defaults and are never written.
namespace mu_tronic
{

inline constexpr const char* kStateType           = "MuTronicState";
inline constexpr const char* kFullPresetTag       = "MuTronicPreset";
inline constexpr const char* kFullPresetExtension = "muTronic";
inline constexpr const char* kAppName             = "muTronic";   // the settings file and content folder

// The parameter-id prefixes a slot's own rows may carry: one per layer-type family, `<letter><slot>_`
// (r Rhythm, v Pattern, a Arp, t Track: the five mu-On engines share t). Every family is listed from the start,
// so the layout does not change as the types are registered one at a time; a family with no registered type
// simply has no parameters. Exactly one prefix is live per slot, the one of the slot's current type
// (ProcessorBase::setLayerType names it), and only that prefix is saved or applied.
inline constexpr const char* kPrefixLetters = "rvat";

inline juce::String slotPrefix(char letter, int slot)
{
    return juce::String::charToString((juce::juce_wchar) letter) + juce::String(slot) + "_";
}

inline std::vector<juce::StringArray> slotPrefixSets()
{
    std::vector<juce::StringArray> out;
    for (int s = 0; s < mu_limits::kMaxLayers; ++s)
    {
        juce::StringArray set;
        for (const char* l = kPrefixLetters; *l != 0; ++l) set.add(slotPrefix(*l, s));
        out.push_back(set);
    }
    return out;
}

// The type a slot node names; Empty for an absent node, attribute or unknown name.
inline mu_core::LayerTypeId slotNodeType(const juce::ValueTree& slotNode)
{
    const auto name = slotNode.getProperty("type").toString();
    return mu_core::layerTypeFromName(name.toRawUTF8());
}

} // namespace mu_tronic
