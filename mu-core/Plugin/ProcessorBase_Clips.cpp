// ProcessorBase, part of the class split by topic (code moved unchanged from ProcessorBase.cpp).
// Clips and scenes: store, clear, load, copy, paste and launch.
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

void ProcessorBase::clearLayerClips(int slot)
{
    if (slot < 0 || slot >= limits_.layers || layers[(size_t) slot] == nullptr) return;
    layers[(size_t) slot]->clips->clearAll();
    layers[(size_t) slot]->activeClip = -1;
    clipWatch[(size_t) slot].reset();
    presetLabels[(size_t) slot] = juce::String();   // a fresh or reset layer came from no preset
    liveState.clearLayerClips(slot);
    publishClipMasks();
}

void ProcessorBase::noteNonClipStage(int slot)
{
    cancelPendingSwap(slot);   // even if this stage is then aborted, an earlier queued clip must not land unlit
    if (slot >= 0 && slot < limits_.layers && layers[(size_t) slot] != nullptr)
    {
        layers[(size_t) slot]->activeClip = -1;
        presetLabels[(size_t) slot] = juce::String();   // the product re-notes the new preset's label after staging it
        clipWatch[(size_t) slot].reset();   // no clip is lit any more: nothing to track
    }
    liveState.clearLayerClips(slot);   // a plain layer-preset load also supersedes a queued clip
}

void ProcessorBase::notePendingPresetTree(int slot, const juce::ValueTree& staged)
{
    if (slot < 0 || slot >= limits_.layers) return;
    pendingPresetTree[(size_t) slot] = hasPendingSwap(slot) ? staged : juce::ValueTree();
}

void ProcessorBase::publishClipMasks()
{
    for (int i = 0; i < limits_.layers; ++i)
    {
        liveState.setClipMask(i, clipMask(i));
        const auto* layer = getLayer(i);
        for (int c = 0; c < mu_limits::kClipsPerLayer; ++c)
            liveState.setClipName(i, c, layer != nullptr ? layer->clips->getName(c) : juce::String());
    }
}

juce::String ProcessorBase::clipLabel(int slot) const
{
    if (slot >= 0 && slot < limits_.layers && presetLabels[(size_t) slot].isNotEmpty())
        return presetLabels[(size_t) slot];
    return getLayerName(slot);
}

void ProcessorBase::noteLayerPresetLabel(int slot, const juce::String& label)
{
    if (slot >= 0 && slot < limits_.layers)
        presetLabels[(size_t) slot] = label;
}

// rt-ok-fn: preset/clip handling on the message thread
bool ProcessorBase::storeClip(int slot, int clip, const juce::String& name)
{
    mu_hotswap::assertMessageThread();   // the clip bank is message-thread only (a controller callback must marshal first)
    auto* layer = getLayer(slot);
    if (layer == nullptr || ! mu_core::ClipBank::isValidIndex(clip)) return false;

    // A preset still waiting for its boundary is what the layer is about to be, but the live layer does not
    // show it yet: store that preset, and let it land as chosen.
    juce::ValueTree tree;
    if (hasPendingSwap(slot) && pendingPresetTree[(size_t) slot].isValid())
        tree = pendingPresetTree[(size_t) slot];
    else if (hasPendingSwap(slot))
    {
        // A queued clip launch: the store is authoritative, so drop it and the lamps that followed it.
        cancelPendingSwap(slot);
        layer->activeClip = -1;
        liveState.clearLayerClips(slot);
    }
    if (! tree.isValid())
        tree = captureLayerPresetTree(slot);
    if (! tree.isValid()) return false;
    const bool storedLive = ! (hasPendingSwap(slot) && pendingPresetTree[(size_t) slot].isValid());

    // Named after the preset the layer came from unless the caller names it; the layer then carries that name.
    const juce::String stored = name.isNotEmpty() ? name : clipLabel(slot);
    layer->clips->store(clip, tree, stored);   // store deep-copies, so the capture is not aliased
    noteLayerPresetLabel(slot, stored);
    publishClipMasks();

    // What was stored is the baseline for the layer's auto-save, adopted and not written. A staged tree stored instead
    // of the live layer leaves live != baseline until it commits, so the tracker starts again from the commit.
    auto& w = clipWatch[(size_t) slot];
    w.reset();
    if (storedLive)
    {
        w.clip     = clip;
        w.baseline = tree;
    }
    return true;
}

