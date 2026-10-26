#include "Persistence/ProductImport.h"
#include "Persistence/LayerState.h"          // mu-core: the composed state's tags, findLayerNode
#include "Persistence/PresetFiles.h"         // mu-core: readFullPreset
#include "Modulation/ModulatorSerialise.h"   // mu-core: the pre-format-2 voice-data tags
#include "Audio/MixerEngine.h"               // mu-core: the external sidechain source
#include "Audio/SidechainRemap.h"            // mu-core: which strip may take which source
#include "MuLimits.h"
#include "Sequencer/RhythmType.h"            // mu-Clid's layer library: a <Rhythm> child to a composed node
#include "Sequencer/TrackTypes.h"            // mu-On's layer library: the engine a track node names

#include <cmath>

namespace mu_tronic
{

namespace
{
    // The strip rows every product's layer carries, in the order they are listed.
    constexpr const char* kStripSuffixes[] = { "lvl", "pan", "mute", "solo", "sendEff", "sendDly", "sendRev",
                                               "scSrc", "scAmt", "scAtk", "scRel", "outBus" };

    struct KindInfo
    {
        const char*          presetTag;
        const char*          stateTag;
        const char*          letters;     // the slot prefix letters an APVTS-dump state used for the product's layers
        mu_core::LayerTypeId type;        // Empty for mu-On (the track's engine decides)
    };

    KindInfo kindInfo(ProductKind kind)
    {
        switch (kind)
        {
            case ProductKind::Clid: return { "MuClidPreset", "MuClidState", "",   mu_core::LayerTypeId::Rhythm };
            case ProductKind::Tant: return { "MuTantPreset", "MuTantState", "v",  mu_core::LayerTypeId::Pattern };
            case ProductKind::Toni: return { "MuToniPreset", "MuToniState", "va", mu_core::LayerTypeId::Arp };
            case ProductKind::On:   return { "MuOnPreset",   "MuOnState",   "t",  mu_core::LayerTypeId::Empty };
        }
        return {};
    }

    ProductImport failed(ProductKind kind, const juce::String& name, const juce::String& why)
    {
        ProductImport out;
        out.kind = kind;
        out.presetName = name;
        out.error = why;
        return out;
    }

    // `id` as "<letter><digits>_<rest>" for one of `letters`: the slot and the rest; false for any other id.
    bool splitSlotId(const juce::String& id, const juce::String& letters, int& slot, juce::String& rest)
    {
        if (id.isEmpty() || ! letters.containsChar(id[0])) return false;
        int i = 1;
        while (i < id.length() && juce::CharacterFunctions::isDigit(id[i])) ++i;
        if (i == 1 || i >= id.length() || id[i] != '_') return false;
        slot = id.substring(1, i).getIntValue();
        rest = id.substring(i + 1);
        return true;
    }

    // An APVTS-dump state (<PARAM id value> plus a <VoiceData> of <Voice idx>) in the composed shape: each PARAM becomes
    // a row in its layer (prefix stripped) or the globals, each voice's properties and children move into its layer.
    // The same rebuild mu-core's composeLegacyState makes, without a processor's parameter layout.
    juce::ValueTree composeDump(const juce::ValueTree& dump, const juce::String& letters)
    {
        juce::ValueTree state(dump.getType());
        state.copyPropertiesFrom(dump, nullptr);
        state.setProperty("format", mu_pp::kComposedStateFormat, nullptr);

        juce::ValueTree globals(mu_pp::kGlobalsTag), slots(mu_pp::kLayersTag);
        std::vector<juce::ValueTree> nodes;
        for (int s = 0; s < kProductLayers; ++s)
        {
            juce::ValueTree node(mu_pp::kLayerTag);
            node.setProperty("idx", s, nullptr);
            nodes.push_back(node);
            slots.appendChild(node, nullptr);
        }
        state.appendChild(globals, nullptr);
        state.appendChild(slots, nullptr);

        // Sort the old root children: params into rows, per-voice data into layers.
        for (int i = 0; i < dump.getNumChildren(); ++i)
        {
            const auto child = dump.getChild(i);
            if (child.hasType("PARAM"))
            {
                if (! child.hasProperty("value")) continue;   // a parameter never set: the default
                const auto id = child.getProperty("id").toString();
                int slot = 0;
                juce::String rest;
                const bool inSlot = splitSlotId(id, letters, slot, rest) && slot < kProductLayers;
                juce::ValueTree row(mu_pp::kRowTag);
                row.setProperty("id", inSlot ? rest : id, nullptr);
                row.setProperty("x", child.getProperty("value"), nullptr);
                (inSlot ? nodes[(size_t) slot] : globals).appendChild(row, nullptr);
            }
            else if (child.hasType(mu_pp::kChannelDataTag))
            {
                for (int v = 0; v < child.getNumChildren(); ++v)
                {
                    const auto voice = child.getChild(v);
                    const int s = (int) voice.getProperty("idx", -1);
                    if (! voice.hasType(mu_pp::kChannelNodeTag) || s < 0 || s >= kProductLayers) continue;
                    auto& node = nodes[(size_t) s];
                    for (int p = 0; p < voice.getNumProperties(); ++p)
                        if (const auto name = voice.getPropertyName(p); name != juce::Identifier("idx"))
                            node.setProperty(name, voice.getProperty(name), nullptr);
                    for (int c = 0; c < voice.getNumChildren(); ++c)
                        node.appendChild(voice.getChild(c).createCopy(), nullptr);
                }
            }
        }
        return state;
    }

