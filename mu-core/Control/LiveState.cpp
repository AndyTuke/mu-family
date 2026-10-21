#include "LiveState.h"
#include <algorithm>

namespace mu_core
{

namespace
{
const juce::Identifier kPerformancePath { "performancePath" };
const juce::Identifier kPerformanceItem { "performanceItem" };
const juce::Identifier kPerformanceMode { "performanceMode" };
}

bool LiveState::handles(ControlActionType t) noexcept
{
    return t == ControlActionType::PerformanceMode || t == ControlActionType::PerformanceItem
        || t == ControlActionType::PerformancePrev || t == ControlActionType::PerformanceNext
        || t == ControlActionType::SelectLayer || t == ControlActionType::LaunchClip
        || t == ControlActionType::ClipNext || t == ControlActionType::ClipPrev || t == ControlActionType::LaunchScene
        || t == ControlActionType::AppPrev || t == ControlActionType::AppNext;
}

void LiveState::changed()
{
    ++revision;
    listeners.call([](Listener& l) { l.liveStateChanged(); });
}

bool LiveState::loadPerformance(mu_pp::PerformanceFile file, const juce::File& where)
{
    if (performanceMode) return false;   // the list is fixed while the screen is locked
    cancelQueuedItem();
    performance     = std::move(file);
    performanceFile = where;
    currentItem     = -1;
    queuedItem      = -1;
    changed();
    return true;
}

bool LiveState::itemLoadable(int item) const
{
    return item >= 0 && item < mu_pp::PerformanceFile::kMaxItems && cb.canLoad && cb.canLoad(item);
}

bool LiveState::setMode(bool on, int seedLayer)
{
    if (on == performanceMode) return true;
    if (on)
    {
        if (! (cb.supportsPerformanceMode && cb.supportsPerformanceMode())) return false;
        if (performance.count() == 0) return false;   // nothing to perform
        const int layers = cb.layerCount ? cb.layerCount() : 1;
        selectedLayer = juce::jlimit(0, juce::jmax(0, layers - 1), seedLayer);
    }
    else
    {
        // Leaving the mode drops a queued item: nothing is left to land it, and the lamps would lie.
        cancelQueuedItem();
        queuedItem = -1;
    }
    performanceMode = on;
    changed();
    return true;
}

bool LiveState::selectItem(int item)
{
    if (! itemLoadable(item)) return false;

    if (item == currentItem)
    {
        if (queuedItem < 0) return true;   // already playing it: nothing to do
        if (cb.cancelItem) cb.cancelItem();   // pressing the playing item while another is queued cancels the switch
        queuedItem = -1;
        changed();
        return true;
    }
    if (item == queuedItem) return true;   // already on its way

    const auto result = cb.stageItem ? cb.stageItem(item) : Stage::Failed;
    if (result == Stage::Failed) return false;   // it did not load: nothing is staged, nothing changes
    if (result == Stage::Applied) { currentItem = item; queuedItem = -1; clampSelectedLayer(); }   // the new preset may have fewer layers
    else                          { queuedItem = item; queuedSerial = cb.stageSerial ? cb.stageSerial() : 0; }
    changed();
    return true;
}

bool LiveState::stepItem(int direction)
{
    // Step from where the switch is heading: the queued item if any, else the current one, so two presses
    // before the commit land two items on. Wraps; skips empty, missing and other-product items.
    const int n      = mu_pp::PerformanceFile::kMaxItems;
    const int origin = queuedItem >= 0 ? queuedItem : currentItem;
    for (int i = 1; i <= n; ++i)
    {
        const int candidate = origin < 0 ? (direction > 0 ? i - 1 : n - i)
                                         : ((origin + direction * i) % n + n) % n;
        if (candidate == origin) continue;   // no other loadable item
        if (itemLoadable(candidate)) return selectItem(candidate);
    }
    return false;
}

bool LiveState::perform(const ControlAction& a)
{
    using T = ControlActionType;
    switch (a.type)
    {
        case T::PerformanceMode:
            return setMode(a.index < 0 ? ! performanceMode : a.index != 0, a.layer);   // `index` (a mapped button's value is the message's)
        case T::LaunchClip:      return startClip(a.layer, a.index);   // the on-screen clip strip launches with the mode off too
        case T::LaunchScene:     return launchScene(a.index);          // so does the scene control
        default: break;
    }

    if (! performanceMode) return false;   // every other action needs the mode

    switch (a.type)
    {
        case T::PerformanceItem: return selectItem(a.index);
        case T::PerformancePrev: return stepItem(-1);
        case T::PerformanceNext: return stepItem(+1);
        case T::SelectLayer:
        {
            const int layers = cb.layerCount ? cb.layerCount() : 1;
            if (a.layer < 0 || a.layer >= layers) return false;
            if (a.layer != selectedLayer) { selectedLayer = a.layer; changed(); }
            return true;
        }
        case T::AppPrev:
        case T::AppNext:         return false;   // not available: there is no multi-app hub to switch to
        case T::ClipNext:        return stepClip(+1);
        case T::ClipPrev:        return stepClip(-1);
        default: return false;
    }
}

void LiveState::cancelQueuedItem()
{
    if (queuedItem < 0 || ! cb.cancelItem) return;
    cb.cancelItem();   // a cancel never moves the full-commit count, so it cannot read as a landing
}

void LiveState::syncFullCommits()
{
    seenFullCommits = cb.fullCommitCount ? cb.fullCommitCount() : 0;
}

void LiveState::clampSelectedLayer()
{
    const int layers = cb.layerCount ? cb.layerCount() : 1;
    selectedLayer = juce::jlimit(0, juce::jmax(0, layers - 1), selectedLayer);   // a commit can shrink the layer count
}

void LiveState::fullPresetCommitted()
{
    // A full preset replaced every layer, so no launch is still on its way and the pads show only what the
    // new layers saved as playing.
    const bool hadClips = resetClips();
    syncFullCommits();
    if (queuedItem >= 0) { reconcileItem(); if (hadClips) changed(); return; }
    // A preset that was not one of this performance's items took over.
    const int before = selectedLayer;
    clampSelectedLayer();
    if (currentItem >= 0 || selectedLayer != before) { currentItem = -1; changed(); }
    else if (hadClips) changed();
}

bool LiveState::needsSettling() const noexcept
{
    if (performanceMode || queuedItem >= 0) return true;
    for (const auto& c : clipState)
        if (c.playing >= 0 || c.queued >= 0) return true;
    return false;
}

void LiveState::reconcile()
{
    reconcileItem();
    reconcileClips();
}

void LiveState::reconcileItem()
{
    if (queuedItem < 0) return;
    const bool pending = cb.fullPending && cb.fullPending();
    const bool ours    = cb.stageSerial && cb.stageSerial() == queuedSerial;
    if (pending && ! ours)
    {
        queuedItem = -1;   // another load replaced the staged preset: our item is no longer on its way
        changed();
    }
    else if (! pending)
    {
        currentItem = ours ? queuedItem : -1;   // it landed (or another preset did): settle
        queuedItem  = -1;
        clampSelectedLayer();
        changed();
    }
}

// ── The clip bank ────────────────────────────────────────────────────────────

void LiveState::setPlaying(int layer, int clip)
{
    clipState[(size_t) layer].playing = clip;
    if (cb.clipPlaying) cb.clipPlaying(layer, clip);
}

bool LiveState::dropClipState()
{
    bool any = false;
    for (auto& c : clipState)
    {
        any |= c.playing >= 0 || c.queued >= 0;
        c.playing = c.queued = -1;
    }
    return any;
}

bool LiveState::seedClipsFromLayers()
{
    if (! cb.savedClip) return false;
    bool any = false;
    const int layers = std::min(getLayerCount(), (int) mu_limits::kMaxLayers);
    for (int layer = 0; layer < layers; ++layer)
    {
        const int clip = cb.savedClip(layer);
        if (clip < 0 || clip >= mu_limits::kClipsPerLayer) continue;
        clipState[(size_t) layer].playing = clip;   // the owner already holds it; no clipPlaying call
        any = true;
    }
    return any;
}

bool LiveState::resetClips()
{
    const bool dropped = dropClipState();
    return seedClipsFromLayers() || dropped;
}

bool LiveState::startClip(int layer, int clip)
{
    if (! inRange(layer) || layer >= getLayerCount() || clip < 0 || clip >= mu_limits::kClipsPerLayer) return false;
    reconcileClips();   // first drop anything a full preset applied since the last settle has made stale
    auto& c = clipState[(size_t) layer];

    if (clip == c.playing)
    {
        if (c.queued >= 0)   // the playing pad with another clip waiting: the first press cancels the wait
        {
            if (cb.cancelSlot) cb.cancelSlot(layer);
            c.queued = -1;
            changed();
            return true;
        }
        const bool muted = cb.muteLayer && cb.muteLayer(layer);   // the existing MuteLayer path
        changed();   // the pad's flash follows the strip's mute, which this state does not own
        return muted;
    }
    if (clip == c.queued) return true;                          // already on its way
    if ((c.mask & (1u << clip)) == 0) return false;              // an empty pad does nothing
    if (! cb.launchClip || ! cb.launchClip(layer, clip)) return false;   // e.g. a full preset is staged

    // Stopped transport: the clip is applied inside launchClip and plays now. Otherwise it waits, and the
    // stage serial identifies this launch until its commit (or a later load that replaces it).
    if (cb.slotPending && cb.slotPending(layer))
    {
        c.queued       = clip;
        c.queuedSerial = cb.slotSerial ? cb.slotSerial(layer) : 0;
    }
    else
    {
        c.queued = -1;
        setPlaying(layer, clip);
    }
    changed();
    return true;
}

bool LiveState::launchScene(int clip)
{
    if (clip < 0 || clip >= mu_limits::kClipsPerLayer) return false;
    reconcileClips();   // first drop anything a full preset applied since the last settle has made stale

    const int layers = std::min(getLayerCount(), (int) mu_limits::kMaxLayers);

    // Everything slow first (a clip's files), so the batch window below holds only the staging calls.
    if (cb.preloadClip)
        for (int layer = 0; layer < layers; ++layer)
            if ((clipState[(size_t) layer].mask & (1u << clip)) != 0 && clip != clipState[(size_t) layer].playing)
                cb.preloadClip(layer, clip);

    // One staging batch: the audio thread flags the scene's layers at the same boundary or not at all.
    bool any = false;
    int  reached = 0;   // layers that launched the clip or already play it
    {
        struct Batch
        {
            explicit Batch(const Callbacks& c) : callbacks(c) { if (callbacks.beginStageBatch) callbacks.beginStageBatch(); }
            ~Batch() { if (callbacks.endStageBatch) callbacks.endStageBatch(); }
            const Callbacks& callbacks;
        } batch(cb);
        for (int layer = 0; layer < layers; ++layer)
        {
            auto& c = clipState[(size_t) layer];
            if ((c.mask & (1u << clip)) == 0) continue;   // a scene never stops a layer that has no such clip
            if (clip == c.playing)
            {
                if (c.queued >= 0 && cb.cancelSlot) { cb.cancelSlot(layer); c.queued = -1; }
                any = true;   // already there: the scene does not mute it
                ++reached;
                continue;
            }
            if (startClip(layer, clip))
            {
                any = true;
                ++reached;
                if (cb.markSceneSlot) cb.markSceneSlot(layer);   // a payload staged for a boundary is the scene's, not the layer's own
            }
        }
    }
    lastScene = { clip, reached, lastScene.serial + 1 };   // before changed(), so a listener reads this launch
    changed();
    return any;
}

bool LiveState::stepClip(int direction)
{
    if (! inRange(selectedLayer)) return false;
    reconcileClips();
    const auto& c = clipState[(size_t) selectedLayer];

    // Step from where the layer is heading: the queued clip if any, else the playing one. Wraps; skips empty clips.
    const int n      = mu_limits::kClipsPerLayer;
    const int origin = c.queued >= 0 ? c.queued : c.playing;
    for (int i = 1; i <= n; ++i)
    {
        const int candidate = origin < 0 ? (direction > 0 ? i - 1 : n - i)
                                         : ((origin + direction * i) % n + n) % n;
        if (candidate == origin) continue;   // no other stored clip
        if (c.mask & (1u << candidate)) return startClip(selectedLayer, candidate);
    }
    return false;
}

void LiveState::reconcileClips()
{
    bool dirty = false;

    // A full preset that landed (or was applied at once) replaced the layers, so the pads restart from what
    // they saved. Staging one, or cancelling a staged one from anywhere, moves no count and changes nothing.
    if (cb.fullCommitCount && cb.fullCommitCount() != seenFullCommits)
    {
        dirty |= resetClips();
        syncFullCommits();
    }

    // A queued clip: still staged and still ours = waiting; gone with our serial = it landed; otherwise
    // another load or a cancel replaced it.
    for (int layer = 0; layer < mu_limits::kMaxLayers; ++layer)
    {
        auto& c = clipState[(size_t) layer];
        if (c.queued < 0) continue;
        const bool pending = cb.slotPending && cb.slotPending(layer);
        const bool ours    = cb.slotSerial && cb.slotSerial(layer) == c.queuedSerial;
        if (pending && ours) continue;
        if (! pending && ours) setPlaying(layer, c.queued);
        c.queued = -1;
        dirty = true;
    }
    if (dirty) changed();
}

void LiveState::clipStored(int layer, int clip)
{
    if (! inRange(layer) || clip < 0 || clip >= mu_limits::kClipsPerLayer) return;
    reconcileClips();   // settle a landed or replaced launch before judging what is queued
    auto& c = clipState[(size_t) layer];
    if (c.queued == clip)   // the staged launch would land the clip's old contents
    {
        if (cb.cancelSlot) cb.cancelSlot(layer);
        c.queued = -1;
    }
    setPlaying(layer, clip);   // what was just stored is what the layer plays
    changed();
}

void LiveState::setClipMask(int layer, uint8_t mask)
{
    if (! inRange(layer) || clipState[(size_t) layer].mask == mask) return;
    clipState[(size_t) layer].mask = mask;
    changed();
}

void LiveState::setClipName(int layer, int clip, const juce::String& name)
{
    if (! inRange(layer) || clip < 0 || clip >= mu_limits::kClipsPerLayer) return;
    auto& slot = clipState[(size_t) layer].names[(size_t) clip];
    if (slot == name) return;
    slot = name;
    changed();
}

void LiveState::clearLayerClips(int layer)
{
    if (! inRange(layer)) return;
    auto& c = clipState[(size_t) layer];
    if (c.playing < 0 && c.queued < 0) return;
    c.playing = c.queued = -1;
    changed();
}

void LiveState::clipCleared(int layer, int clip)
{
    if (! inRange(layer)) return;
    auto& c = clipState[(size_t) layer];
    bool dirty = false;
    if (c.playing == clip) { c.playing = -1; dirty = true; }
    if (c.queued == clip)   // the staged launch would still land a clip that no longer exists
    {
        if (cb.cancelSlot) cb.cancelSlot(layer);
        c.queued = -1;
        dirty = true;
    }
    if (dirty) changed();
}

void LiveState::remapLayers(const std::function<int(int)>& oldToNew)
{
    std::array<ClipState, (size_t) mu_limits::kMaxLayers> moved {};
    for (int i = 0; i < mu_limits::kMaxLayers; ++i)
    {
        const int j = oldToNew(i);
        if (! inRange(j)) continue;   // the layer is gone
        const auto& from = clipState[(size_t) i];
        auto& to = moved[(size_t) j];
        to.playing = from.playing;
        to.mask    = from.mask;
        to.names   = from.names;
        if (j == i) { to.queued = from.queued; to.queuedSerial = from.queuedSerial; }   // a layer that did not move keeps its wait
    }

    bool differs = false;
    for (size_t i = 0; i < moved.size(); ++i)
        differs |= moved[i].playing != clipState[i].playing || moved[i].queued != clipState[i].queued
                || moved[i].mask != clipState[i].mask || moved[i].names != clipState[i].names;
    clipState = moved;
    if (differs) changed();
}

void LiveState::saveTo(juce::ValueTree& state) const
{
    if (performanceFile == juce::File())
    {
        state.removeProperty(kPerformancePath, nullptr);
        state.removeProperty(kPerformanceItem, nullptr);
        state.removeProperty(kPerformanceMode, nullptr);
        return;
    }
    state.setProperty(kPerformancePath, performanceFile.getFullPathName(), nullptr);
    state.setProperty(kPerformanceItem, currentItem, nullptr);
    state.setProperty(kPerformanceMode, performanceMode, nullptr);
}

bool LiveState::modeSaved(const juce::ValueTree& state)
{
    return state.isValid() && (bool) state.getProperty(kPerformanceMode, false);
}

void LiveState::restoreFrom(const juce::ValueTree& state)
{
    cancelQueuedItem();
    performanceMode = false;
    currentItem = queuedItem = -1;
    // A launch staged before the restore would land on the restored layers unlit.
    if (cb.cancelSlot)
        for (int layer = 0; layer < mu_limits::kMaxLayers; ++layer) cb.cancelSlot(layer);
    resetClips();   // a restored session replaced the layers: the pads show the clips it saved as playing
    syncFullCommits();
    performance = {};
    performanceFile = juce::File();

    const juce::File file(state.getProperty(kPerformancePath).toString());
    if (file != juce::File())
    {
        mu_pp::PerformanceFile loaded;
        if (mu_pp::PerformanceFile::load(file, loaded) == mu_pp::PerformanceFile::LoadResult::Ok)
        {
            performance     = std::move(loaded);
            performanceFile = file;
            const int item  = (int) state.getProperty(kPerformanceItem, -1);
            currentItem     = (item >= 0 && item < mu_pp::PerformanceFile::kMaxItems) ? item : -1;
            // The mode comes back only if this build still supports it and the list is usable.
            if ((bool) state.getProperty(kPerformanceMode, false)
                && cb.supportsPerformanceMode && cb.supportsPerformanceMode() && performance.count() > 0)
                performanceMode = true;
        }
    }
    changed();
}

} // namespace mu_core
