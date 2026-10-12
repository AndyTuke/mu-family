#pragma once

#include <juce_core/juce_core.h>
#include <utility>
#include <vector>

// The mu-On parameter ids, spelled in one place. A mu-On layer is a "track"; each track has a sound
// type, its engine (Kick, Bass, Hat, Snare, Rumble). Every engine's parameters exist on every track
// (switching the engine keeps the other engines' settings), so a track's ids are:
//   t{N}_eng             the engine (choice)
//   t{N}_src             the layer a Rumble track processes (a layer index)
//   t{N}_<engine>_<x>    one engine's parameter, <engine> = kick / bass / hat / snare / rumble
// Saved data keys on these ids, so they never change again.
namespace mu_on
{

// The values are the saved `t{N}_eng` choice index, so they are fixed.
enum class EngineType : int { Kick = 0, Bass, Hat, Snare, Rumble };
inline constexpr int kNumEngineTypes = 5;

inline constexpr EngineType engineFromIndex(int index) noexcept
{
    return static_cast<EngineType>(index < 0 ? 0 : (index >= kNumEngineTypes ? kNumEngineTypes - 1 : index));
}
inline constexpr int toIndex(EngineType e) noexcept { return static_cast<int>(e); }

// The engine's display name (also the default name of a track running it).
inline constexpr const char* engineName(EngineType e) noexcept
{
    constexpr const char* kNames[kNumEngineTypes] = { "Kick", "Bass", "Hat", "Snare", "Rumble" };
    return kNames[toIndex(e)];
}

// The engine's lower-case id tag, the middle of an engine parameter id.
inline constexpr const char* engineTag(EngineType e) noexcept
{
    constexpr const char* kTags[kNumEngineTypes] = { "kick", "bass", "hat", "snare", "rumble" };
    return kTags[toIndex(e)];
}

inline juce::StringArray engineNames()
{
    juce::StringArray names;
    for (int e = 0; e < kNumEngineTypes; ++e) names.add(engineName(engineFromIndex(e)));
    return names;
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
inline const std::vector<const char*>& engineParamNames(EngineType e)
{
    static const std::vector<const char*> kKick   { "tune", "ptch", "pdec", "adec", "drive" };
    static const std::vector<const char*> kBass   { "wave", "sub", "tune", "cut", "res", "env", "edec", "atk", "dec", "sus", "drive" };
    static const std::vector<const char*> kHat    { "tune", "dec" };
    static const std::vector<const char*> kSnare  { "tune", "dec" };
    static const std::vector<const char*> kRumble { "drive", "d1", "d2", "d3", "size", "revmix", "revlp", "cut", "res" };
    switch (e)
    {
        case EngineType::Kick:   return kKick;
        case EngineType::Bass:   return kBass;
        case EngineType::Hat:    return kHat;
        case EngineType::Snare:  return kSnare;
        case EngineType::Rumble: return kRumble;
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

} // namespace mu_on
