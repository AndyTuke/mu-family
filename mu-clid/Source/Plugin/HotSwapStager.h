#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include "Sequencer/GeneratorAliases.h"
#include "Audio/VoiceEngine.h"
#include "Sequencer/GeneratorAliases.h"
#include "Plugin/HotSwap.h"   // mu-core: the shared staging state
#include <array>
#include <atomic>
#include <memory>

namespace mu_clid {

class PluginProcessor;

// mu-Clid's hot-swap machinery: the shared mu-core stager holding pre-built rhythms (per
// rhythm + one full preset), mu-Clid's boundary rule (master loop / each rhythm's own loop), and
// the message-thread commit pass.
//
// Threading contract (mu_hotswap::Stager):
//   - Payloads are written on the message thread (stage / cancel) and committed there
//     (processSwaps, under suspendProcessing); the audio thread touches only the flags
//     (checkBoundaries).
//   - No lock is held during stage() — the isReady store-release is the barrier.
class HotSwapStager
{
public:

    // A full .muClid preset pre-built off the audio thread, ready to swap in at the
    // next loop boundary. The expensive work (parsing, per-slot VoiceEngine build +
    // sample disk load, Rhythm population) all happens at stage time so the commit
    // is just fast in-memory moves under a single suspend. tree carries the parsed
    // root so the commit can apply the non-Rhythm APVTS state (mixer / globals).
    struct PreparedFullPreset
    {
        int numRhythms = 0;
        std::array<Rhythm, mu_limits::kMaxLayers>                       rhythms     {};
        std::array<std::unique_ptr<VoiceEngine>, mu_limits::kMaxLayers> voices      {};
        std::array<juce::String, mu_limits::kMaxLayers>                 samplePaths {};
        juce::ValueTree                                       tree;
    };

    explicit HotSwapStager(PluginProcessor& proc) : proc(proc) {}

    // Message-thread: cancel any existing staged swap for a slot without bounds check.
    // Called from PresetIO::stageRhythmPreset before overwriting a pending swap.
    void cancelPendingIfAny(int rhythmIndex);

    // Message-thread: atomically commit all staged data for a slot.
    // Called from PresetIO::stageRhythmPreset after preparing rhythm + voice.
    void stage(int rhythmIndex, Rhythm&& rhythm, std::unique_ptr<VoiceEngine>&& voice,
               const juce::String& samplePath);

    // Public API — delegated from PluginProcessor.
    void cancelStagedSwap(int rhythmIndex);
    bool hasPendingSwap(int rhythmIndex) const;

    // Message-thread: stage a pre-built full preset for commit at the next MASTER
    // loop boundary. Supersedes any pending per-rhythm swaps (a full preset
    // replaces every slot). Committed via PresetIO::commitStagedFullPreset from
    // processSwaps() once the boundary is reached.
    void stageFullPreset(PreparedFullPreset&& prepared);

    // Message-thread: true if a full-preset swap is staged but not yet committed.
    bool hasPendingFullPreset() const;

    // Audio-thread: scan all pending swaps and flag any that have reached a loop boundary.
    // Returns true if a swap commit should be requested (requestDeferredWork).
    bool checkBoundaries(int numRhythms, bool masterLoopWrapped, int rhythmLoopWrapMask);

    // Install a prepared rhythm into slot `r`: the outgoing engine retires (it keeps rendering its
    // sample tail / amp release from a retired slot), the rhythm, engine and sample path move in and
    // the pattern rebuilds. The one swap behind the loop-boundary commit, the stopped load and the
    // full-preset commit. Caller holds suspendProcessing (and rhythmsLock where needed); the
    // APVTS push follows outside the suspend.
    // originEff / frameEpoch say where the new pattern's step 0 falls (see SequencerEngine::requestSwapStart);
    // the defaults start it on the step counter's own phase, for a load that has no boundary.
    // `rhythm` comes back holding the outgoing rhythm, and a force-cut retired engine goes to
    // *cutVictim when given, so the caller can let both destruct after it drops its locks.
    void installRhythm(int r, Rhythm&& rhythm, std::unique_ptr<VoiceEngine>&& voice, const juce::String& samplePath,
                       int originEff = 0, uint32_t frameEpoch = SequencerEngine::kAnyFrameEpoch,
                       std::unique_ptr<VoiceEngine>* cutVictim = nullptr);

    // Message-thread: drain retired-engine cleanup + commit all flagged swaps.
    // Called from PluginProcessor::runDeferredWork.
    void processSwaps();

private:
    // One rhythm's staged swap, pre-built at stage time.
    struct PendingRhythm
    {
        Rhythm                       rhythm;
        juce::String                 samplePath;
        std::unique_ptr<VoiceEngine> voice;
    };

    mu_hotswap::Stager<PendingRhythm, PreparedFullPreset, mu_limits::kMaxLayers> stager;

    PluginProcessor& proc;

    // Where each staged swap's loop boundary fell, written by the audio thread just before it flags
    // the swap (the flag's release publishes them) and read by the committing thread after it
    // takes the flag. Meaningless for a swap that is not flagged.
    std::array<std::atomic<int>,      mu_limits::kMaxLayers> flaggedOrigin {};
    std::array<std::atomic<uint32_t>, mu_limits::kMaxLayers> flaggedEpoch  {};
    std::atomic<int>      fullFlaggedOrigin { 0 };
    std::atomic<uint32_t> fullFlaggedEpoch  { 0 };
};

} // namespace mu_clid
