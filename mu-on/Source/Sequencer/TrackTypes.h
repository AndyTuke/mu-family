#pragma once

#include "Sequencer/Track.h"
#include "Sequencer/LayerTypeInfo.h"   // mu-core: mu_core::LayerTypeId, LayerTypeInfo
#include "Persistence/LayerState.h"    // mu-core: the saved row tag
#include "Persistence/TrackParams.h"

#include <memory>

// mu-On's five layer types (docs/mu-tronic/design.md 1.4): Kick, Bass, Hat, Snare and Rumble are one class,
// Track, whose type is the engine it runs. All five share the parameter prefix `t` and the extension .muTrack;
// the id, not the prefix, identifies the type. Message-thread data only: the constants describe and
// construct, they never dispatch (rendering stays virtual on Layer).
namespace mu_on
{

// The type a track preset (.muTrack) carries: its engine row ("eng"), or, in a preset saved when the lanes
// were fixed, the instrument named by its "lane" property. Empty when the tree names neither.
inline mu_core::LayerTypeId typeFromPreset(const juce::ValueTree& preset)
{
    for (int i = 0; i < preset.getNumChildren(); ++i)
        if (const auto row = preset.getChild(i); row.hasType(mu_pp::kRowTag) && row.getProperty("id").toString() == "eng")
            return layerTypeOf(engineFromIndex(juce::roundToInt((double) row.getProperty("x", 0))));
    const int lane = engineNames().indexOf(preset.getProperty("lane").toString());
    return lane >= 0 ? layerTypeOf(engineFromIndex(lane)) : mu_core::LayerTypeId::Empty;
}

// A fresh Track for `slot`, not yet bound or prepared. Which engine it runs is its slot's `t{N}_eng`
// parameter: a host of the type sets that parameter to engineOfType(id) when it creates the layer.
inline std::unique_ptr<Layer> createTrack(int slot)
{
    auto t = std::make_unique<Track>();
    t->slot = slot;
    return t;
}

// The five type descriptions over the one class.
inline mu_core::LayerTypeInfo makeTrackTypeInfo(EngineType engine) noexcept
{
    mu_core::LayerTypeInfo info;
    info.id = layerTypeOf(engine);
    info.name = engineName(engine);
    info.group = "mu-On";
    info.paramPrefixLetter = 't';
    info.presetExtension = ".muTrack";
    info.addLayerParams = &addLayerParams;   // one table per prefix: every engine's rows exist on every track
    info.create = &createTrack;
    return info;
}

inline const mu_core::LayerTypeInfo kKickType   = makeTrackTypeInfo(EngineType::Kick);
inline const mu_core::LayerTypeInfo kBassType   = makeTrackTypeInfo(EngineType::Bass);
inline const mu_core::LayerTypeInfo kHatType    = makeTrackTypeInfo(EngineType::Hat);
inline const mu_core::LayerTypeInfo kSnareType  = makeTrackTypeInfo(EngineType::Snare);
inline const mu_core::LayerTypeInfo kRumbleType = makeTrackTypeInfo(EngineType::Rumble);

} // namespace mu_on
