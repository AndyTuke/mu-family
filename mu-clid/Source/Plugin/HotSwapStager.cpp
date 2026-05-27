#include "HotSwapStager.h"
#include "HotSwapBoundary.h"   // mu_clid::hotswap:: pure loop-boundary predicates (#653 logic)
#include "PluginProcessor.h"
#include "Persistence/ScopedApvtsLoading.h"
#include "MuLimits.h"
#include "Sequencer/SequencerEngine.h"

//==============================================================================
void HotSwapStager::cancelPendingIfAny(int rhythmIndex)
{
    auto& sw = pendingSwaps[(size_t)rhythmIndex];
    sw.isReady.store(false, std::memory_order_release);
    sw.boundaryReached.store(false, std::memory_order_relaxed);
    sw.pendingVoice.reset();
}

void HotSwapStager::stage(int rhythmIndex, Rhythm&& rhythm,
                          std::unique_ptr<VoiceEngine>&& voice,
                          const juce::String& samplePath)
{
    auto& sw = pendingSwaps[(size_t)rhythmIndex];
    sw.pendingRhythm     = std::move(rhythm);
    sw.pendingSamplePath = samplePath;
    sw.pendingVoice      = std::move(voice);
    sw.boundaryReached.store(false, std::memory_order_relaxed);
    sw.isReady.store(true, std::memory_order_release);
}

void HotSwapStager::cancelStagedSwap(int rhythmIndex)
{
    if (rhythmIndex < 0 || rhythmIndex >= SequencerEngine::MaxRhythms) return;
    cancelPendingIfAny(rhythmIndex);
}

bool HotSwapStager::hasPendingSwap(int rhythmIndex) const
{
    if (rhythmIndex < 0 || rhythmIndex >= SequencerEngine::MaxRhythms) return false;
    return pendingSwaps[(size_t)rhythmIndex].isReady.load(std::memory_order_acquire);
}

void HotSwapStager::stageFullPreset(PreparedFullPreset&& prepared)
{
    // A full preset replaces every slot — drop any per-rhythm swaps still queued
    // so they don't commit onto slots the preset is about to overwrite.
    for (int r = 0; r < kMaxRhythms; ++r)
        cancelPendingIfAny(r);

    pendingPreset = std::move(prepared);
    presetBoundaryReached.store(false, std::memory_order_relaxed);
    presetReady.store(true, std::memory_order_release);
}

bool HotSwapStager::hasPendingFullPreset() const
{
    return presetReady.load(std::memory_order_acquire);
}

//==============================================================================
bool HotSwapStager::checkBoundaries(int numRhythms, bool masterLoopWrapped,
                                    int rhythmLoopWrapMask)
{
    const int mode = proc_.swapModeAtomic.load(std::memory_order_relaxed);
    bool needAsync = false;
    for (int r = 0; r < numRhythms; ++r)
    {
        auto& sw = pendingSwaps[(size_t)r];
        if (sw.isReady.load(std::memory_order_acquire)
            && !sw.boundaryReached.load(std::memory_order_relaxed))
        {
            const bool wrap = mu_clid::hotswap::perRhythmBoundaryReached(mode, r, masterLoopWrapped,
                                                                   rhythmLoopWrapMask);
            if (wrap)
            {
                sw.boundaryReached.store(true, std::memory_order_release);
                needAsync = true;
            }
        }
    }

    // Full-preset swaps wait for the MASTER loop point when a master loop is
    // defined (a preset spans every rhythm, so the master loop is the musical
    // boundary). When free-running (mstrLoop=0, the default), there is no master
    // loop to wrap, so fall back to rhythm 0's loop — otherwise the swap would
    // hang forever waiting for a boundary that never comes.
    if (presetReady.load(std::memory_order_acquire)
        && !presetBoundaryReached.load(std::memory_order_relaxed))
    {
        const bool hasMasterLoop = proc_.sequencer.getMasterLoopSteps() > 0;
        const bool wrap = mu_clid::hotswap::fullPresetBoundaryReached(hasMasterLoop, masterLoopWrapped,
                                                                rhythmLoopWrapMask);
        if (wrap)
        {
            presetBoundaryReached.store(true, std::memory_order_release);
            needAsync = true;
        }
    }

    return needAsync;
}

