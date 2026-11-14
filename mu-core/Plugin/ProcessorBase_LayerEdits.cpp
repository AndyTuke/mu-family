// ProcessorBase, part of the class split by topic (code moved unchanged from ProcessorBase.cpp).
// Structural layer edits: add, remove, swap, reset a layer and its parameter rows.
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

// ─── Structural layer edits ──────────────────────────────────────────────────

void ProcessorBase::copyLayerParams(int from, int to)
{
    const auto src = layerParamPrefixes(layerLayout, from), dst = layerParamPrefixes(layerLayout, to);
    for (int k = 0; k < src.size(); ++k)
        forEachPrefixed(*this, src[k], [&](juce::RangedAudioParameter& rp, const juce::String& suffix)
        {
            if (auto* dp = apvts.getParameter(dst[k] + suffix)) dp->setValueNotifyingHost(rp.getValue());
        });
}

void ProcessorBase::swapLayerParams(int a, int b)
{
    const auto pa = layerParamPrefixes(layerLayout, a), pb = layerParamPrefixes(layerLayout, b);
    for (int k = 0; k < pa.size(); ++k)
        forEachPrefixed(*this, pa[k], [&](juce::RangedAudioParameter& rp, const juce::String& suffix)
        {
            if (auto* bp = apvts.getParameter(pb[k] + suffix))
            {
                const float va = rp.getValue(), vb = bp->getValue();
                rp.setValueNotifyingHost(vb);
                bp->setValueNotifyingHost(va);
            }
        });
}

void ProcessorBase::resetLayerParams(int slot)
{
    for (const auto& prefix : layerParamPrefixes(layerLayout, slot))
        forEachPrefixed(*this, prefix, [](juce::RangedAudioParameter& rp, const juce::String&)
        {
            rp.setValueNotifyingHost(rp.getDefaultValue());
        });
}

// rt-ok-fn: builds the sidechain source list for the UI and setup, message thread only
std::vector<juce::RangedAudioParameter*> ProcessorBase::sidechainSourceParams()
{
    std::vector<juce::RangedAudioParameter*> out;
    auto add = [this, &out](const juce::String& id)
    {
        if (auto* p = apvts.getParameter(id)) out.push_back(p);
    };
    for (int c = 0; c < limits_.layers; ++c)
        add("ch" + juce::String(c) + "_scSrc");
    for (const char* ret : { "ret_eff_scSrc", "ret_dly_scSrc", "ret_rev_scSrc" })
        add(ret);
    for (int b = 0; b < mu_limits::kMaxBuses; ++b)
        add("bus" + juce::String(b) + "_scSrc");
    return out;
}

void ProcessorBase::remapSidechainSources(const std::function<int(int)>& remap)
{
    remapSidechainSources(sidechainSourceParams(), remap);
}

void ProcessorBase::remapSidechainSources(const std::vector<juce::RangedAudioParameter*>& params, const std::function<int(int)>& remap)
{
    for (auto* p : params)
    {
        const int v  = juce::roundToInt(p->convertFrom0to1(p->getValue()));
        const int nv = juce::jlimit(0, mu_mixfx::kScSrcMax, remap(v));   // 0 off, 1..kMaxLayers a channel, then external, then the buses
        if (nv != v) p->setValueNotifyingHost(p->convertTo0to1((float) nv));
    }
}

// rt-ok-fn: state capture for save, message or host thread only
juce::ValueTree ProcessorBase::captureComposedState()
{
    flushClipAutoSaveForStateCapture();   // a late edit of a lit clip is in the saved bank
    auto state = mu_pp::captureState(apvts.state.getType(), layerLayout, layerExtras);

    // Each slot node gains its layer's clip bank and playing clip. Not part of captureLayer, so a layer
    // preset never carries a bank. An empty bank writes nothing, keeping a clip-free file unchanged.
    for (int i = 0; i < layerLayout.numLayers() && i < limits_.layers; ++i)
        if (layers[(size_t) i] != nullptr)
            if (auto node = mu_pp::findLayerNode(state, i); node.isValid())
            {
                layers[(size_t) i]->clips->writeTo(node);
                if (layers[(size_t) i]->activeClip >= 0)
                    node.setProperty("activeClip", layers[(size_t) i]->activeClip, nullptr);
            }

    if (canEditLayers())
    {
        // The colour is a slot's identity, not part of its sound, so it lives on the full state, not in a layer node.
        juce::StringArray colours;
        for (int i = 0; i < limits_.layers; ++i)
            colours.add(juce::String(layers[(size_t) i] != nullptr ? layers[(size_t) i]->colourIndex : i));
        state.setProperty("numVoices", numLayers.load(std::memory_order_relaxed), nullptr);
        state.setProperty("voiceColours", colours.joinIntoString(","), nullptr);
    }
    return state;
}

