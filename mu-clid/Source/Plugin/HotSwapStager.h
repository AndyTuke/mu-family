#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include "Sequencer/Rhythm.h"
#include "Audio/VoiceEngine.h"
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
    // Returns true if triggerAsyncUpdate() should be called.
    bool checkBoundaries(int numRhythms, bool masterLoopWrapped, int rhythmLoopWrapMask);

    // Install a prepared rhythm into slot `r`: the outgoing engine retires (it keeps rendering its
    // sample tail / amp release from a retired slot), the rhythm, engine and sample path move in and
    // the pattern rebuilds. The one swap behind the loop-boundary commit, the stopped load and the
    // full-preset commit. Caller holds suspendProcessing (and rhythmsLock where needed); the
    // APVTS push follows outside the suspend.
    void installRhythm(int r, Rhythm&& rhythm, std::unique_ptr<VoiceEngine>&& voice, const juce::String& samplePath);

    // Message-thread: drain retired-engine cleanup + commit all flagged swaps.
    // Called from PluginProcessor::handleAsyncUpdate.
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
};

} // namespace mu_clid
