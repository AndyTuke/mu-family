// ProcessorBase, part of the class split by topic (code moved unchanged from ProcessorBase.cpp).
// Layer services, publishing the layer set and changing or creating a layer of a type.
#include "Plugin/ProcessorBase.h"
#include "Plugin/MixerFxParams.h"           // mu_mixfx::isGlobalFxParamId
#include "Sequencer/ClipClipboard.h"         // the process-wide clip clipboard
#include "Persistence/PresetFiles.h"         // mu_pp full-preset files
#include "Audio/FX/Slots/FXAlgorithmDef.h"   // FXAlgorithmRegistry
#include "Audio/AlgorithmNames.h"             // mu_audio::kInsertAlgorithmCount
#include <cstring>                            // std::strcmp — alloc-free suffix compare
#include <map>
#include <memory>
#include <mutex>
#include "Plugin/ProcessorBase_LayerEditSupport.h"   // structural-edit helpers shared with the layer-types file

// ─── Typed slots ─────────────────────────────────────────────────────────────────────────────

// rt-ok-fn: structural layer edit on the message or host thread under layoutWriteLock/layersLock/layerServicesLock; the audio thread only try-locks or reads the published view
mu_core::LayerServices* ProcessorBase::ensureLayerServices(const mu_core::LayerTypeInfo& info)
{
    const int i = (int) info.id;
    if (info.createServices == nullptr || i < 0 || i >= mu_core::kLayerTypeIdCount) return nullptr;
    {
        const juce::ScopedLock sl(layerServicesLock);
        if (layerServices[(size_t) i] != nullptr) return layerServices[(size_t) i].get();
    }

    // Built outside the table's lock (a type's services may load tables or allocate engines), inserted under it.
    auto built = info.createServices(*this);
    if (built == nullptr) return nullptr;
    const juce::ScopedLock sl(layerServicesLock);
    if (layerServices[(size_t) i] != nullptr) return layerServices[(size_t) i].get();   // another thread was first
    // A processor already prepared hands its rate and block size to services built after it; prepareLayerRender
    // reaches them through the table once they are in it, so a prepare racing this one still ends on the newest.
    double rate = 0.0;
    int block = 0;
    {
        const juce::ScopedLock ll(layersLock);
        rate  = layersPreparedRate;
        block = layersPreparedBlock;
    }
    if (rate > 0.0) built->prepare(rate, block);
    layerServices[(size_t) i] = std::move(built);
    audioServices[(size_t) i].store(layerServices[(size_t) i].get(), std::memory_order_release);
    return layerServices[(size_t) i].get();
}

// rt-ok-fn: structural layer edit on the message or host thread under layoutWriteLock/layersLock/layerServicesLock; the audio thread only try-locks or reads the published view
void ProcessorBase::runLayerServicesWork()
{
    // Copied out under the table's lock and run outside it: a services object's work may reach back into the table
    // (a content asking for another type's services). Entries live until the processor is destroyed.
    std::array<mu_core::LayerServices*, (size_t) mu_core::kLayerTypeIdCount> built {};
    {
        const juce::ScopedLock sl(layerServicesLock);
        for (size_t i = 0; i < built.size(); ++i) built[i] = layerServices[i].get();
    }
    for (auto* s : built)
        if (s != nullptr) s->deferredWork();
}

// rt-ok-fn: structural layer edit on the message or host thread under layoutWriteLock/layersLock/layerServicesLock; the audio thread only try-locks or reads the published view
mu_core::LayerServices* ProcessorBase::findLayerServices(mu_core::LayerTypeId id) const
{
    const int i = (int) id;
    if (i < 0 || i >= mu_core::kLayerTypeIdCount) return nullptr;
    const juce::ScopedLock sl(layerServicesLock);
    return layerServices[(size_t) i].get();
}
// docs/mu-tronic/design.md 2.1.2: a slot's type changes in two view publications with a grace wait between
// them, nothing suspended and nothing muted. The audio thread reaches layers only through the published view
// (LayerAccess::Published), so every pointer move here is invisible to it until a view is published.