// rt-ok-fn: structural layer edit on the message or host thread under layoutWriteLock/layersLock/layerServicesLock; the audio thread only try-locks or reads the published view
void ProcessorBase::applyComposedState(const juce::ValueTree& state)
{
    dropClipWatches();   // the restored layers re-adopt a baseline; nothing is written for the restore
    // A restore that lowers the count publishes the new count BEFORE the parameters move: applyState
    // writes defaults into the slots the state lacks, and those slots must already be out of the count
    // by then or they would render with default parameters until the store below. A restore that grows
    // the count keeps the count last, so no block sees a slot counted before its data is in.
    const bool editsLayers = canEditLayers();
    const int  newCount    = editsLayers ? savedLayerCount(state) : 0;
    bool       shrinkStored = false;
    if (editsLayers)
    {
        jassert(getLayerAccess() == mu_core::LayerAccess::TryLock);
        const juce::ScopedLock lw(layoutWriteLock);
        if (newCount < numLayers.load(std::memory_order_relaxed))
        {
            storeLayerCount(newCount);
            shrinkStored = true;
        }
    }
    if (shrinkStored && retireBin.countPolicy() == mu_core::RetireBin::CountPolicy::CutAtCount)
        retireBin.cutAtOrAbove(newCount);

    mu_pp::applyState(state, layerLayout, layerExtras);

    // Every slot takes its node's clips: a node without a <Clips> child (an older file, or a slot the
    // state lacks) empties the bank, so a smaller state leaves no stale clips past its layer count.
    for (int i = 0; i < layerLayout.numLayers() && i < limits_.layers; ++i)
        if (layers[(size_t) i] != nullptr)
        {
            const auto node = mu_pp::findLayerNode(state, i);
            auto& layer = *layers[(size_t) i];
            layer.clips->readFrom(node);
            // A clip carries its type: one saved for another type than the slot now holds (a hand-edited file, an engine
            // this build reads differently) is dropped rather than launched into the wrong layer.
            if (layersAreSlots())
                for (int c = 0; c < mu_limits::kClipsPerLayer; ++c)
                    if (layer.clips->isOccupied(c) && layerPresetType(layer.clips->get(c)) != slotTypeId(i))
                        layer.clips->clear(c);
            const int active = (int) node.getProperty("activeClip", -1);
            layer.activeClip = layer.clips->isOccupied(active) ? active : -1;
        }
    publishClipMasks();

    if (! editsLayers) return;

    // Count last on a grow, so the audio thread never sees a count ahead of the data; then the colours (a state without colours keeps the current ones).
    // Thread contract: hosts normally call setStateInformation on the message thread, and the editor's modulation
    // reads, the Stager and the products' lock-free paths all assume that. The layout store below is the one part
    // that is also safe from a host thread, so it goes through the packed layout word under layoutWriteLock: a lost
    // or misattached serial would break the retire bin's same-block hand-over. A product that parks a tail before this call holds layoutWriteLock
    // across both (it is re-entrant), so the park's nextLayoutSerial() is the serial this store carries.
    // A slot host keeps its count (the capacity): only the colours are restored.
    if (layersAreSlots())
    {
        const auto slotToks = juce::StringArray::fromTokens(state.getProperty("voiceColours", "").toString(), ",", "");
        for (int i = 0; i < limits_.layers && i < slotToks.size(); ++i)
            if (layers[(size_t) i] != nullptr)
                layers[(size_t) i]->colourIndex = juce::jlimit(0, limits_.layers - 1, slotToks[i].getIntValue());
        return;
    }

    // TryLock mode only until Published-mode quiesce exists: this store and the parameter moves above assume
    // the audio thread's try-lock protocol.
    int oldCount = 0;
    if (! shrinkStored)
    {
        const juce::ScopedLock lw(layoutWriteLock);
        oldCount = numLayers.load(std::memory_order_relaxed);
        storeLayerCount(newCount);
        // A regrow into slots whose spares were fading in place: count first (above), then cut, as addLayer does
        // (retire bin rule R1.3), so no block has neither the live layer nor the tail and a later shrink parks
        // afresh. Inside the same layout hold (order layoutWriteLock, binMutex), so no structural edit can move
        // another slot's tail into these rows between the store and the cut.
        if (regrowCutsInPlaceTails)
            for (int s = oldCount; s < newCount; ++s)
                retireBin.cutSlot(s);
    }
    if (! shrinkStored && retireBin.countPolicy() == mu_core::RetireBin::CountPolicy::CutAtCount)
        retireBin.cutAtOrAbove(newCount);
    const auto toks = juce::StringArray::fromTokens(state.getProperty("voiceColours", "").toString(), ",", "");
    for (int i = 0; i < limits_.layers && i < toks.size(); ++i)
        if (layers[(size_t) i] != nullptr)
            layers[(size_t) i]->colourIndex = juce::jlimit(0, limits_.layers - 1, toks[i].getIntValue());
}

