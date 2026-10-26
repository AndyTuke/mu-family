#pragma once

#include "Sequencer/LayerTypeRegistry.h"

#include <juce_core/juce_core.h>
#include <juce_data_structures/juce_data_structures.h>

#include <vector>

// Import of a product's full preset (.muClid, .muTant, .muToni, .muOn) as layers for mu-Tronic's Empty slots
// (docs/mu-tronic/design.md 3.4, "Import (Stage T8b)"). Pure message-thread reading: nothing here touches a processor.
// The result is an ordered list of layers, each the type, the composed layer node a slot applies through
// ProcessorBase::setLayerType (the product's own saved values, never the type's `_default`), and the layer's mixer
// strip. Where the layers go and what happens to the strips is the processor's (PluginProcessor::importProductPreset).
//
// Read: a format-2 composed state (the shape every product saves today) and, for mu-Tant and mu-Toni, the APVTS-dump
// state before it. A mu-On state from before its layers were variable (five lanes named by instrument) and a state with
// no layers are refused. Ignored by design: the imported preset's global FX and master, clip banks, layer colours.
namespace mu_tronic
{

enum class ProductKind { Clid, Tant, Toni, On };

// The product an extension belongs to (".muClid", case-insensitive, with or without the dot); false for any other.
bool productKindFromExtension(const juce::String& extension, ProductKind& kind);

// A mixer-strip row of an imported layer. `suffix` is the strip's row name ("lvl", "scSrc", "midiIn"); `value` is the
// actual value, or the normalised one (mu-Clid writes its strip normalised) when `normalised` is set.
struct ImportedStripValue
{
    juce::String suffix;
    double       value = 0.0;
    bool         normalised = false;
};

struct ImportedLayer
{
    mu_core::LayerTypeId type = mu_core::LayerTypeId::Empty;
    juce::ValueTree      node;                   // the composed layer node, rooted at the type's presetTag
    juce::String         name;                   // the layer's own name in the product; empty = the default
    int                  sourceIndex = 0;        // the layer's index in the product preset (what sidechains and a Rumble name)
    int                  rumbleSource = -1;      // a Rumble's source layer, as a product index; -1 for any other layer
    std::vector<ImportedStripValue> strip;       // lvl, pan, mute, solo, sends, sidechain ... and midiIn where the product has one
};

// A global the preset carries that a Pattern layer reads (the tonal centre): the row id and its actual value.
struct ImportedGlobal
{
    juce::String id;
    double       value = 0.0;
};

struct ProductImport
{
    ProductKind                 kind = ProductKind::Clid;
    juce::String                presetName;
    juce::String                error;           // set (and `entries` empty) when the file could not be read
    std::vector<ImportedLayer>  entries;          // in the product's layer order
    std::vector<ImportedGlobal> globals;         // see ImportedGlobal; only mu-Tant has any

    bool ok() const noexcept { return error.isEmpty() && ! entries.empty(); }
};

// How many layers the products hold at most (their MU_MAX_LAYERS); a product's sidechain parameter is
// 0 off, 1..kProductLayers channels, kProductLayers + 1 the external input.
inline constexpr int kProductLayers = 8;

// Reads `file` (its extension names the product). `registry` supplies each type's preset tag. Never throws; a file
// that cannot be read returns `error` set.
ProductImport readProductPreset(const juce::File& file, const mu_core::LayerTypeRegistry& registry);

// The same over an already parsed preset root (a wrapped <MuXPreset> or its bare state), for tests and for callers
// that have the tree.
ProductImport readProductPresetTree(const juce::ValueTree& root, ProductKind kind, const juce::String& presetName,
                                    const mu_core::LayerTypeRegistry& registry);

// The mu-Tronic `scSrc` parameter value for an imported strip whose product value was `productParam`: 0 off, a product
// channel becomes the slot its layer was laid into (`slotOfSource[productIndex]`, -1 when that layer was not imported),
// the product's external input becomes this binary's external input. A source that is not legal for the strip (its own
// slot, an unimported layer) becomes off. The mu_mix renumbering rules decide legality.
int remapImportedSidechain(int productParam, int ownSlot, const std::vector<int>& slotOfSource);

// Rewrites a Rumble node's source row to `slot` (a slot number in this host).
void setRumbleSourceRow(juce::ValueTree& node, int slot);

} // namespace mu_tronic