mu_core::LayerView ProcessorBase::makeLayerView() const
{
    mu_core::LayerView v;
    v.numLive = numLayers.load(std::memory_order_relaxed);
    for (int s = 0; s < mu_limits::kMaxLayers; ++s) v.layer[(size_t) s] = layers[(size_t) s].get();
    return v;
}

void ProcessorBase::resetOwnLayerParams(int slot)
{
    if (const auto own = layerLayout.prefix(slot); own.isNotEmpty())
        forEachPrefixed(*this, own, [](juce::RangedAudioParameter& rp, const juce::String&)
        {
            rp.setValueNotifyingHost(rp.getDefaultValue());
        });
}

// rt-ok-fn: structural layer edit on the message or host thread under layoutWriteLock/layersLock/layerServicesLock; the audio thread only try-locks or reads the published view
void ProcessorBase::publishLayers(int liveCount)
{
    jassert(getLayerAccess() == mu_core::LayerAccess::Published);
    const juce::ScopedLock lw(layoutWriteLock);
    const juce::ScopedLock sl(layersLock);
    auto v = makeLayerView();
    v.numLive = liveCount;
    v.serial = nextLayoutSerial();
    layerAccess.views.publish(v, layerAccess.epoch);   // the view first, then the word: the serial order of setLayerType
    storeLayerCount(liveCount);
}