int ProcessorBase::savedLayerCount(const juce::ValueTree& state) const
{
    return juce::jlimit(1, limits_.layers, (int) state.getProperty("numVoices", unsavedLayerCount));
}

int ProcessorBase::firstUnusedColourIndex() const
{
    std::array<bool, mu_limits::kAbsoluteMaxLayers> used {};
    const int n = numLayers.load(std::memory_order_relaxed);
    for (int i = 0; i < n; ++i)
        if (layers[(size_t) i] != nullptr)
        {
            const int c = layers[(size_t) i]->colourIndex;
            if (c >= 0 && c < limits_.layers) used[(size_t) c] = true;
        }
    for (int c = 0; c < limits_.layers; ++c)
        if (! used[(size_t) c]) return c;
    return 0;
}

// rt-ok-fn: structural layer edit on the message or host thread under layoutWriteLock/layersLock/layerServicesLock; the audio thread only try-locks or reads the published view
int ProcessorBase::addLayer()
{
    const int n = numLayers.load(std::memory_order_relaxed);
    if (! canEditLayers() || n >= limits_.layers) return -1;
    if (getLayerAccess() != mu_core::LayerAccess::TryLock) return -1;   // refused, never half done: a Published host uses setLayerType
    prepareClipWatchesForStructuralEdit();

    // Phase 1: build and prepare the new layer before anything is locked.
    auto fresh = createLayer(n);
    if (fresh == nullptr) return -1;
    cancelPendingSwap(n);                       // the new slot must carry no stale staged swap
    fresh->colourIndex = firstUnusedColourIndex();
    fresh->bindParams(apvts, n);                // the render contract: bound before it is published (no-op by default)

    // Declared before the suspend guard so a replaced spare is released after processing resumes.
    std::unique_ptr<Layer> retired;
    ScopedSuspend suspend(*this);

    // Phase 2: publish under layersLock (pointer move, count, a clean mixer strip), microseconds. The layout
    // writer lock comes first (lock order layoutWriteLock, layersLock, binMutex): the count store advances the serial.
    {
        const juce::ScopedLock lw(layoutWriteLock);
        const juce::ScopedLock sl(layersLock);
        retired = publishLayerLocked(std::move(fresh));
        // A regrow into a slot whose spare was fading in place: the count is published first, then the entry
        // is cut, so no block has neither the live layer nor the tail; and the spare `retired` is released only
        // after this cut (retire bin rules R1.3 and R1.4). Nothing is parked in any product today.
        retireBin.cutSlot(n);
        mixerEngine.channels[(size_t) n].reset();
        mixerEngine.resetSidechainEnv(n);
        mixerEngine.resetStripRamp(n);
        layersMoved(n, n);
    }

    // A new layer starts with no clips. Explicit and outside layersLock: freeing trees is not a
    // microsecond job, and a re-used spare must not keep an old bank.
    clearLayerClips(n);

    // Phase 3: the slot's parameters back to defaults, still suspended so no block sees stale values.
    // Where the layer data is the truth the audio never reads these parameters, so processing resumes
    // first: the product's push runs beside live audio, as any edit of its own parameters does, and
    // layerAdded (which may read files) runs outside the edit's scope too.
    if (layerHoldsParamTruth())
    {
        suspend.release();
        syncSlotParams(n);
    }
    else
    {
        resetLayerParams(n);
    }
    layerAdded(n);
    return n;
}

