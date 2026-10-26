// Clip auto-save: the edits made to a layer whose clip is lit are written back to that clip. The detection is a
// message-thread poll and compare over captureLayerPresetTree, the same capture a clip is made from, so it needs no
// product code and nothing on the audio thread. See docs/design-plugin-family.md, "Clip bank", item 10.

#include "Plugin/ProcessorBase.h"

namespace
{
// True on the message thread, and where no message manager exists (a headless test) so a flush still runs there.
bool onMessageThreadOrHeadless()
{
    auto* mm = juce::MessageManager::getInstanceWithoutCreating();
    return mm == nullptr || mm->isThisTheMessageThread();
}
}

void ProcessorBase::setClipAutoSave(bool on)
{
    clipAutoSaveOn = on;
    if (appSettings != nullptr) { appSettings->setValue("clipAutoSave", on); appSettings->saveIfNeeded(); }
    if (! on) dropClipWatches();   // switched back on later, the layers re-adopt a baseline from what they are then
}

// A full preset replaced the layers: every tracker belongs to layers that no longer exist.
void ProcessorBase::dropClipWatchesOnFullCommit()
{
    if (const uint32_t commits = fullCommitCount(); commits != clipWatchFullCommits)
    {
        clipWatchFullCommits = commits;
        dropClipWatches();
    }
}

bool ProcessorBase::clipAutoSaveActive() const
{
    return clipAutoSaveOn && ! isNonRealtime() && ! liveState.isPerformanceMode() && ! hasPendingFullPreset();
}

void ProcessorBase::flushClipAutoSaveForStateCapture()
{
    if (onMessageThreadOrHeadless())
        flushClipAutoSave();
}

void ProcessorBase::flushClipAutoSave(int slot)
{
    mu_hotswap::assertMessageThread();
    dropClipWatchesOnFullCommit();
    if (! clipAutoSaveActive()) return;
    if (slot >= 0)
    {
        if (slot < mu_limits::kMaxLayers) visitClipWatch(slot, false);
        return;
    }
    for (int s = 0; s < mu_limits::kMaxLayers; ++s)
        visitClipWatch(s, false);
}

bool ProcessorBase::writeWatchedClip(int slot, int clip, const juce::ValueTree& capture)
{
    auto* layer = getLayer(slot);
    if (layer == nullptr) return false;

    // The clip keeps its name; store deep-copies, so the tracker's baseline stays independent of the bank.
    layer->clips->store(clip, capture, layer->clips->getName(clip));
    publishClipMasks();
    auto& w = clipWatch[(size_t) slot];
    w.baseline  = capture;
    w.candidate = juce::ValueTree();
    return true;
}

// One look at layer `slot`. `settle` true is the poll's step (a differing capture waits one more step to be written,
// and a slow capture slows that layer's visits); false is a flush (a differing capture is written at once).
bool ProcessorBase::visitClipWatch(int slot, bool settle)
{
    auto& w = clipWatch[(size_t) slot];
    auto* layer = getLayer(slot);
    const int lit = liveState.getPlayingClip(slot);
    if (layer == nullptr || lit < 0 || ! layer->clips->isOccupied(lit))
    {
        if (! w.expectSwap) w.reset();   // nothing lit: nothing tracked (a staged launch is kept until it lands or is dropped)
        return false;
    }
    if (w.suspended) return false;       // a paste replaced this clip: its relaunch has not landed yet

    // A pending swap is either our own clip launch (the live layer is still the lit clip until it lands, so its edits
    // keep being saved) or someone else's, which makes live differ from the baseline until it commits.
    if (hasPendingSwap(slot) && liveState.getQueuedClip(slot) < 0 && ! w.expectSwap)
    {
        w.reset();
        return false;
    }

    // A different clip is lit than the tracker was adopted for: start over for the new one.
    if (w.clip != lit)
    {
        const bool     expect = w.expectSwap;
        const uint32_t serial = w.serial;
        w.reset();
        w.expectSwap = expect;
        w.serial     = serial;
        w.clip       = lit;
    }

    const double started = juce::Time::getMillisecondCounterHiRes();
    auto capture = captureLayerPresetTree(slot);
    const double took = juce::Time::getMillisecondCounterHiRes() - started;
    if (settle && took > kClipCaptureBudgetMs)
        w.period = juce::jmin(w.period * 2, 16);   // a UI-hitch limit: visit this layer half as often
    if (! capture.isValid()) return false;

    if (! w.baseline.isValid())
    {
        if (settle) w.baseline = capture;   // nothing known to compare with yet: adopt, never write
        return false;
    }
    if (capture.isEquivalentTo(w.baseline))
    {
        w.candidate = juce::ValueTree();
        return false;
    }
    if (! settle) return writeWatchedClip(slot, lit, capture);

    // Debounce: written only when the layer has held the same differing state for a whole step.
    if (w.candidate.isValid() && capture.isEquivalentTo(w.candidate))
        return writeWatchedClip(slot, lit, capture);
    w.candidate = capture;
    return false;
}

