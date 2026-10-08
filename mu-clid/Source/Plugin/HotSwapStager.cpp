#include "HotSwapStager.h"
#include "HotSwapBoundary.h"   // mu_clid::hotswap:: pure loop-boundary predicates
#include "PluginProcessor.h"
#include "Persistence/ScopedApvtsLoading.h"
#include "MuLimits.h"
#include "Sequencer/SequencerEngine.h"

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
    if (rhythmIndex < 0 || rhythmIndex >= SequencerEngine::MaxRhythms) return;
    cancelPendingIfAny(rhythmIndex);
}

bool HotSwapStager::hasPendingSwap(int rhythmIndex) const
{
    if (rhythmIndex < 0 || rhythmIndex >= SequencerEngine::MaxRhythms) return false;
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

//==============================================================================
bool HotSwapStager::checkBoundaries(int numRhythms, bool masterLoopWrapped,
                                    int rhythmLoopWrapMask)
{
    // Per-rhythm swaps: the master loop point, or each rhythm's own loop (Hot-swap timing).
    const int mode = proc_.swapModeAtomic.load(std::memory_order_relaxed);
    bool needAsync = false;
    for (int r = 0; r < numRhythms; ++r)
        needAsync |= stager.flagIfReady(r, mu_clid::hotswap::perRhythmBoundaryReached(mode, r, masterLoopWrapped,
                                                                                    rhythmLoopWrapMask));

    // Full-preset swaps wait for the MASTER loop point when a master loop is
    // defined (a preset spans every rhythm, so the master loop is the musical
    // boundary). When free-running (mstrLoop=0, the default), there is no master
    // loop to wrap, so fall back to rhythm 0's loop — otherwise the swap would
    // hang forever waiting for a boundary that never comes.
    const bool hasMasterLoop = proc_.sequencer.getMasterLoopSteps() > 0;
    needAsync |= stager.flagFullIfReady(mu_clid::hotswap::fullPresetBoundaryReached(hasMasterLoop, masterLoopWrapped,
                                                                                   rhythmLoopWrapMask));
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
        if (stager.isFlagged(r))   // (a swap cancelled after it was flagged is skipped)
            readyRhythms[(size_t)readyCount++] = r;

    if (readyCount > 0)
    {
        proc_.suspendProcessing(true);
        for (int idx = 0; idx < readyCount; ++idx)
        {
            const int r = readyRhythms[(size_t)idx];
            stager.consume(r, [&](PendingRhythm& sw)
            {
                // Stage 34 Step 3: retire-then-swap. Old engine continues rendering its
                // in-flight sample / amp envelope tail from a retired slot.
                auto oldEngine = std::move(proc_.voiceEngines[(size_t)r]);
                proc_.voiceEngines[(size_t)r] = std::move(sw.voice);

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

                proc_.sequencer.getRhythm(r) = std::move(sw.rhythm);
                proc_.samples.setPath(r, sw.samplePath);
                proc_.sequencer.updatePattern(r);
                proc_.sequencer.resetStepTrackingForSwap(r);
            });
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
    // (consumeFull releases the pre-built voices + tree afterwards.)
    if (stager.consumeFull([this](PreparedFullPreset& p) { proc_.presetIO.commitStagedFullPreset(p); })
        && proc_.onPresetSwapCommitted)
        proc_.onPresetSwapCommitted();
}
