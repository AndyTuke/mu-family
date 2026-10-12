#pragma once
#include <juce_core/juce_core.h>
#include <utility>
#include <vector>

namespace mu_toni
{

// The one place mu-Toni spells a layer's APVTS id: "a{layer}_{suffix}" (a = Arp). Saved data keys on
// these ids, so the letter and the underscore never change again.
inline constexpr const char* kLayerParamLetter = "a";

// The letter the ids carried before the rename (the pre-release builds): read-only, only so older saved
// data still loads.
inline constexpr const char* kLegacyLayerParamLetter = "v";

inline juce::String layerParamPrefix(int layer)
{
    return juce::String(kLayerParamLetter) + juce::String(layer) + "_";
}

inline juce::String legacyLayerParamPrefix(int layer)
{
    return juce::String(kLegacyLayerParamLetter) + juce::String(layer) + "_";
}

// The pre-rename prefixes of layers 0..count-1, in slot order (for LayerLayout::addLegacyPrefixes).
inline juce::StringArray legacyLayerParamPrefixes(int count)
{
    juce::StringArray prefixes;
    for (int l = 0; l < count; ++l) prefixes.add(legacyLayerParamPrefix(l));
    return prefixes;
}

// {old, new} prefix pairs for layers 0..count-1, for MidiControlMap::setIdPrefixRenames.
inline std::vector<std::pair<juce::String, juce::String>> layerParamPrefixRenames(int count)
{
    std::vector<std::pair<juce::String, juce::String>> renames;
    for (int l = 0; l < count; ++l) renames.emplace_back(legacyLayerParamPrefix(l), layerParamPrefix(l));
    return renames;
}

// Same shape under another letter group (the mixer's "ch" sends share the layer index).
inline juce::String layerParamId(const juce::String& letters, int layer, const juce::String& suffix)
{
    return letters + juce::String(layer) + "_" + suffix;
}

inline juce::String layerParamId(int layer, const juce::String& suffix)
{
    return layerParamPrefix(layer) + suffix;
}

} // namespace mu_toni
