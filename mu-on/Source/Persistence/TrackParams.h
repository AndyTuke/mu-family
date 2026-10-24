#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Sequencer/LayerTypeInfo.h"   // mu-core: mu_core::LayerTypeId
#include <functional>
#include <utility>
#include <vector>

// The Track layer type's parameter table (docs/mu-tronic/design.md 2.3): the mu-On parameter ids, spelled
// in one place, and the rows one track slot adds to the layout. A mu-On layer is a "track"; each track has
// a sound type, its engine (Kick, Bass, Hat, Snare, Rumble, FM Perc). Every engine's parameters exist on every track
// (switching the engine keeps the other engines' settings), so a track's ids are:
//   t{N}_eng             the engine (choice)
//   t{N}_src             the layer a Rumble track processes (a layer index)
//   t{N}_swing, _accent  the track's own groove (swing 0..1, accented-step velocity 0..1)
//   t{N}_<engine>_<x>    one engine's parameter, <engine> = kick / bass / hat / snare / rumble / fmp
// Saved data keys on these ids and their order, so they never change again and rows are only appended.
namespace mu_on
{

// The root tag of a track-preset file (.muTrack), shared by mu-On's own presets and the slot hosts that load them.
inline constexpr const char* kTrackPresetTag = "MuOnTrack";

// The values are the saved `t{N}_eng` choice index, so they are fixed.
enum class EngineType : int { Kick = 0, Bass, Hat, Snare, Rumble, FmPerc, Chord };
inline constexpr int kNumEngineTypes = 7;

inline constexpr EngineType engineFromIndex(int index) noexcept
{
    return static_cast<EngineType>(index < 0 ? 0 : (index >= kNumEngineTypes ? kNumEngineTypes - 1 : index));
}
inline constexpr int toIndex(EngineType e) noexcept { return static_cast<int>(e); }

// The layer type a track running each engine is (design 1.4: the five engines are five layer types over one
// class). The one mapping: it relies on LayerTypeId's Kick .. FmPerc standing in EngineType's order, which the
// asserts below keep true.
inline constexpr mu_core::LayerTypeId layerTypeOf(EngineType engine) noexcept
{
    return (mu_core::LayerTypeId) ((int) mu_core::LayerTypeId::Kick + toIndex(engine));
}
inline constexpr bool isTrackType(mu_core::LayerTypeId id) noexcept
{
    return (int) id >= (int) mu_core::LayerTypeId::Kick && (int) id <= (int) mu_core::LayerTypeId::Chord;
}
// The engine a Track type runs (Kick for a type that is not mu-On's).
inline constexpr EngineType engineOfType(mu_core::LayerTypeId id) noexcept
{
    return isTrackType(id) ? engineFromIndex((int) id - (int) mu_core::LayerTypeId::Kick) : EngineType::Kick;
}
static_assert(layerTypeOf(EngineType::Kick)   == mu_core::LayerTypeId::Kick,   "LayerTypeId must list the engines in EngineType order");
static_assert(layerTypeOf(EngineType::Bass)   == mu_core::LayerTypeId::Bass,   "LayerTypeId must list the engines in EngineType order");
static_assert(layerTypeOf(EngineType::Hat)    == mu_core::LayerTypeId::Hat,    "LayerTypeId must list the engines in EngineType order");
static_assert(layerTypeOf(EngineType::Snare)  == mu_core::LayerTypeId::Snare,  "LayerTypeId must list the engines in EngineType order");
static_assert(layerTypeOf(EngineType::Rumble) == mu_core::LayerTypeId::Rumble, "LayerTypeId must list the engines in EngineType order");
static_assert(layerTypeOf(EngineType::FmPerc) == mu_core::LayerTypeId::FmPerc, "LayerTypeId must list the engines in EngineType order");
static_assert(layerTypeOf(EngineType::Chord)  == mu_core::LayerTypeId::Chord,  "LayerTypeId must list the engines in EngineType order");
static_assert(kNumEngineTypes == (int) mu_core::LayerTypeId::Chord - (int) mu_core::LayerTypeId::Kick + 1, "one layer type per engine");

// The engine's display name (also the default name of a track running it).
inline constexpr const char* engineName(EngineType e) noexcept
{
    constexpr const char* kNames[kNumEngineTypes] = { "Kick", "Bass", "Hat", "Snare", "Rumble", "FM Perc", "Chord" };
    return kNames[toIndex(e)];
}

// The engine's lower-case id tag, the middle of an engine parameter id.
inline constexpr const char* engineTag(EngineType e) noexcept
{
    constexpr const char* kTags[kNumEngineTypes] = { "kick", "bass", "hat", "snare", "rumble", "fmp", "chord" };
    return kTags[toIndex(e)];
}

inline juce::StringArray engineNames()
{
    juce::StringArray names;
    for (int e = 0; e < kNumEngineTypes; ++e) names.add(engineName(engineFromIndex(e)));
    return names;
}

// The FM Perc Algorithm choice, in stored-index order (append-only): the voice topology a hit starts with.
inline juce::StringArray fmPercAlgorithmNames()
{
    return { "Tom", "Conga", "Zap", "Click / Rim", "Clank", "Wood / Mallet", "Pluck / Ping", "Burst / Glitch" };
}

// "t{N}_": the prefix of every parameter of track N (the layer slot).
inline juce::String layerParamPrefix(int track)
{
    return "t" + juce::String(track) + "_";
}

// A non-engine parameter of the track ("eng", "src").
inline juce::String layerParamId(int track, const juce::String& suffix)
{
    return layerParamPrefix(track) + suffix;
}

// The part of an engine parameter's id after the track prefix: "kick_tune".
inline juce::String engineParamSuffix(EngineType e, const juce::String& param)
{
    return juce::String(engineTag(e)) + "_" + param;
}

// The full id of an engine parameter: "t0_kick_tune".
inline juce::String engineParamId(int track, EngineType e, const juce::String& param)
{
    return layerParamPrefix(track) + engineParamSuffix(e, param);
}

// Every parameter of one engine (the id part after "<engine>_"), in display order.
// The master key and scale (global, the ids mu-Tant uses): the Kick and Bass tune snaps to them.
inline constexpr const char* kRootParamId  = "root";
inline constexpr const char* kScaleParamId = "scale";

// A Bass track's Tune left at its default (kBassTuneRootHz, E1) plays the master key's root note instead, in the octave
// from MIDI 28 (E1) to 39, so a new Bass sequence starts on the key's root whatever the key. Turn the Tune away from it
// and the track plays that tuning snapped to the scale, as before.
inline constexpr float kBassTuneRootHz = 41.2f;
inline bool isBassTuneRoot(float tuneHz) noexcept { return tuneHz > kBassTuneRootHz - 0.05f && tuneHz < kBassTuneRootHz + 0.05f; }
inline int  bassRootMidi(int root) noexcept        { return 28 + ((root % 12) - 4 + 12) % 12; }

// Loop ratio choices: "play the `play`th of every `of` passes of the row" (1:1 plays every pass).
struct LoopRatio { int play, of; };
inline constexpr LoopRatio kLoopRatios[] = { {1,1}, {1,2}, {2,2}, {1,3}, {2,3}, {3,3}, {1,4}, {2,4}, {3,4}, {4,4} };
juce::StringArray loopRatioNames();

inline const std::vector<const char*>& engineParamNames(EngineType e)
{
    static const std::vector<const char*> kKick   { "tune", "ptch", "pdec", "adec", "drive", "fine" };
    static const std::vector<const char*> kBass   { "wave", "sub", "tune", "cut", "res", "env", "edec", "atk", "dec", "sus", "drive", "fine", "rel", "glide" };
    static const std::vector<const char*> kHat    { "tune", "dec", "otune", "odec" };
    static const std::vector<const char*> kSnare  { "tune", "dec", "snap" };
    static const std::vector<const char*> kRumble { "drive", "d1", "d2", "d3", "size", "revmix", "revlp", "cut", "res", "inlp" };
    static const std::vector<const char*> kChord  { "wave", "cut", "res", "drive", "env", "edec", "atk", "dec", "sus", "rel" };
    static const std::vector<const char*> kFmPerc { "algo", "tune", "ratio", "index", "idec", "fdbk", "dive", "dtime", "decay", "fold", "noise" };
    switch (e)
    {
        case EngineType::Kick:   return kKick;
        case EngineType::Bass:   return kBass;
        case EngineType::Hat:    return kHat;
        case EngineType::Snare:  return kSnare;
        case EngineType::Rumble: return kRumble;
        case EngineType::FmPerc: return kFmPerc;
        case EngineType::Chord:  return kChord;
    }
    return kKick;
}

// The prefixes before the layers became variable: five lanes named by their instrument (Kick, Bass,
// Hat, Snare, Rumble). Read-only, so older saved data still loads; never used to name a live parameter.
inline constexpr int kNumLegacyLanes = 5;
inline constexpr const char* legacyLaneParamPrefix(int lane) noexcept
{
    constexpr const char* kLegacy[kNumLegacyLanes] = { "k_", "b_", "h_", "s_", "r_" };
    return kLegacy[lane < 0 ? 0 : (lane >= kNumLegacyLanes ? kNumLegacyLanes - 1 : lane)];
}

// The lane prefixes in lane order (for LayerLayout::addLegacyPrefixes).
inline juce::StringArray legacyLaneParamPrefixes()
{
    juce::StringArray prefixes;
    for (int l = 0; l < kNumLegacyLanes; ++l) prefixes.add(legacyLaneParamPrefix(l));
    return prefixes;
}

// {old, new} prefix pairs in lane order (for MidiControlMap::setIdPrefixRenames): "k_tune" -> "t0_kick_tune".
inline std::vector<std::pair<juce::String, juce::String>> legacyLaneParamPrefixRenames()
{
    std::vector<std::pair<juce::String, juce::String>> renames;
    for (int l = 0; l < kNumLegacyLanes; ++l)
        renames.emplace_back(legacyLaneParamPrefix(l), layerParamPrefix(l) + engineTag(engineFromIndex(l)) + "_");
    return renames;
}

// The Track type's rows for one layer slot (t{slot}_*): the engine, the Rumble source, every engine's
// parameters, the groove (swing, accent), then the FM Perc rows (appended last to keep earlier rows in place).
void addLayerParams(juce::AudioProcessorValueTreeState::ParameterLayout& layout, int slot);

// A Rumble track's source (`t{N}_src`) names a layer, so it follows that layer through a remove or a reorder:
// every slot's source is rewritten through `oldToNew` (a layer index as it was to the index it has now, -1 when
// that layer is gone), and a gone source becomes the first Kick of the new order. `liveCount` is the live layer
// count, `engineOfSlot` the engine each slot runs, `fallbackCandidate` (optional, by old slot) which slots may take over from
// a gone source - the first Kick when not given. Message thread, before the parameter values shift (the base's
// remapLayerReferences hook), so each value is rewritten at its old slot and moves with it.
void remapRumbleSources(juce::AudioProcessorValueTreeState& apvts, int liveCount,
                        const std::function<EngineType(int)>& engineOfSlot,
                        const std::function<int(int)>& oldToNew,
                        const std::function<bool(int)>& fallbackCandidate = {});

} // namespace mu_on
