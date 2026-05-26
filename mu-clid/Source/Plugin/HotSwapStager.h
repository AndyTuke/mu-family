#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include "Sequencer/Rhythm.h"
#include "Audio/VoiceEngine.h"
#include <array>
#include <atomic>
#include <memory>

class PluginProcessor;

// Encapsulates all hot-swap staging machinery: the per-rhythm pending-swap state,
// the audio-thread boundary detection, and the message-thread commit pass.
//
// Threading contract (preserved from PluginProcessor):
//   - pendingSwaps[] is written on the message thread (stage / cancelPendingIfAny /
//     cancelStagedSwap) and read atomically by the audio thread (checkBoundaries).
//   - processSwaps() runs on the message thread under suspendProcessing + rhythmsLock.
//   - No lock is held during stage() — the isReady store-release is the barrier.
class HotSwapStager
{
public:
    static constexpr int kMaxRhythms = 8; // = mu_limits::kMaxRhythms

    // A full .muClid preset pre-built off the audio thread, ready to swap in at the
    // next loop boundary. The expensive work (parsing, per-slot VoiceEngine build +
    // sample disk load, Rhythm population) all happens at stage time so the commit
    // is just fast in-memory moves under a single suspend. tree carries the parsed
    // root so the commit can apply the non-Rhythm APVTS state (mixer / globals).
    struct PreparedFullPreset
    {
        int numRhythms = 0;
        std::array<Rhythm, kMaxRhythms>                       rhythms     {};
        std::array<std::unique_ptr<VoiceEngine>, kMaxRhythms> voices      {};
        std::array<juce::String, kMaxRhythms>                 samplePaths {};
        juce::ValueTree                                       tree;
    };

    explicit HotSwapStager(PluginProcessor& proc) : proc_(proc) {}

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

    // Message-thread: drain retired-engine cleanup + commit all flagged swaps.
    // Called from PluginProcessor::handleAsyncUpdate.
    void processSwaps();

private:
    struct PendingRhythmSwap
    {
        Rhythm                       pendingRhythm;
        juce::String                 pendingSamplePath;
        std::unique_ptr<VoiceEngine> pendingVoice;
        std::atomic<bool> isReady         { false };
        std::atomic<bool> boundaryReached { false };

        PendingRhythmSwap() = default;
        PendingRhythmSwap(const PendingRhythmSwap&) = delete;
        PendingRhythmSwap& operator=(const PendingRhythmSwap&) = delete;
    };

    std::array<PendingRhythmSwap, kMaxRhythms> pendingSwaps;

    // Full-preset deferral. pendingPreset is written/read only on the message thread
    // (stageFullPreset / processSwaps); the audio thread touches only the two atomic
    // flags, so the payload needs no lock. Same store-release / load-acquire handshake
    // as the per-rhythm swaps above.
    PreparedFullPreset pendingPreset;
    std::atomic<bool>  presetReady           { false };
    std::atomic<bool>  presetBoundaryReached { false };

    PluginProcessor& proc_;
};
