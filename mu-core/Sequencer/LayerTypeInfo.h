#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "Sequencer/LayerServices.h"

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
    // How many ids there are: the size of a table indexed by LayerTypeId.
    inline constexpr int kLayerTypeIdCount = 11;

    // Saved by NAME (layerTypeName), never by number, so the order here may change.
    // Test and Test2 are reserved for test layers: no product registers them, so a fake type can never collide with a
    // real one the day that product's type is registered.
    enum class LayerTypeId : int { Empty = 0, Rhythm, Pattern, Arp, Kick, Bass, Hat, Snare, Rumble, Test, Test2 };

    static_assert(kLayerTypeIdCount == (int) LayerTypeId::Test2 + 1, "kLayerTypeIdCount follows the last LayerTypeId");

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
            case LayerTypeId::Test:    return "Test";
            case LayerTypeId::Test2:   return "Test2";
        }
        return "Empty";
    }

    // The id a saved name stands for; an unknown or empty name is Empty ("missing means default").
    inline LayerTypeId layerTypeFromName(const char* name) noexcept
    {
        if (name == nullptr) return LayerTypeId::Empty;
        for (int i = 0; i <= (int) LayerTypeId::Test2; ++i)
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
        // A layer-preset FILE tree to the composed node a slot applies (rows + extras), and back, for a type whose file
        // keeps another shape than the composed node (mu-Clid's .muRhythm keeps its r0_ properties). `name` is the preset's
        // name when saving. Pure, message thread. The node keeps the file's root tag, so the type test still reads it.
        using PresetConvertFn = juce::ValueTree (*)(const juce::ValueTree& tree, const juce::String& name);
        using CreateContentFn = std::unique_ptr<LayerContent> (*)(ProcessorBase& host, int slot);
        // The slot's picture in the sidebar; `pulse` asks the host to repaint the item.
        using CreateMiniVisualFn = std::unique_ptr<juce::Component> (*)(ProcessorBase& host, int slot, std::function<void()> pulse);
        // What the type's layers share (design 2.4), built once per processor when the first layer of the type is added.
        using CreateServicesFn = std::unique_ptr<LayerServices> (*)(ProcessorBase& host);
        // Wires a layer `create` just made to the type's services (its host link, its slot's engine). Called once on the
        // message thread, after create and before the layer is prepared, bound or published.
        using AttachServicesFn = void (*)(Layer& layer, LayerServices& services, int slot);
        // The type's parameter rows that belong to no slot (a tonal centre every layer of the type reads), added once.
        using AddGlobalParamsFn = void (*)(juce::AudioProcessorValueTreeState::ParameterLayout& layout);

        LayerTypeId id = LayerTypeId::Empty;
        const char* name = "Empty";            // display name ("Arp", "Kick")
        const char* group = "";                // the product it belongs to, for the chooser ("mu-Toni")
        char        paramPrefixLetter = 0;     // r / v / a / t: the slot's parameter prefix is <letter><slot>_
        const char* presetExtension = "";      // the layer-preset extension (".muArp")
        const char* contentFolderName = "";    // the product's content folder under the shared TDP folder ("muToni")
        const char* presetFolder = "";         // the layer-preset subfolder inside it ("Arps"): the type's presets
                                               // are the product's own, shared with it and never copied
        AddParamsFn addLayerParams = nullptr;  // the type's parameter table for one slot (once per prefix)
        AddGlobalParamsFn addGlobalParams = nullptr;   // rows shared by every slot of the type, once per type (null = none)
        CreateFn    create = nullptr;          // a fresh default layer for `slot`, not yet prepared or bound
        TypeFromPresetFn typeFromPreset = nullptr;   // set where several types share an extension (.muTrack): the folder
                                               // listing then keeps only the files that map to this type; null = every file
        PresetConvertFn presetFileToNode = nullptr;   // load: file tree -> composed node; null = the file is the node
        PresetConvertFn presetNodeToFile = nullptr;   // save: composed node -> file tree; null = the node is the file
        CreateContentFn    createContent    = nullptr;   // the type's content component, built once per type by the editor
        CreateMiniVisualFn createMiniVisual = nullptr;   // the type's sidebar picture
        CreateServicesFn   createServices   = nullptr;   // null = the type shares nothing between its layers
        AttachServicesFn   attachServices   = nullptr;   // null = nothing to wire (a type that needs no services)
        int         appColourId = 0;           // the MuLookAndFeel colour ID (appBlue ...) the slot's main area is painted in; 0 = the app's own
        const char* typeTag = "";              // the header's type plate text, UTF-8, the full product name ("Î¼-Toni"); empty = none
        const char* presetTag = "";            // the root tag of a layer-preset file ("MuToniLayer"): each product's existing tag,
                                               // so a file saved by the product loads in a slot host and the reverse
        const char* descriptor = "";           // one short line for the chooser card ("Generative arpeggiator")
        const char* discInitial = "";          // one character on the type's disc (sidebar and chooser card); unique across a host's choices
        bool        takesNotes = false;        // the type plays from MIDI notes, so a slot of it shows the Notes In channel control
        bool        hasClipBank = true;        // a slot of the type stores clips; false (a type that processes another layer's audio) hides the strip
    };
}
