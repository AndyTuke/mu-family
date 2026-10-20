#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <cstring>
#include <functional>
#include <memory>

struct Layer;
class ProcessorBase;
class LayerContent;   // mu-core/UI/LayerContent.h: a type's content component for the shown slot

// Layer types: family vocabulary (as kMaxLayers is), so the ids live in mu-core. docs/mu-tronic/design.md
// 2.5. A type is described and constructed by its LayerTypeInfo; it is never dispatched through it
// (rendering stays virtual on Layer). A host of several types lists them in a LayerTypeRegistry; a product
// that hosts one type uses create / addLayerParams itself.
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

    // What a host needs to know about a type, all message-thread data. The UI factories are null for a host that
    // builds its own UI (the four list products); mu-Tronic's editor builds the shown slot's UI through them.
    struct LayerTypeInfo
    {
        using AddParamsFn = void (*)(juce::AudioProcessorValueTreeState::ParameterLayout& layout, int slot);
        // `type` lets one function make several types of one class (the five mu-On Track types).
        using CreateFn    = std::unique_ptr<Layer> (*)(int slot, LayerTypeId type);
        // The type a layer-preset tree holds; Empty when the tree names none.
        using TypeFromPresetFn = LayerTypeId (*)(const juce::ValueTree& preset);
        using CreateContentFn = std::unique_ptr<LayerContent> (*)(ProcessorBase& host, int slot);
        // The slot's picture in the sidebar; `pulse` asks the host to repaint the item.
        using CreateMiniVisualFn = std::unique_ptr<juce::Component> (*)(ProcessorBase& host, int slot, std::function<void()> pulse);

        LayerTypeId id = LayerTypeId::Empty;
        const char* name = "Empty";            // display name ("Arp", "Kick")
        const char* group = "";                // the product it belongs to, for the chooser ("mu-Toni")
        char        paramPrefixLetter = 0;     // r / v / a / t: the slot's parameter prefix is <letter><slot>_
        const char* presetExtension = "";      // the layer-preset extension (".muArp")
        const char* contentFolderName = "";    // the product's content folder under the shared TDP folder ("muToni")
        const char* presetFolder = "";         // the layer-preset subfolder inside it ("Arps"): the type's presets
                                               // are the product's own, shared with it and never copied
        AddParamsFn addLayerParams = nullptr;  // the type's parameter table for one slot (once per prefix)
        CreateFn    create = nullptr;          // a fresh default layer for `slot`, not yet prepared or bound
        TypeFromPresetFn typeFromPreset = nullptr;   // set where several types share an extension (.muTrack): the folder
                                               // listing then keeps only the files that map to this type; null = every file
        CreateContentFn    createContent    = nullptr;   // the type's content component, built once per type by the editor
        CreateMiniVisualFn createMiniVisual = nullptr;   // the type's sidebar picture
        int         appColourId = 0;           // the MuLookAndFeel colour ID (appBlue ...) the slot's main area is painted in; 0 = the app's own
        const char* typeTag = "";              // the header's type plate text ("Toni", "On"); empty = none
    };
}