// A staged clip launch that has landed (or been applied at once) makes the layer the new clip: adopt a baseline from
// the live layer, never write. A launch that was superseded or cancelled drops the tracker.
void ProcessorBase::resolveClipLaunches()
{
    for (int slot = 0; slot < mu_limits::kMaxLayers; ++slot)
    {
        auto& w = clipWatch[(size_t) slot];
        if (! w.expectSwap) continue;
        if (getLayer(slot) == nullptr || slotStageSerial(slot) != w.serial)
        {
            w.reset();
            continue;
        }
        if (hasPendingSwap(slot)) continue;

        liveState.reconcile();   // the lit clip moves with the landing
        const int lit = liveState.getPlayingClip(slot);
        w.reset();
        if (lit < 0 || ! clipAutoSaveActive()) continue;
        auto capture = captureLayerPresetTree(slot);
        if (! capture.isValid()) continue;
        w.clip     = lit;
        w.baseline = std::move(capture);
    }
}

bool ProcessorBase::stepClipAutoSave()
{
    mu_hotswap::assertMessageThread();

    dropClipWatchesOnFullCommit();
    if (! clipAutoSaveActive())
    {
        dropClipWatches();
        return false;
    }
    resolveClipLaunches();

    // One layer per step, round robin over the layers that have a lit clip.
    for (int n = 0; n < mu_limits::kMaxLayers; ++n)
    {
        const int slot = (clipWatchCursor + n) % mu_limits::kMaxLayers;
        auto& w = clipWatch[(size_t) slot];
        if (liveState.getPlayingClip(slot) < 0)
        {
            if (! w.expectSwap) w.reset();
            continue;
        }
        if (w.wait > 0)   // a slow layer is only visited every period-th time it comes round
        {
            --w.wait;
            continue;
        }
        clipWatchCursor = (slot + 1) % mu_limits::kMaxLayers;
        w.wait = w.period - 1;
        return visitClipWatch(slot, true);
    }
    return false;
}

void ProcessorBase::pollClipAutoSave()
{
    // Cheap exit for the common case: no lit clip and no launch waiting to land.
    bool any = false;
    for (int s = 0; s < mu_limits::kMaxLayers && ! any; ++s)
        any = liveState.getPlayingClip(s) >= 0 || clipWatch[(size_t) s].expectSwap;
    if (! any)
    {
        dropClipWatches();
        return;
    }

    if (clipAutoSaveActive())
        resolveClipLaunches();   // every tick, so a landed launch is adopted before the user can edit the new clip

    const double now = juce::Time::getMillisecondCounterHiRes();
    if (now < nextClipStepMs) return;
    nextClipStepMs = now + kClipAutoSaveStepMs;
    stepClipAutoSave();
}
