#pragma once

#include "Sequencer/Track.h"
#include "Sequencer/LayerTypeInfo.h"   // mu-core: mu_core::LayerTypeId, LayerTypeInfo
#include "Persistence/LayerState.h"    // mu-core: the saved row tag
#include "Persistence/TrackParams.h"

#include <memory>

// mu-On's six layer types (docs/mu-tronic/design.md 1.4): Kick, Bass, Hat, Snare, Rumble and FM Perc are one class,
// Track, whose type is the engine it runs. All six share the parameter prefix `t` and the extension .muTrack;
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

// A fresh Track for `slot`, not yet bound or prepared, locked to the engine of `type`: its engineSetting() is
// that engine whatever `t{N}_eng` holds, and its completeSetup writes the parameter to match (a record, so a
// saved row or .muTrack names the right engine).
inline std::unique_ptr<Layer> createTrack(int slot, mu_core::LayerTypeId type)
{
    auto t = std::make_unique<Track>();
    t->slot = slot;
    t->lockEngine(engineOfType(type));
    // A new slot makes a sound at once: the engine's default groove (Rumble has none), as mu-On gives a new track.
    // Set here, before any saved row or initial node is applied, and StepRow::deserialise rewrites every step, so a
    // restore (an empty row included) is unaffected.
    t->row.loadDefaultGroove(engineOfType(type));
    return t;
}

// The chooser card's line for an engine (one line, from the products' own doc wording).
inline constexpr const char* trackDescriptor(EngineType e) noexcept
{
    switch (e)
    {
        case EngineType::Kick:   return "909 kick drum";
        case EngineType::Bass:   return "Bass line";
        case EngineType::Hat:    return "Hi-hat";
        case EngineType::Snare:  return "Snare";
        case EngineType::Rumble: return "Rumble tail from a kick";
        case EngineType::FmPerc: return "FM toms and percussion";
    }
    return "";
}

// The one letter on the engine's disc (mu-On's own sidebar picture).
inline constexpr const char* trackInitial(EngineType e) noexcept
{
    switch (e)
    {
        case EngineType::Kick:   return "K";
        case EngineType::Bass:   return "B";
        case EngineType::Hat:    return "H";
        case EngineType::Snare:  return "S";
        case EngineType::Rumble: return "R";
        case EngineType::FmPerc: return "F";
    }
    return "";
}

// The six type descriptions over the one class.
inline mu_core::LayerTypeInfo makeTrackTypeInfo(EngineType engine) noexcept
{
    mu_core::LayerTypeInfo info;
    info.id = layerTypeOf(engine);
    info.name = engineName(engine);
    info.group = "mu-On";
    info.paramPrefixLetter = 't';
    info.presetExtension = ".muTrack";
    info.contentFolderName = "muOn";   // the layer presets are mu-On's own, shared with it, not copied
    info.presetFolder = "Tracks";
    info.presetTag = kTrackPresetTag;
    info.descriptor = trackDescriptor(engine);
    info.discInitial = trackInitial(engine);
    info.typeFromPreset = &typeFromPreset;   // the six types share .muTrack: the engine a preset saved names its type
    info.addLayerParams = &addLayerParams;   // one table per prefix: every engine's rows exist on every track
    info.create = &createTrack;
    return info;
}

inline const mu_core::LayerTypeInfo kKickType   = makeTrackTypeInfo(EngineType::Kick);
inline const mu_core::LayerTypeInfo kBassType   = makeTrackTypeInfo(EngineType::Bass);
inline const mu_core::LayerTypeInfo kHatType    = makeTrackTypeInfo(EngineType::Hat);
inline const mu_core::LayerTypeInfo kSnareType  = makeTrackTypeInfo(EngineType::Snare);
inline const mu_core::LayerTypeInfo kRumbleType = makeTrackTypeInfo(EngineType::Rumble);
inline const mu_core::LayerTypeInfo kFmPercType = makeTrackTypeInfo(EngineType::FmPerc);

} // namespace mu_on