bool ProcessorBase::clearClip(int slot, int clip)
{
    mu_hotswap::assertMessageThread();   // the clip bank is message-thread only (a controller callback must marshal first)
    auto* layer = getLayer(slot);
    if (layer == nullptr || ! layer->clips->clear(clip)) return false;

    // The cleared clip can no longer be the one this layer is remembered as playing; a launch of it still
    // waiting for its boundary is cancelled (it would land a clip that no longer exists).
    if (layer->activeClip == clip)
        layer->activeClip = -1;
    if (clipWatch[(size_t) slot].clip == clip)
        clipWatch[(size_t) slot].reset();
    liveState.clipCleared(slot, clip);
    publishClipMasks();
    return true;
}

bool ProcessorBase::loadClipFromFile(int slot, int clip, const juce::File& layerPreset)
{
    mu_hotswap::assertMessageThread();   // the clip bank is message-thread only (a controller callback must marshal first)
    auto* layer = getLayer(slot);
    if (layer == nullptr || ! mu_core::ClipBank::isValidIndex(clip)) return false;

    // A typed slot host takes a file of the slot's own type only (the clip carries that type), converted to the
    // composed node a capture would write when the type's file has another shape.
    if (layersAreSlots())
    {
        std::unique_ptr<juce::XmlElement> fileXml;
        if (layerPreset.existsAsFile())
            fileXml = juce::XmlDocument::parse(layerPreset);
        const auto tree = fileXml != nullptr ? juce::ValueTree::fromXml(*fileXml) : juce::ValueTree();
        const auto type = tree.isValid() ? layerPresetType(tree, layerPreset.getFileExtension()) : mu_core::LayerTypeId::Empty;
        const auto* info = layerTypes.find(type);
        const auto node = info != nullptr && info->presetFileToNode != nullptr
                              ? info->presetFileToNode(tree, layerPreset.getFileNameWithoutExtension()) : tree;
        if (info == nullptr || ! node.isValid() || ! clipFitsSlot(slot, node))
        {
            if (onLoadError) onLoadError("Could not read \"" + layerPreset.getFileName() + "\"");
            return false;
        }
        layer->clips->store(clip, node, layerPreset.getFileNameWithoutExtension());
        publishClipMasks();
        return true;
    }

    // The file must be this product's layer preset: its root tag is the tag a capture would write.
    std::unique_ptr<juce::XmlElement> xml;
    if (layerPreset.existsAsFile())
        xml = juce::XmlDocument::parse(layerPreset);
    const auto fileTree = xml != nullptr ? juce::ValueTree::fromXml(*xml) : juce::ValueTree();
    if (! fileTree.isValid() || ! clipFitsSlot(slot, fileTree))
    {
        if (onLoadError) onLoadError("Could not read \"" + layerPreset.getFileName() + "\"");
        return false;
    }

    // Stored as read: any migration runs at launch, on a copy.
    layer->clips->store(clip, fileTree, layerPreset.getFileNameWithoutExtension());
    publishClipMasks();
    return true;
}

// rt-ok-fn: preset/clip handling on the message thread
bool ProcessorBase::clipFitsSlot(int slot, const juce::ValueTree& tree)
{
    if (! tree.isValid() || getLayer(slot) == nullptr) return false;
    if (layersAreSlots())
        return layerPresetType(tree) == slotTypeId(slot);

    // A list host: the layer's capture writes the root tag every clip of that layer carries.
    const auto expected = captureLayerPresetTree(slot).getType();
    return expected.toString().isNotEmpty() && tree.hasType(expected);
}

bool ProcessorBase::copyClip(int slot, int clip)
{
    mu_hotswap::assertMessageThread();   // the clip bank and the clipboard are message-thread only
    auto* layer = getLayer(slot);
    if (layer == nullptr || ! layer->clips->isOccupied(clip)) return false;   // an empty clip has nothing to copy

    flushClipAutoSave(slot);   // a late edit of the lit clip is in the bank before it is read
    const auto tree = layer->clips->get(clip);
    mu_core::clipClipboard().set(tree, layer->clips->getName(clip),
                                 layersAreSlots() ? layerPresetType(tree) : mu_core::LayerTypeId::Empty);
    return true;
}