// rt-ok-fn: structural layer edit on the message or host thread under layoutWriteLock/layersLock/layerServicesLock; the audio thread only try-locks or reads the published view
bool ProcessorBase::removeLayer(int idx)
{
    const int n = numLayers.load(std::memory_order_relaxed);
    if (! canEditLayers() || n <= 1 || idx < 0 || idx >= n) return false;   // the last layer stays
    if (getLayerAccess() != mu_core::LayerAccess::TryLock) return false;   // the Published-mode quiesce is not built yet
    prepareClipWatchesForStructuralEdit();

    // Phase 1: the down-shift renumbers every layer from idx upward, so every staged per-layer swap
    // (keyed by index) would land on the wrong slot. A staged full preset replaces all slots and wins.
    for (int s = 0; s < limits_.layers; ++s) cancelPendingSwap(s);

    // The slot the shift vacates stays filled when the product keeps spares; built before locking.
    std::unique_ptr<Layer> spare;
    if (keepSpareLayers)
    {
        spare = createLayer(n - 1);
        if (spare == nullptr) return false;
        spare->colourIndex = layers[(size_t) (n - 1)]->colourIndex;   // the spare keeps the slot's saved colour
    }

    std::unique_ptr<Layer> retired;             // released last, after processing resumes
    ScopedSuspend suspend(*this);

    // Phase 2: pointer shift, strip state shift and sidechain re-pointing under layersLock (after the layout
    // writer lock: the count store advances the serial).
    {
        const juce::ScopedLock lw(layoutWriteLock);
        const juce::ScopedLock sl(layersLock);
        mixerEngine.renumberSidechainsAfterRemove(idx);   // before the strips shift down
        retired = unpublishLayerLocked(idx, std::move(spare));
        if (retired != nullptr)
            layerRemoved(*retired, idx);   // before anything below moves the slots' state
        for (int d = idx; d < n - 1; ++d)
        {
            mixerEngine.channels[(size_t) d].copyFrom(mixerEngine.channels[(size_t) (d + 1)]);
            mixerEngine.moveSidechainEnv(d + 1, d);   // the follower moves with its strip
            mixerEngine.moveStripRamp(d + 1, d);      // and so do its gain ramp and bus routing
            layers[(size_t) d]->onMoved(d);
        }
        mixerEngine.channels[(size_t) (n - 1)].reset();
        mixerEngine.resetSidechainEnv(n - 1);
        mixerEngine.resetStripRamp(n - 1);
        // The removed slot's tails are cut; owned tails above it move down with their layers (empty bins in
        // every product today).
        retireBin.removeRow(idx, n);
        if (retireBin.countPolicy() == mu_core::RetireBin::CountPolicy::CutAtCount)
            retireBin.cutAtOrAbove(n - 1);
        layersReordered(LayerEdit::Remove, idx, -1);
        layersMoved(idx, n - 1);                // the shifted slots and the vacated top one
    }

    // The moved layers re-bind their parameter pointers to their new slot (C9) while layerEditInFlight still
    // keeps the audio thread from rendering them; outside layersLock because binding looks parameters up by id.
    for (int d = idx; d < n; ++d)
        if (layers[(size_t) d] != nullptr) layers[(size_t) d]->bindParams(apvts, d);

    // Phase 3: parameter values. Strips that ducked from the removed layer now duck from nothing and
    // the others follow their layer down (translated before the values shift).
    remapSidechainSources([idx, this](int v) { return mu_mix::sidechainParamAfterRemove(v, idx, limits_.layers); });
    remapLayerReferences([idx, this](int i) { return mu_mix::sidechainAfterRemove(i, idx, limits_.layers); });
    if (layerHoldsParamTruth())
    {
        suspend.release();                                 // the audio never reads these parameters: resume first
        for (int d = idx; d < n; ++d) syncSlotParams(d);   // the shifted slots and the vacated top one
    }
    else
    {
        for (int d = idx; d < n - 1; ++d) copyLayerParams(d + 1, d);
        resetLayerParams(n - 1);                // the vacated top slot
    }

    // The banks moved with their layers (the pointer shift), and so do the live playing clips; the vacated
    // top slot holds a spare or nothing and must start empty. Last, with processing resumed and the parameters
    // shifted, so the notification a surface acts on sees the finished edit. Remap first: the clear names the new index.
    suspend.release();
    liveState.remapLayers([idx, this](int i) { return mu_mix::sidechainAfterRemove(i, idx, limits_.layers); });
    for (int d = idx; d < n - 1; ++d) presetLabels[(size_t) d] = presetLabels[(size_t) (d + 1)];   // labels follow their layer down
    clearLayerClips(n - 1);
    return true;
}