    // The actual value of the row `id` among `parent`'s rows; false when it has none.
    bool rowValue(const juce::ValueTree& parent, const juce::String& id, double& value)
    {
        for (int i = 0; i < parent.getNumChildren(); ++i)
            if (const auto row = parent.getChild(i);
                row.hasType(mu_pp::kRowTag) && row.getProperty("id").toString() == id && row.hasProperty("x"))
            {
                value = (double) row.getProperty("x");
                return std::isfinite(value);
            }
        return false;
    }

    // A composed state (mu-Tant, mu-Toni, mu-On) as imported layers.
    ProductImport readComposed(const juce::ValueTree& stateIn, ProductKind kind, const juce::String& name,
                               const mu_core::LayerTypeRegistry& registry)
    {
        const auto info = kindInfo(kind);
        const bool composed = mu_pp::isComposedState(stateIn);
        // mu-On kept five lanes named by instrument before its layers were variable: not a layer list.
        if (kind == ProductKind::On && ! stateIn.hasProperty("numVoices"))
            return failed(kind, name, "This preset is from an older mu-On. Open it in mu-On and save it again first");

        const auto state = composed ? stateIn : composeDump(stateIn, info.letters);
        const int count = juce::jlimit(1, kProductLayers, (int) state.getProperty("numVoices", 1));
        const auto globals = state.getChildWithName(mu_pp::kGlobalsTag);

        ProductImport out;
        out.kind = kind;
        out.presetName = name;
        for (int i = 0; i < count; ++i)
        {
            const auto slotNode = mu_pp::findLayerNode(state, i);
            ImportedLayer layer;
            layer.sourceIndex = i;

            // A track names its type by its engine row; the other products have one layer type.
            auto type = info.type;
            if (kind == ProductKind::On)
                type = slotNode.isValid() ? mu_on::typeFromPreset(slotNode) : mu_core::LayerTypeId::Empty;
            const auto* typeInfo = registry.find(type);
            if (typeInfo == nullptr)
                return failed(kind, name, "Layer " + juce::String(i + 1) + " is a type this build does not offer");
            layer.type = type;

            layer.node = juce::ValueTree(typeInfo->presetTag);
            if (slotNode.isValid()) layer.node.copyPropertiesAndChildrenFrom(slotNode, nullptr);
            layer.node.removeProperty("idx", nullptr);
            layer.name = slotNode.getProperty("name").toString();

            if (type == mu_core::LayerTypeId::Rumble)
            {
                double src = 0.0;
                layer.rumbleSource = rowValue(layer.node, "src", src) ? juce::roundToInt(src) : 0;
            }

            for (const char* suffix : kStripSuffixes)
                if (double v = 0.0; rowValue(globals, "ch" + juce::String(i) + "_" + suffix, v))
                    layer.strip.push_back({ suffix, v, false });
            out.entries.push_back(std::move(layer));
        }

        // mu-Toni's single Notes In channel (a global) is the layer's own in a slot host.
        if (kind == ProductKind::Toni && ! out.entries.empty())
            if (double ch = 0.0; rowValue(globals, "midiInCh", ch))
                out.entries.front().strip.push_back({ "midiIn", ch, false });

        // mu-Tant's tonal centre, which every Pattern layer reads.
        if (kind == ProductKind::Tant)
            for (const char* id : { "root", "scale" })
                if (double v = 0.0; rowValue(globals, id, v))
                    out.globals.push_back({ id, v });
        return out;
    }

