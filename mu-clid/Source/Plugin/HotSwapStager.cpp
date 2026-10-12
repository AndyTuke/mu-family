#include "HotSwapStager.h"
#include "HotSwapBoundary.h"   // mu_clid::hotswap:: pure loop-boundary predicates
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
    if (rhythmIndex < 0 || rhythmIndex >= RhythmSequencer::MaxRhythms) return;
    cancelPendingIfAny(rhythmIndex);
}

bool HotSwapStager::hasPendingSwap(int rhythmIndex) const
{
    if (rhythmIndex < 0 || rhythmIndex >= RhythmSequencer::MaxRhythms) return false;
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
    // Per-rhythm swaps: the master loop point, or each rhythm's own loop (Hot-swap timing). With no
    // master loop set there is no master wrap, so the own loop is used whatever the timing says.
    const int mode = proc.swapModeAtomic.load(std::memory_order_relaxed);
    const bool hasMasterLoop = proc.sequencer.getMasterLoopSteps() > 0;
    const bool onMasterLoop  = mu_clid::hotswap::perRhythmUsesMasterLoop(mode, hasMasterLoop);
    const uint32_t epoch = proc.sequencer.getFrameEpoch();
    bool needAsync = false;
    for (int r = 0; r < numRhythms; ++r)
    {
        // Read the token first so the flag lands only on the swap this boundary was judged for.
        const auto token = stager.pendingToken(r);
        const bool atBoundary = mu_clid::hotswap::perRhythmBoundaryReached(mode, hasMasterLoop, r,
                                                                          masterLoopWrapped, rhythmLoopWrapMask);
        // A non-zero token is a ready, not yet flagged swap. Record where its boundary fell before
        // flagging: the flag's release publishes it to the committing thread, which starts the new
        // pattern's step 0 there (the master loop point is step 0 of the loop; a rhythm's own loop
        // is wherever that rhythm last wrapped).
        if (token != 0 && atBoundary)
        {
            flaggedOrigin[(size_t) r].store(onMasterLoop ? 0 : proc.sequencer.getBoundaryEff(r), std::memory_order_relaxed);
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
    const bool fullAtBoundary = mu_clid::hotswap::fullPresetBoundaryReached(hasMasterLoop, masterLoopWrapped,
                                                                           rhythmLoopWrapMask);
    if (fullToken != 0 && fullAtBoundary)
    {
        fullFlaggedOrigin.store(hasMasterLoop ? 0 : proc.sequencer.getBoundaryEff(0), std::memory_order_relaxed);
        fullFlaggedEpoch .store(epoch, std::memory_order_relaxed);
    }
    needAsync |= stager.flagFullIfReady(fullToken, fullAtBoundary);
    return needAsync;
}

//==============================================================================
void HotSwapStager::installRhythm(int r, Rhythm&& rhythm, std::unique_ptr<VoiceEngine>&& voice,
                                  const juce::String& samplePath, int originEff, uint32_t frameEpoch,
                                  std::unique_ptr<VoiceEngine>* cutVictim)
{
    // The incoming engine is not yet visible to the audio thread, so its noise seed can be set here.
    if (voice) voice->insertProc.setSlotIndex(r);

    // Retire-then-swap: the old engine continues rendering its in-flight tail from a retired slot.
    auto oldEngine = std::move(proc.voiceEngines[(size_t) r]);
    proc.voiceEngines[(size_t) r] = std::move(voice);

    if (oldEngine)
    {
        // Must happen BEFORE placement so the engine is already in its
        // released / filter-reset state when the next audio block picks it up.
        oldEngine->markRetired();

        bool placed = false;
        for (auto& slot : proc.retiredVoiceEngines[(size_t) r])
            if (! slot) { slot = std::move(oldEngine); placed = true; break; }
        if (! placed)
        {
            // All retired slots full — spam-swap back-pressure: force-cut slot 0. The engine it held
            // goes to the caller's slot when given, so its teardown runs after the caller's unlock.
            auto victim = std::move(proc.retiredVoiceEngines[(size_t) r][0]);
            proc.retiredVoiceEngines[(size_t) r][0] = std::move(oldEngine);
            if (cutVictim != nullptr) *cutVictim = std::move(victim);
            proc.retiredReadyForCleanup[(size_t) r][0].store(false, std::memory_order_release);
        }
    }

    // Swap rather than assign: the outgoing rhythm ends up in the caller's object, which the caller
    // destroys after releasing its locks.
    std::swap(proc.sequencer.getRhythm(r), rhythm);
    proc.samples.setPath(r, samplePath);
    // Rebuilds the pattern and hands the audio thread its start position in one step.
    proc.sequencer.requestSwapStart(r, originEff, frameEpoch);
}

void HotSwapStager::processSwaps()
{
    // Drain retired-engine cleanup flags. Audio thread store-releases the per-slot
    // flag once VoiceEngine::isFullyDrained() returns true; the move-out below
    // transfers ownership to a local that destructs off the RT thread.
    // suspendProcessing fences the audio thread so it cannot be mid-process() on
    // the engine when the unique_ptr is yanked.
    // Empty-fast-path: skip the suspend cost when no engines need cleanup.
    bool anyCleanupNeeded = false;
    for (auto& slotArr : proc.retiredReadyForCleanup)
    {
        for (auto& flag : slotArr)
            if (flag.load(std::memory_order_acquire)) { anyCleanupNeeded = true; break; }
        if (anyCleanupNeeded) break;
    }
    if (anyCleanupNeeded)
    {
        std::array<std::unique_ptr<VoiceEngine>,
                   RhythmSequencer::MaxRhythms * mu_limits::kMaxRetiredVoiceEngines> orphans;
        int orphanCount = 0;
        proc.suspendProcessing(true);
        for (int r = 0; r < (int)proc.retiredReadyForCleanup.size(); ++r)
        {
            for (int i = 0; i < mu_limits::kMaxRetiredVoiceEngines; ++i)
            {
                if (!proc.retiredReadyForCleanup[(size_t)r][(size_t)i]
                        .load(std::memory_order_acquire))
                    continue;
                orphans[(size_t)orphanCount++] =
                    std::move(proc.retiredVoiceEngines[(size_t)r][(size_t)i]);
                proc.retiredReadyForCleanup[(size_t)r][(size_t)i]
                    .store(false, std::memory_order_release);
            }
        }
        proc.suspendProcessing(false);
        // `orphans` destructs at scope exit — fully off the audio thread.
    }

    // Two-pass to suspend audio ONCE per call: collect all ready slots, then swap
    // them under one suspend, then do post-commit APVTS push + UI callback outside it.
    const int n = proc.numActiveRhythms.load(std::memory_order_acquire);
    std::array<int, RhythmSequencer::MaxRhythms> readyRhythms {};
    int readyCount = 0;
    for (int r = 0; r < n; ++r)
        if (stager.isFlagged(r))   // (a swap cancelled after it was flagged is skipped)
            readyRhythms[(size_t)readyCount++] = r;

    if (readyCount > 0)
    {
        // Which slots consume actually applied: a swap cancelled between the scan and here is skipped.
        std::array<bool, RhythmSequencer::MaxRhythms> applied {};
        // Everything the commit retires or empties lives out here, so its destructors run after the
        // lock and the suspend are released, not inside them.
        std::array<PendingRhythm, RhythmSequencer::MaxRhythms>            spent;
        std::array<std::unique_ptr<VoiceEngine>, RhythmSequencer::MaxRhythms> cutVictims;
        proc.suspendProcessing(true);
        {
            // suspendProcessing alone does not stop an in-flight callback in hosts that ignore it
            // (CLAP); rhythmsLock is what the audio thread try-locks, so the engine and sample moves
            // below cannot overlap a block. Released before the APVTS push.
            const juce::ScopedLock sl(proc.rhythmsLock);
            for (int idx = 0; idx < readyCount; ++idx)
            {
                const int r = readyRhythms[(size_t)idx];
                applied[(size_t)r] = stager.consume(r, [&](PendingRhythm& sw)
                {
                    spent[(size_t) r] = std::move(sw);
                    auto& in = spent[(size_t) r];
                    installRhythm(r, std::move(in.rhythm), std::move(in.voice), in.samplePath,
                                  flaggedOrigin[(size_t) r].load(std::memory_order_relaxed),
                                  flaggedEpoch [(size_t) r].load(std::memory_order_relaxed),
                                  &cutVictims[(size_t) r]);
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
    if (stager.consumeFull([this](PreparedFullPreset& p)
        {
            proc.presetIO.commitStagedFullPreset(p, fullFlaggedOrigin.load(std::memory_order_relaxed),
                                                 fullFlaggedEpoch.load(std::memory_order_relaxed));
        })
        && proc.onPresetSwapCommitted)
        proc.onPresetSwapCommitted();
}

} // namespace mu_clid