// rt-ok-fn: structural layer edit on the message or host thread under layoutWriteLock/layersLock/layerServicesLock; the audio thread only try-locks or reads the published view
bool ProcessorBase::swapLayers(int a, int b)
{
    const int n = numLayers.load(std::memory_order_relaxed);
    if (! canEditLayers() || a < 0 || b < 0 || a >= n || b >= n || a == b) return false;
    prepareClipWatchesForStructuralEdit();   // both slots of a Published exchange too: the trackers are keyed by slot
    if (getLayerAccess() == mu_core::LayerAccess::Published) return swapLayersPublished(a, b);

    // Phase 1: staged swaps are keyed by index, so swapping the two slots would misdirect them.
    cancelPendingSwap(a);
    cancelPendingSwap(b);

    ScopedSuspend suspend(*this);

    // Phase 2: pointers and strip state exchanged under layersLock; sidechain sources re-pointed first.
    {
        const juce::ScopedLock lw(layoutWriteLock);
        const juce::ScopedLock sl(layersLock);
        mixerEngine.renumberSidechainsAfterSwap(a, b);
        swapLayersLocked(a, b);
        storeLayerCount(numLayers.load(std::memory_order_relaxed), std::memory_order_relaxed);   // same count, next serial
        mixerEngine.swapChannelState(a, b);
        retireBin.swapRows(a, b);              // owned tails follow their slot, in-place ones are cut
        layers[(size_t) a]->onMoved(a);
        layers[(size_t) b]->onMoved(b);
        layersReordered(LayerEdit::Swap, a, b);
        layersMoved(a, a);                      // only the two exchanged slots, not those between
        layersMoved(b, b);
    }

    // Re-bind the two moved layers to their new slots (C9), still before any block can render them.
    layers[(size_t) a]->bindParams(apvts, a);
    layers[(size_t) b]->bindParams(apvts, b);

    // Phase 3: parameter values. A strip that ducked from one swapped layer now ducks from the other
    // (translated before the values swap, so each strip's own source moves with it).
    remapSidechainSources([a, b, this](int v) { return mu_mix::sidechainParamAfterSwap(v, a, b, limits_.layers); });
    remapLayerReferences([a, b, this](int i) { return mu_mix::sidechainAfterSwap(i, a, b, limits_.layers); });
    if (layerHoldsParamTruth())
    {
        suspend.release();                                 // the audio never reads these parameters: resume first
        syncSlotParams(a);
        syncSlotParams(b);
    }
    else
        swapLayerParams(a, b);

    // The banks and playing clips swapped with their layers. Last, after the parameters moved and processing
    // resumed, so the notification a surface acts on sees the finished edit.
    suspend.release();
    liveState.remapLayers([a, b, this](int i) { return mu_mix::sidechainAfterSwap(i, a, b, limits_.layers); });
    std::swap(presetLabels[(size_t) a], presetLabels[(size_t) b]);
    publishClipMasks();
    return true;
}

