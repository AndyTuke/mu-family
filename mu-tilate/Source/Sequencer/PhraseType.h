#pragma once

#include "Persistence/PhraseParams.h"
#include "Sequencer/LayerTypeInfo.h"
#include "Sequencer/Phrase.h"

// The Phrase layer type as a registry row (docs/mu-tronic/design.md 2.5): what a host that offers several layer types needs
// to describe and build a Phrase. The host adds what only it can: its services and UI factories, its header tag.
namespace mu_tilate
{

// A fresh Phrase for `slot`, not yet bound or prepared. The insert's delay lines are allocated here, off the audio thread and
// outside any lock; the per-slot seed keeps layers from sharing a noise sequence.
inline std::unique_ptr<Layer> createPhrase(int slot, mu_core::LayerTypeId /*type*/)
{
    auto p = std::make_unique<Phrase>();
    p->insert->setSlotIndex(slot);
    return p;
}

inline mu_core::LayerTypeInfo phraseLayerTypeInfo() noexcept
{
    mu_core::LayerTypeInfo info;
    info.id = mu_core::LayerTypeId::Phrase;
    info.name = "Phrase";
    info.group = "mu-Tilate";
    info.paramPrefixLetter = 'p';
    info.presetExtension = ".muPhrase";
    info.contentFolderName = "muTilate";   // the layer presets and samples are mu-Tilate's own, shared with it, not copied
    info.presetFolder = "Phrases";
    info.presetTag = kPhrasePresetTag;
    info.descriptor = "Loop slicer, timeline";
    info.discInitial = "T";
    info.addLayerParams = &addLayerParams;
    info.create = &createPhrase;
    return info;
}

} // namespace mu_tilate
