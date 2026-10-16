#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <cstring>
#include <memory>

struct Layer;

// Layer types: family vocabulary (as kMaxLayers is), so the ids live in mu-core. docs/mu-tronic/design.md
// 2.5. A type is described and constructed by its LayerTypeInfo; it is never dispatched through it
// (rendering stays virtual on Layer). No registry list yet: each product declares its one LayerTypeInfo
// constant when it adopts the contract and uses create / addLayerParams itself.
namespace mu_core
{
    // Saved by NAME (layerTypeName), never by number, so the order here may change.
    enum class LayerTypeId : int { Empty = 0, Rhythm, Pattern, Arp, Kick, Bass, Hat, Snare, Rumble };

    inline const char* layerTypeName(LayerTypeId id) noexcept
    {
        switch (id)
        {
            case LayerTypeId::Empty:   return "Empty";
            case LayerTypeId::Rhythm:  return "Rhythm";
            case LayerTypeId::Pattern: return "Pattern";
            case LayerTypeId::Arp:     return "Arp";
            case LayerTypeId::Kick:    return "Kick";
            case LayerTypeId::Bass:    return "Bass";
            case LayerTypeId::Hat:     return "Hat";
            case LayerTypeId::Snare:   return "Snare";
            case LayerTypeId::Rumble:  return "Rumble";
        }
        return "Empty";
    }

    // The id a saved name stands for; an unknown or empty name is Empty ("missing means default").
    inline LayerTypeId layerTypeFromName(const char* name) noexcept
    {
        if (name == nullptr) return LayerTypeId::Empty;
        for (int i = 0; i <= (int) LayerTypeId::Rumble; ++i)
            if (std::strcmp(name, layerTypeName((LayerTypeId) i)) == 0) return (LayerTypeId) i;
        return LayerTypeId::Empty;
    }

    // What a host needs to know about a type, all message-thread data. Describe-only: the fields the four
    // products use as they adopt the contract (the chooser, services and UI factories come with mu-Tronic).
    struct LayerTypeInfo
    {
        using AddParamsFn = void (*)(juce::AudioProcessorValueTreeState::ParameterLayout& layout, int slot);
        using CreateFn    = std::unique_ptr<Layer> (*)(int slot);

        LayerTypeId id = LayerTypeId::Empty;
        const char* name = "Empty";            // display name ("Arp", "Kick")
        const char* group = "";                // the product it belongs to, for the chooser ("mu-Toni")
        char        paramPrefixLetter = 0;     // r / v / a / t: the slot's parameter prefix is <letter><slot>_
        const char* presetExtension = "";      // the layer-preset extension (".muArp")
        AddParamsFn addLayerParams = nullptr;  // the type's parameter table for one slot (once per prefix)
        CreateFn    create = nullptr;          // a fresh default layer for `slot`, not yet prepared or bound
    };
}