bool ProcessorBase::cutClip(int slot, int clip)
{
    if (! copyClip(slot, clip)) return false;   // a failed copy clears nothing
    return clearClip(slot, clip);
}

bool ProcessorBase::canPasteClip(int slot)
{
    const auto& board = mu_core::clipClipboard();
    return board.hasClip() && clipFitsSlot(slot, board.get());
}

bool ProcessorBase::pasteClip(int slot, int clip)
{
    mu_hotswap::assertMessageThread();   // the clip bank and the clipboard are message-thread only
    auto* layer = getLayer(slot);
    if (layer == nullptr || ! mu_core::ClipBank::isValidIndex(clip)) return false;
    if (hasPendingFullPreset()) return false;   // a staged full preset replaces the layers and their banks

    const auto& board = mu_core::clipClipboard();
    const auto  tree  = board.get();
    if (! board.hasClip() || ! clipFitsSlot(slot, tree)) return false;

    // Settle any landed launch first, then see whether the target is what the layer plays or is heading to.
    liveState.reconcile();
    const bool lit = liveState.getPlayingClip(slot) == clip || liveState.getQueuedClip(slot) == clip;

    // Pasting onto the playing clip: the live layer is the old contents until the relaunch lands, so auto-save must not
    // write them over the paste (it stays suspended until the launch commits).
    if (liveState.getPlayingClip(slot) == clip)
    {
        clipWatch[(size_t) slot].reset();
        clipWatch[(size_t) slot].suspended = true;
    }

    const auto previous     = layer->clips->get(clip);
    const auto previousName = layer->clips->getName(clip);
    layer->clips->store(clip, tree, board.getName());

    // The lit clip's live layer must follow its new contents, so the lit pad still equals what plays.
    if (lit && ! liveState.clipPasted(slot, clip))
    {
        layer->clips->store(clip, previous, previousName);   // the relaunch could not be staged: change nothing
        clipWatch[(size_t) slot].suspended = false;
        return false;
    }
    publishClipMasks();
    return true;
}

bool ProcessorBase::launchScene(int clipIndex)
{
    mu_hotswap::assertMessageThread();
    mu_core::ControlAction a;
    a.type  = mu_core::ControlActionType::LaunchScene;
    a.index = clipIndex;
    return liveState.perform(a);
}

bool ProcessorBase::launchClip(int slot, int clip)
{
    mu_hotswap::assertMessageThread();   // the clip bank is message-thread only (a controller callback must marshal first)
    auto* layer = getLayer(slot);
    if (layer == nullptr || ! layer->clips->isOccupied(clip)) return false;
    if (hasPendingFullPreset()) return false;   // a staged full preset lands first and replaces the layers

    // A clip carries its type and loads only into a slot of that type: a launch is a staged swap of the same layer,
    // never a type change.
    if (! clipFitsSlot(slot, layer->clips->get(clip))) return false;

    // The same stage path as a layer-preset load; get() deep-copies, so the stage can migrate the
    // copy and the bank is never written.
    // A hook that could not build the clip stages nothing and leaves the serial alone; a stage, and the
    // stopped path's apply-at-once (which cancels first), both move it.
    // The edits of the clip being left are in its bank before the stage (and before a stopped launch overwrites the layer).
    flushClipAutoSave(slot);

    const auto serialBefore = slotStageSerial(slot);
    stageLayerPreset(slot, layer->clips->get(clip), true, true);
    const bool staged = slotStageSerial(slot) != serialBefore;
    if (staged)
    {
        noteLayerPresetLabel(slot, layer->clips->getName(clip));   // the layer is now that clip: a re-store keeps its name

        // The launch changes the layer without an edit: auto-save adopts a new baseline when it lands, writing nothing.
        auto& w = clipWatch[(size_t) slot];
        w.expectSwap = true;
        w.serial     = slotStageSerial(slot);
    }
    return staged;
}

uint8_t ProcessorBase::clipMask(int slot) const
{
    const auto* layer = getLayer(slot);
    return layer != nullptr ? layer->clips->mask() : (uint8_t) 0;
}
