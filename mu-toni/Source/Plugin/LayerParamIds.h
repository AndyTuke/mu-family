#pragma once
#include <juce_core/juce_core.h>

namespace mu_toni
{

// The one place mu-Toni spells a layer's APVTS id: "v{layer}_{suffix}". Saved data keys on these ids,
// so the letter and the underscore never change.
inline constexpr const char* kLayerParamLetter = "v";

inline juce::String layerParamPrefix(int layer)
{
    return juce::String(kLayerParamLetter) + juce::String(layer) + "_";
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