// A Published host reorders without suspending: the audio thread is quiesced (design 2.1.2), so for the few
// moments the edit takes the host's blocks render nothing, the same silence a TryLock host's edit makes; nothing
// else about the layers changes. What travels with a layer: its object (engine, state, modulators, clips, name,
// colour), its mixer strip (level, sends, sidechain and bus assignment, follower), the tails owed to its strip, and its
// parameter rows under its type's prefix. What stays with the slot: the slot number itself.
// rt-ok-fn: structural layer edit on the message or host thread under layoutWriteLock/layersLock/layerServicesLock; the audio thread only try-locks or reads the published view
bool ProcessorBase::swapLayersPublished(int a, int b)
{
    mu_hotswap::assertMessageThread();   // one writer at a time: the editor's drag and a state restore are message-thread work
    if (layers[(size_t) a] == nullptr || layers[(size_t) b] == nullptr) return false;

    // The editor lets go of both layers before they move (modulator panel, content bindings), outside every lock.
    if (onLayerLeaving) { onLayerLeaving(a); onLayerLeaving(b); }

    // Everything that only reads is done before the audio thread is excluded, so the window in which its blocks render
    // nothing holds only waits, writes and pointer moves: which rows move where, and which sidechain parameters to rewrite.
    const auto liveA = layerLayout.prefix(a), liveB = layerLayout.prefix(b);
    const juce::String stripA = "ch" + juce::String(a) + "_", stripB = "ch" + juce::String(b) + "_";
    std::vector<RowMove> moves { { stripA, stripB }, { stripB, stripA } };
    if (liveA.isNotEmpty()) moves.push_back({ liveA, prefixOnSlot(liveA, b) });
    if (liveB.isNotEmpty()) moves.push_back({ liveB, prefixOnSlot(liveB, a) });
    const RowPlan rowPlan = layerHoldsParamTruth() ? RowPlan() : planRows(*this, apvts, moves);
    const auto sidechainParams = sidechainSourceParams();
    const double raisedAt = juce::Time::getMillisecondCounterHiRes();

    // Phase 0: exclude the audio thread. A timeout moves nothing (strip state is never moved without the grace).
    ScopedQuiesce quiesce(layerEditInFlight, layerAccess.epoch, 50);
    if (! quiesce.ok()) return false;

    // Phase 1: staged swaps are keyed by index, so swapping the two slots would misdirect them.
    cancelPendingSwap(a);
    cancelPendingSwap(b);

    // A parameter that names a layer (a Rumble's source) is rewritten now, at the old slots and before any pointer
    // moves, because the hook reads each slot's layer type: after the move the old indices would name the wrong layers.
    const auto oldToNew = [a, b, this](int i) { return mu_mix::sidechainAfterSwap(i, a, b, limits_.layers); };
    remapLayerReferences(oldToNew);

    // Phase 2: pointers, strip state, tails and the one published view, under the writers' locks (the count store
    // advances the serial, so the layout writer lock comes first).
    {
        const juce::ScopedLock lw(layoutWriteLock);
        const juce::ScopedLock sl(layersLock);
        mixerEngine.renumberSidechainsAfterSwap(a, b);
        swapLayersLocked(a, b);
        mixerEngine.swapChannelState(a, b);
        retireBin.swapRows(a, b);              // owned tails follow their strip, in-place ones are cut
        layers[(size_t) a]->onMoved(a);
        layers[(size_t) b]->onMoved(b);
        layersReordered(LayerEdit::Swap, a, b);
        layersMoved(a, a);
        layersMoved(b, b);

        // Each slot now holds the other's layer, so it is live on the other's type letters.
        layerLayout.setLivePrefix(a, prefixOnSlot(liveB, a));
        layerLayout.setLivePrefix(b, prefixOnSlot(liveA, b));

        auto view = makeLayerView();
        view.serial = nextLayoutSerial();
        layerAccess.views.publish(view, layerAccess.epoch);   // the view first, then the word: the serial order of setLayerType
        storeLayerCount(numLayers.load(std::memory_order_relaxed));
    }

    // What a type's services keep per slot moves with the layers (a Pattern's engine), before they re-bind.
    {
        std::array<mu_core::LayerServices*, (size_t) mu_core::kLayerTypeIdCount> built {};
        {
            const juce::ScopedLock sl(layerServicesLock);
            for (size_t i = 0; i < built.size(); ++i) built[i] = layerServices[i].get();
        }
        for (auto* services : built)
            if (services != nullptr) services->slotsSwapped(a, b);
    }

    // The moved layers re-bind their parameter pointers to the new slots (C9); the flag still keeps blocks out.
    layers[(size_t) a]->bindParams(apvts, a);
    layers[(size_t) b]->bindParams(apvts, b);

    // Phase 3: parameter values. A strip that ducked from one swapped layer now ducks from the other (translated
    // before the rows move, so each strip's own source moves with it), then each layer's rows follow it to its new slot.
    remapSidechainSources(sidechainParams, [a, b, this](int v) { return mu_mix::sidechainParamAfterSwap(v, a, b, limits_.layers); });
    if (layerHoldsParamTruth())
    {
        syncSlotParams(a);
        syncSlotParams(b);
    }
    else
        applyRows(rowPlan);
    quiesce.release();
    lastEditWindowMs.store(juce::Time::getMillisecondCounterHiRes() - raisedAt, std::memory_order_relaxed);

    // The banks and playing clips moved with their layers. Last, after the audio thread is running again, so the
    // notification a surface acts on sees the finished edit.
    liveState.remapLayers(oldToNew);
    std::swap(presetLabels[(size_t) a], presetLabels[(size_t) b]);
    publishClipMasks();
    if (retireBin.hasPendingWork()) requestDeferredWork();
    return true;
}