//==============================================================================
void HotSwapStager::processSwaps()
{
    // Drain retired-engine cleanup flags. Audio thread store-releases the per-slot
    // flag once VoiceEngine::isFullyDrained() returns true; the move-out below
    // transfers ownership to a local that destructs off the RT thread.
    // suspendProcessing fences the audio thread so it cannot be mid-process() on
    // the engine when the unique_ptr is yanked.
    // Empty-fast-path: skip the suspend cost when no engines need cleanup.
    bool anyCleanupNeeded = false;
    for (auto& slotArr : proc_.retiredReadyForCleanup)
    {
        for (auto& flag : slotArr)
            if (flag.load(std::memory_order_acquire)) { anyCleanupNeeded = true; break; }
        if (anyCleanupNeeded) break;
    }
    if (anyCleanupNeeded)
    {
        std::array<std::unique_ptr<VoiceEngine>,
                   SequencerEngine::MaxRhythms * mu_limits::kMaxRetiredVoiceEngines> orphans;
        int orphanCount = 0;
        proc_.suspendProcessing(true);
        for (int r = 0; r < (int)proc_.retiredReadyForCleanup.size(); ++r)
        {
            for (int i = 0; i < mu_limits::kMaxRetiredVoiceEngines; ++i)
            {
                if (!proc_.retiredReadyForCleanup[(size_t)r][(size_t)i]
                        .load(std::memory_order_acquire))
                    continue;
                orphans[(size_t)orphanCount++] =
                    std::move(proc_.retiredVoiceEngines[(size_t)r][(size_t)i]);
                proc_.retiredReadyForCleanup[(size_t)r][(size_t)i]
                    .store(false, std::memory_order_release);
            }
        }
        proc_.suspendProcessing(false);
        // `orphans` destructs at scope exit — fully off the audio thread.
    }

    // Two-pass to suspend audio ONCE per call: collect all ready slots, then swap
    // them under one suspend, then do post-commit APVTS push + UI callback outside it.
    const int n = proc_.numActiveRhythms.load(std::memory_order_acquire);
    std::array<int, SequencerEngine::MaxRhythms> readyRhythms {};
    int readyCount = 0;
    for (int r = 0; r < n; ++r)
    {
        auto& sw = pendingSwaps[(size_t)r];
        if (!sw.boundaryReached.load(std::memory_order_acquire)) continue;
        // isReady may have been cleared by cancelStagedSwap between the audio thread
        // setting boundaryReached and this handler running — skip if so.
        if (!sw.isReady.load(std::memory_order_relaxed))
        {
            sw.boundaryReached.store(false, std::memory_order_relaxed);
            continue;
        }
        readyRhythms[(size_t)readyCount++] = r;
    }

    if (readyCount > 0)
    {
        proc_.suspendProcessing(true);
        for (int idx = 0; idx < readyCount; ++idx)
        {
            const int r = readyRhythms[(size_t)idx];
            auto& sw = pendingSwaps[(size_t)r];

            // Stage 34 Step 3: retire-then-swap. Old engine continues rendering its
            // in-flight sample / amp envelope tail from a retired slot.
            auto oldEngine = std::move(proc_.voiceEngines[(size_t)r]);
            proc_.voiceEngines[(size_t)r] = std::move(sw.pendingVoice);

            if (oldEngine)
            {
                // Must happen BEFORE placement so the engine is already in its
                // released / filter-reset state when the next audio block picks it up.
                oldEngine->markRetired();

                bool placed = false;
                for (auto& slot : proc_.retiredVoiceEngines[(size_t)r])
                {
                    if (!slot)
                    {
                        slot = std::move(oldEngine);
                        placed = true;
                        break;
                    }
                }
                if (!placed)
                {
                    // All retired slots full — spam-swap back-pressure: force-cut slot 0.
                    proc_.retiredVoiceEngines[(size_t)r][0] = std::move(oldEngine);
                    proc_.retiredReadyForCleanup[(size_t)r][0]
                        .store(false, std::memory_order_release);
                }
            }

            proc_.sequencer.getRhythm(r) = std::move(sw.pendingRhythm);
            proc_.loadedSamplePaths.set(r, sw.pendingSamplePath);
            proc_.sequencer.updatePattern(r);
            proc_.sequencer.resetStepTrackingForSwap(r);
            sw.isReady.store(false, std::memory_order_relaxed);
            sw.boundaryReached.store(false, std::memory_order_relaxed);
        }
        proc_.suspendProcessing(false);

        // Post-commit: APVTS push + editor refresh. One guard for the whole batch
        // so every panel's parameterChanged sees apvtsLoading=true.
        mu_core::ScopedApvtsLoading guard(proc_.apvtsLoading);
        for (int idx = 0; idx < readyCount; ++idx)
        {
            const int r = readyRhythms[(size_t)idx];
            proc_.pushRhythmToAPVTS(r);
            if (proc_.onRhythmHotSwapCommitted)
                proc_.onRhythmHotSwapCommitted(r);
        }
    }

    // Commit a staged full-preset swap once its loop boundary has been reached.
    // Runs after the per-rhythm block; stageFullPreset already cancelled any
    // per-rhythm swaps, so in practice only one path fires per call. All the heavy
    // lifting (parse, voice build, sample load) happened at stage time, so the
    // commit is just fast in-memory moves under suspend + an APVTS finalize.
    if (presetBoundaryReached.load(std::memory_order_acquire))
    {
        presetBoundaryReached.store(false, std::memory_order_relaxed);
        presetReady.store(false, std::memory_order_relaxed);
        proc_.presetIO.commitStagedFullPreset(pendingPreset);
        pendingPreset = PreparedFullPreset{};  // release the pre-built voices + tree
        if (proc_.onPresetSwapCommitted)
            proc_.onPresetSwapCommitted();
    }
}