bool ProcessorBase::setLayerType(int slot, mu_core::LayerTypeId type, const juce::ValueTree* initial)
{
    using mu_core::LayerTypeId;
    if (getLayerAccess() != mu_core::LayerAccess::Published) return false;   // a TryLock host edits through add / remove
    if (slot < 0 || slot >= numLayers.load(std::memory_order_relaxed) || layers[(size_t) slot] == nullptr) return false;
    const mu_core::LayerTypeInfo* info = type == LayerTypeId::Empty ? nullptr : layerTypes.find(type);
    if (type != LayerTypeId::Empty && info == nullptr) return false;
    if ((LayerTypeId) layers[(size_t) slot]->typeId() == type) return true;
    mu_hotswap::assertMessageThread();   // one caller at a time: the editor, a state restore and a swap commit are all message-thread work
    prepareClipWatchesForStructuralEdit();

    // Step 3: the UI lets go of the old layer (modulator panel, content) before it can leave the view.
    if (onLayerLeaving) onLayerLeaving(slot);

    // Step 1: a staged swap of the old type must not land on the new one.
    cancelPendingSwap(slot);

    // Step 2: build and prepare the new layer before anything is visible. A prepareToPlay on a host thread may
    // land before step 8; the generation read here is compared again there.
    uint32_t generation = 0;
    double rate = 0.0;
    int block = 0;
    {
        const juce::ScopedLock sl(layersLock);
        generation = prepareGeneration;
        rate  = layersPreparedRate;
        block = layersPreparedBlock;
    }
    // The type's shared services exist before its first layer does; the layer is wired to them before it is prepared.
    mu_core::LayerServices* services = info != nullptr ? ensureLayerServices(*info) : nullptr;
    std::unique_ptr<Layer> fresh = info != nullptr ? info->create(slot, type) : createLayer(slot);
    if (fresh == nullptr) return false;
    if (services != nullptr && info->attachServices != nullptr) info->attachServices(*fresh, *services, slot);
    if (rate > 0.0) fresh->prepare(rate, block);
    // The slot's identity stays with the slot: its colour and its name.
    fresh->colourIndex = layers[(size_t) slot]->colourIndex;
    fresh->name        = layers[(size_t) slot]->name;

    // Step 4: the last moment the old layer may read its parameters, none has changed yet.
    if (layers[(size_t) slot]->retirePolicy().kind != mu_core::RetirePolicy::Kind::None)
        layers[(size_t) slot]->beginRetire();

    // Step 5: publish view V1 with the slot empty and park the old layer in the bin. The bin entry is written
    // before V1 is published, with V1's serial as its activation, so the first block that loads V1 renders the
    // old layer as a tail and no block has neither. Lock order: layoutWriteLock, layersLock, binMutex.
    uint64_t afterV1 = 0;
    {
        const juce::ScopedLock lw(layoutWriteLock);
        const juce::ScopedLock sl(layersLock);
        std::unique_ptr<Layer> old = std::move(layers[(size_t) slot]);
        auto v1 = makeLayerView();
        v1.serial = nextLayoutSerial();
        retireBin.park(slot, std::move(old), v1.serial, false);
        layerAccess.views.publish(v1, layerAccess.epoch);
        storeLayerCount(numLayers.load(std::memory_order_relaxed));   // the layout word follows the view: the same serial
        afterV1 = layerAccess.epoch.stamp();
    }

    // Step 6: no block that still renders the old layer live can overlap step 7. A timeout costs at most one
    // block of a parameter mix; nothing is freed by step 7.
    layerAccess.epoch.waitFor(afterV1, 50);

    // Step 7: the new type's parameters to defaults (or the initial node) while nothing live reads them, then the
    // layer binds them. Only the NEW type's prefix is touched (a same-prefix type, Kick to Bass, shares t{N}_; the
    // old type's dormant rows are left alone and never saved). The mixer strip (ch{N}_) is not touched, and neither
    // is its sidechain follower: the strip and the source it ducks from are the same after a type change, so zeroing
    // the follower would only unduck a strip that is still being ducked. The new layer sits in layers[] but is not in
    // any view, so the audio thread cannot reach it.
    {
        const juce::ScopedLock sl(layersLock);
        layers[(size_t) slot] = std::move(fresh);
    }
    layerLayout.setLivePrefix(slot, info != nullptr && info->paramPrefixLetter != 0
                                        ? juce::String::charToString((juce::juce_wchar) info->paramPrefixLetter) + juce::String(slot) + "_"
                                        : juce::String());
    resetOwnLayerParams(slot);
    if (initial != nullptr && initial->isValid()) applyLayerNode(slot, *initial);
    layers[(size_t) slot]->bindParams(apvts, slot);
    layers[(size_t) slot]->completeSetup(apvts, slot);   // the layer's own message-thread setup, once, outside every lock
    clearLayerClips(slot);   // a type change empties the clip bank (a clip carries its type)

    // Step 8: V2 with the new layer. A prepare that ran since step 2 (a new rate or block size) is repeated for the
    // new layer first, so its first block is prepared for that block's rate and size.
    {
        const juce::ScopedLock lw(layoutWriteLock);
        const juce::ScopedLock sl(layersLock);
        if (prepareGeneration != generation && layersPreparedRate > 0.0)
            layers[(size_t) slot]->prepare(layersPreparedRate, layersPreparedBlock);
        auto v2 = makeLayerView();
        v2.serial = nextLayoutSerial();
        layerAccess.views.publish(v2, layerAccess.epoch);
        storeLayerCount(numLayers.load(std::memory_order_relaxed));
    }

    // Step 9: the old view and the old layer are freed after their grace by the views' reclaim and the bin's
    // service, on this side; ask for a pass.
    if (retireBin.hasPendingWork()) requestDeferredWork();
    publishClipMasks();
    return true;
}

bool ProcessorBase::createLayerOfType(int slot, mu_core::LayerTypeId type)
{
    const auto* info = layerTypes.find(type);
    if (info != nullptr && slotTypeId(slot) == mu_core::LayerTypeId::Empty)
    {
        const auto file = presetDirOf(*info).getChildFile("_default." + presetExtensionOf(*info));
        if (file.existsAsFile())
        {
            const auto xml = juce::XmlDocument::parse(file);
            const auto tree = xml != nullptr ? juce::ValueTree::fromXml(*xml) : juce::ValueTree();
            // Only a file that is this type's (the five Track types share one extension; typeFromPreset tells them apart).
            if (tree.isValid() && layerPresetType(tree, file.getFileExtension()) == type)
            {
                const auto node = info->presetFileToNode != nullptr ? info->presetFileToNode(tree, "_default") : tree;
                if (node.isValid() && setLayerType(slot, type, &node))
                    return true;
            }
        }
    }
    return setLayerType(slot, type);
}