    // A mu-Clid preset: one Rhythm layer per <Rhythm> child. The strip is written normalised.
    ProductImport readClid(const juce::ValueTree& root, const juce::String& name, const mu_core::LayerTypeRegistry& registry)
    {
        const auto* typeInfo = registry.find(mu_core::LayerTypeId::Rhythm);
        if (typeInfo == nullptr) return failed(ProductKind::Clid, name, "This build does not offer the Rhythm layer");

        ProductImport out;
        out.kind = ProductKind::Clid;
        out.presetName = name;
        for (int i = 0; i < root.getNumChildren() && (int) out.entries.size() < kProductLayers; ++i)
        {
            const auto child = root.getChild(i);
            if (! child.hasType("Rhythm")) continue;

            ImportedLayer layer;
            layer.type = mu_core::LayerTypeId::Rhythm;
            layer.sourceIndex = (int) out.entries.size();
            layer.node = mu_clid::rhythmChildToNode(child, typeInfo->presetTag, name);
            const auto own = child.getProperty("name").toString();
            if (own != "<unnamed>") layer.name = own;

            for (const char* suffix : kStripSuffixes)
            {
                const juce::Identifier prop("ch_" + juce::String(suffix));
                if (! child.hasProperty(prop)) continue;
                const double norm = (double) child.getProperty(prop);
                // The sidechain source is an integer over the product's list; every other row stays normalised and is
                // decoded against the matching mu-Tronic row's range.
                if (juce::String(suffix) == "scSrc")
                    layer.strip.push_back({ suffix, std::round(norm * (kProductLayers + 1)), false });
                else
                    layer.strip.push_back({ suffix, norm, true });
            }
            out.entries.push_back(std::move(layer));
        }
        if (out.entries.empty()) return failed(ProductKind::Clid, name, "The preset holds no rhythms");
        return out;
    }
}

bool productKindFromExtension(const juce::String& extension, ProductKind& kind)
{
    const auto e = extension.trimCharactersAtStart(".");
    if (e.equalsIgnoreCase("muClid")) { kind = ProductKind::Clid; return true; }
    if (e.equalsIgnoreCase("muTant")) { kind = ProductKind::Tant; return true; }
    if (e.equalsIgnoreCase("muToni")) { kind = ProductKind::Toni; return true; }
    if (e.equalsIgnoreCase("muOn"))   { kind = ProductKind::On;   return true; }
    return false;
}

ProductImport readProductPresetTree(const juce::ValueTree& root, ProductKind kind, const juce::String& presetName,
                                    const mu_core::LayerTypeRegistry& registry)
{
    const auto info = kindInfo(kind);
    if (kind == ProductKind::Clid)
    {
        if (! root.hasType(info.presetTag)) return failed(kind, presetName, "Could not read " + presetName);
        return readClid(root, presetName, registry);
    }

    // A wrapped preset holds the state as its child; a bare state is the root.
    juce::ValueTree state;
    if (root.hasType(info.presetTag))       state = root.getChildWithName(info.stateTag);
    else if (root.hasType(info.stateTag))   state = root;
    if (! state.isValid()) return failed(kind, presetName, "Could not read " + presetName);
    return readComposed(state, kind, presetName, registry);
}

ProductImport readProductPreset(const juce::File& file, const mu_core::LayerTypeRegistry& registry)
{
    const auto name = file.getFileNameWithoutExtension();
    ProductKind kind = ProductKind::Clid;
    if (! productKindFromExtension(file.getFileExtension(), kind))
        return failed(kind, name, "Not a product preset: " + file.getFileName());

    const auto xml = juce::XmlDocument::parse(file);
    if (xml == nullptr) return failed(kind, name, "Could not read " + file.getFileName());
    const auto root = juce::ValueTree::fromXml(*xml);
    if (! root.isValid()) return failed(kind, name, "Could not read " + file.getFileName());

    auto out = readProductPresetTree(root, kind, name, registry);
    if (out.error.isNotEmpty() && out.error.startsWith("Could not read")) out.error = "Could not read " + file.getFileName();
    return out;
}

int remapImportedSidechain(int productParam, int ownSlot, const std::vector<int>& slotOfSource)
{
    if (productParam <= 0) return 0;
    const int source = productParam - 1;   // the engine's encoding: a channel, or kProductLayers for the external input

    int mapped = -1;
    if (source < kProductLayers)
        mapped = source < (int) slotOfSource.size() ? slotOfSource[(size_t) source] : -1;
    else if (source == kProductLayers)
        mapped = MixerEngine::kExtSidechainSrc;   // the product's external input is this binary's
    // Any other value is a bus the products do not have.

    if (! mu_mix::isLegalSidechainSource(mu_mix::StripKind::Channel, ownSlot, mapped, mu_limits::kMaxLayers, mu_limits::kMaxBuses))
        mapped = -1;
    return mapped + 1;
}

void setRumbleSourceRow(juce::ValueTree& node, int slot)
{
    for (int i = 0; i < node.getNumChildren(); ++i)
        if (auto row = node.getChild(i); row.hasType(mu_pp::kRowTag) && row.getProperty("id").toString() == "src")
        {
            row.setProperty("x", slot, nullptr);
            row.removeProperty("c", nullptr);
            return;
        }
    juce::ValueTree row(mu_pp::kRowTag);
    row.setProperty("id", "src", nullptr);
    row.setProperty("x", slot, nullptr);
    node.appendChild(row, nullptr);
}

} // namespace mu_tronic
