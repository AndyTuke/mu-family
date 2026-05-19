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

    static constexpr int kMaxRhythms = 8; // = mu_limits::kMaxRhythms
    std::array<PendingRhythmSwap, kMaxRhythms> pendingSwaps;
    PluginProcessor& proc_;
};
