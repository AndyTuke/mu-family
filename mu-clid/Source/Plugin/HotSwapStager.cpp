#include "HotSwapStager.h"
#include "PluginProcessor.h"
#include "Persistence/ScopedApvtsLoading.h"
#include "MuLimits.h"
#include "Sequencer/GeneratorAliases.h"

namespace mu_clid {

//==============================================================================
void HotSwapStager::cancelPendingIfAny(int rhythmIndex)
{
    stager.cancel(rhythmIndex);
}

void HotSwapStager::stage(int rhythmIndex, Rhythm&& rhythm,
                          std::unique_ptr<VoiceEngine>&& voice,
                          const juce::String& samplePath)
{
    stager.stage(rhythmIndex, PendingRhythm { std::move(rhythm), samplePath, std::move(voice) });
}

void HotSwapStager::cancelStagedSwap(int rhythmIndex)
{
    if (rhythmIndex < 0 || rhythmIndex >= mu_limits::kMaxLayers) return;
    cancelPendingIfAny(rhythmIndex);
}

bool HotSwapStager::hasPendingSwap(int rhythmIndex) const
{
    if (rhythmIndex < 0 || rhythmIndex >= mu_limits::kMaxLayers) return false;
    return stager.hasPending(rhythmIndex);
}

void HotSwapStager::stageFullPreset(PreparedFullPreset&& prepared)
{
    // A full preset replaces every slot — the stager drops any per-rhythm swaps still queued
    // so they don't commit onto slots the preset is about to overwrite.
    stager.stageFull(std::move(prepared));
}

bool HotSwapStager::hasPendingFullPreset() const
{
    return stager.hasFullPending();
}

void HotSwapStager::cancelStagedFullPreset()
{
    stager.cancelFull();
}

bool HotSwapStager::takeStagedFullPreset(PreparedFullPreset& out)
{
    return stager.consumeFullNow([&out](PreparedFullPreset& staged) { out = std::move(staged); });
}

void HotSwapStager::cancelAllStaged()
{
    // Drop every per-rhythm swap, then the full preset: a full preset applied at once overwrites every slot.
    for (int r = 0; r < mu_limits::kMaxLayers; ++r) stager.cancel(r);
    stager.cancelFull();
}

//==============================================================================
bool HotSwapStager::checkBoundaries(int numRhythms, bool masterLoopWrapped,
                                    int rhythmLoopWrapMask)
{
    // Per-rhythm swaps: the master loop point, or each rhythm's own loop (the `mstr_swap` timing). With no
    // master loop set there is no master wrap, so the own loop is used whatever the timing says.
    const auto timing = proc.swapTiming();
    const bool hasMasterLoop = proc.stepClock.getMasterLoopSteps() > 0;
    const bool onMasterLoop  = mu_hotswap::usesMasterLoop(timing, hasMasterLoop);
    const uint32_t epoch = proc.swapFrameEpoch();
    bool needAsync = false;
    for (int r = 0; r < numRhythms; ++r)
    {
        // Read the token first so the flag lands only on the swap this boundary was judged for.
        const auto token = stager.pendingToken(r);
        const bool atBoundary = mu_hotswap::layerBoundaryReached(timing, hasMasterLoop, r, masterLoopWrapped,
                                                                 rhythmLoopWrapMask, /*barWrapped*/ false);
        // A non-zero token is a ready, not yet flagged swap. Record where its boundary fell before
        // flagging: the flag's release publishes it to the committing thread, which starts the new
        // pattern's step 0 there (the master loop point is step 0 of the loop; a rhythm's own loop
        // is wherever that rhythm last wrapped).
        if (token != 0 && atBoundary)
        {
            flaggedOrigin[(size_t) r].store(onMasterLoop ? 0 : proc.swapBoundaryEff(r), std::memory_order_relaxed);
            flaggedEpoch [(size_t) r].store(epoch, std::memory_order_relaxed);
        }
        needAsync |= stager.flagIfReady(r, token, atBoundary);
    }

    // Full-preset swaps wait for the MASTER loop point when a master loop is
    // defined (a preset spans every rhythm, so the master loop is the musical
    // boundary). When free-running (mstrLoop=0, the default), there is no master
    // loop to wrap, so fall back to rhythm 0's loop — otherwise the swap would
    // hang forever waiting for a boundary that never comes.
    const auto fullToken = stager.fullToken();
    const bool fullAtBoundary = mu_hotswap::fullPresetBoundaryReached(timing, hasMasterLoop, masterLoopWrapped,
                                                                      rhythmLoopWrapMask, /*barWrapped*/ false);
    if (fullToken != 0 && fullAtBoundary)
    {
        fullFlaggedOrigin.store(hasMasterLoop ? 0 : proc.swapBoundaryEff(0), std::memory_order_relaxed);
        fullFlaggedEpoch .store(epoch, std::memory_order_relaxed);
    }
    needAsync |= stager.flagFullIfReady(fullToken, fullAtBoundary);
    return needAsync;
}

//==============================================================================
void HotSwapStager::installRhythm(int r, Rhythm&& rhythm, std::unique_ptr<VoiceEngine>&& voice,
                                  const juce::String& samplePath, int originEff, uint32_t frameEpoch)
{
    // The incoming engine is not yet visible to the audio thread, so its noise seed can be set here.
    if (voice) voice->insertProc.setSlotIndex(r);

    // Retire-then-swap: the old engine continues rendering its in-flight tail from the shared RetireBin (its
    // envelopes are released at its first tail block). A full row (spam-swap back-pressure) cuts its entry 0,
    // whose engine is freed later, outside the lock.
    auto& layer = proc.sampleRhythm(r);
    auto oldEngine = std::move(layer.voice);
    layer.voice = std::move(voice);
    proc.retireEngine(r, std::move(oldEngine));

    // Swap rather than assign: the outgoing rhythm ends up in the caller's object, which the caller
    // destroys after releasing its locks. The clip bank stays with the slot (SlotBound); the playing
    // clip is the slot's too, so a clip launch keeps it and a non-clip load clears it before it gets here.
    const int activeClip = proc.getRhythm(r).activeClip;
    std::swap(proc.getRhythm(r), rhythm);
    proc.getRhythm(r).activeClip = activeClip;
    proc.samples.setPath(r, samplePath);
    // Rebuilds the pattern and hands the audio thread its start position in one step.
    proc.sampleRhythm(r).cursor.requestSwapStart(proc.sampleRhythm(r), PluginProcessor::kStepCap, originEff, frameEpoch);
}

void HotSwapStager::processSwaps()
{
    // (The drained tails are freed by the RetireBin's service pass, which runDeferredWork runs before this one.)

    // Two-pass to suspend audio ONCE per call: collect all ready slots, then swap
    // them under one suspend, then do post-commit APVTS push + UI callback outside it.
    const int n = proc.numLayers.load(std::memory_order_acquire);
    std::array<int, mu_limits::kMaxLayers> readyRhythms {};
    int readyCount = 0;
    for (int r = 0; r < n; ++r)
        if (stager.isFlagged(r))   // (a swap cancelled after it was flagged is skipped)
            readyRhythms[(size_t)readyCount++] = r;

    if (readyCount > 0)
    {
        // Which slots consume actually applied: a swap cancelled between the scan and here is skipped.
        std::array<bool, mu_limits::kMaxLayers> applied {};
        // Everything the commit empties lives out here, so its destructors run after the lock and the suspend
        // are released, not inside them (a retired engine goes to the RetireBin, which frees it later).
        std::array<PendingRhythm, mu_limits::kMaxLayers>            spent;
        proc.suspendProcessing(true);
        {
            // suspendProcessing alone does not stop an in-flight callback in hosts that ignore it
            // (CLAP); layersLock is what the audio thread try-locks, so the engine and sample moves
            // below cannot overlap a block. Released before the APVTS push.
            const juce::ScopedLock sl(proc.layersLock);
            for (int idx = 0; idx < readyCount; ++idx)
            {
                const int r = readyRhythms[(size_t)idx];
                applied[(size_t)r] = stager.consume(r, [&](PendingRhythm& sw)
                {
                    spent[(size_t) r] = std::move(sw);
                    auto& in = spent[(size_t) r];
                    installRhythm(r, std::move(in.rhythm), std::move(in.voice), in.samplePath,
                                  flaggedOrigin[(size_t) r].load(std::memory_order_relaxed),
                                  flaggedEpoch [(size_t) r].load(std::memory_order_relaxed));
                });
            }
        }
        proc.suspendProcessing(false);

        // Post-commit: APVTS push + editor refresh. One guard for the whole batch
        // so every panel's parameterChanged sees apvtsLoading=true.
        mu_core::ScopedApvtsLoading guard(proc.apvtsLoading);
        for (int idx = 0; idx < readyCount; ++idx)
        {
            const int r = readyRhythms[(size_t)idx];
            if (! applied[(size_t)r]) continue;
            proc.pushRhythmToApvts(r);
            if (proc.onLayerPresetCommitted)
                proc.onLayerPresetCommitted(r);
        }
    }

    // Commit a staged full-preset swap once its loop boundary has been reached.
    // Runs after the per-rhythm block; stageFullPreset already cancelled any
    // per-rhythm swaps, so in practice only one path fires per call. All the heavy
    // lifting (parse, voice build, sample load) happened at stage time, so the
    // commit is just fast in-memory moves under suspend + an APVTS finalize.
    // (consumeFull releases the pre-built voices + tree afterwards.)
    const bool committed = stager.consumeFull([this](PreparedFullPreset& p)
    {
        proc.commitStagedFullPreset(p, fullFlaggedOrigin.load(std::memory_order_relaxed),
                                             fullFlaggedEpoch.load(std::memory_order_relaxed));
    });
    if (committed)
    {
        proc.getLiveState().fullPresetCommitted();   // the queued performance item becomes current
        if (proc.onPresetSwapCommitted) proc.onPresetSwapCommitted();
    }
}

} // namespace mu_clid