void ProcessorBase::resetLayer(int idx)
{
    if (idx < 0 || idx >= numLayers.load(std::memory_order_relaxed) || layers[(size_t) idx] == nullptr) return;
    prepareClipWatchesForStructuralEdit();

    // A Published host never suspends: it gives the slot a fresh layer of the same type through two publications
    // (setLayerType, via Empty), then returns the strip and the name to defaults, as a reset always does.
    if (getLayerAccess() == mu_core::LayerAccess::Published)
    {
        const auto type = (mu_core::LayerTypeId) layers[(size_t) idx]->typeId();
        if (type != mu_core::LayerTypeId::Empty)
        {
            setLayerType(idx, mu_core::LayerTypeId::Empty);
            setLayerType(idx, type);
        }
        mixerEngine.requestSidechainEnvReset(idx);   // the strip returns to defaults, so its follower restarts too
        resetLayerParams(idx);
        layers[(size_t) idx]->name.clear();
        return;
    }
    cancelPendingSwap(idx);                     // a staged swap would re-fill what we are clearing

    // Phase 1: build the default data before anything is locked (constructing allocates).
    auto fresh = createLayer(idx);
    if (fresh == nullptr) return;               // a product without createLayer overrides resetLayer

    ScopedSuspend suspend(*this);
    {
        // Phase 2: take the default data in place under layersLock, so the editor's pointers into the
        // layer stay valid; `fresh` leaves holding the old data and is destroyed after unlocking.
        const juce::ScopedLock sl(layersLock);
        layers[(size_t) idx]->adoptDefaults(*fresh);
        mixerEngine.channels[(size_t) idx].reset();
        mixerEngine.resetSidechainEnv(idx);
        mixerEngine.resetStripRamp(idx);
        layersMoved(idx, idx);
    }

    // Reset means back to a freshly added layer, so the bank empties too (adoptDefaults leaves it alone).
    clearLayerClips(idx);

    if (layerHoldsParamTruth())
    {
        suspend.release();                                 // the audio never reads these parameters: resume first
        syncSlotParams(idx);
    }
    else
    {
        resetLayerParams(idx);
    }
}
