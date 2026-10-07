#pragma once

#include <juce_data_structures/juce_data_structures.h>
#include "Plugin/HotSwap.h"            // mu-core: the shared staging state
#include "Plugin/HotSwapBoundary.h"
#include <array>

namespace mu_tant
{

// mu-tant's preset hot-swap staging: the shared mu-core stager holding parsed preset trees
// (per voice + one full preset), plus mu-tant's boundary rule. The apply (replaceState /
// readVoiceDataFromState / loadVoicePreset body) lives in PluginProcessor, which drains committed
// swaps from here, so the staging logic stays unit-testable on its own. Threading as
// mu_hotswap::Stager: trees on the message thread only, the audio thread touches only the flags.
class VoiceHotSwapStager
{
public:
    static constexpr int kMaxVoices = 8;

    // ── Message thread: staging ──────────────────────────────────────────────
    // Per-voice (.muPattern) preset for voice `v`, superseding any swap pending on that slot.
    void stageVoice(int v, juce::ValueTree&& tree) { stager.stage(v, std::move(tree)); }
    // A full preset replaces every voice, so it supersedes all pending per-voice swaps.
    void stageFull(juce::ValueTree&& tree)         { stager.stageFull(std::move(tree)); }
    void cancelVoice(int v)                        { stager.cancel(v); }

    bool hasVoicePending(int v) const noexcept { return stager.hasPending(v); }
    bool hasFullPending() const noexcept       { return stager.hasFullPending(); }

    // ── Audio thread: boundary detection ─────────────────────────────────────
    // Flag each ready swap that reached its loop point this block. Returns true if anything was
    // newly flagged (caller then calls triggerAsyncUpdate()).
    //   voicePatBeats[v] = voice v's own gate-pattern length in beats
    //   fullPatBeats     = voice 0's gate-pattern length in beats (full-preset ref)
    bool checkBoundaries(int numActiveVoices, bool playing, bool wasPlaying,
                         double oldPos, double newPos,
                         const std::array<double, kMaxVoices>& voicePatBeats,
                         double fullPatBeats) noexcept
    {
        bool any = false;
        const int n = juce::jlimit(0, kMaxVoices, numActiveVoices);
        for (int v = 0; v < n; ++v)
            any |= stager.flagIfReady(v, hotswap::swapBoundaryReached(playing, wasPlaying, oldPos, newPos,
                                                                       voicePatBeats[(size_t) v]));
        any |= stager.flagFullIfReady(hotswap::swapBoundaryReached(playing, wasPlaying, oldPos, newPos, fullPatBeats));
        return any;
    }

    // ── Message thread: commit drain ─────────────────────────────────────────
    // If voice `v` has a flagged, ready swap, move its tree into `out`, clear the slot and return
    // true. The caller then applies `out`.
    bool takeVoice(int v, juce::ValueTree& out)
    {
        return stager.consume(v, [&out](juce::ValueTree& t) { out = std::move(t); });
    }
    bool takeFull(juce::ValueTree& out)
    {
        return stager.consumeFull([&out](juce::ValueTree& t) { out = std::move(t); });
    }

private:
    mu_hotswap::Stager<juce::ValueTree, juce::ValueTree, kMaxVoices> stager;
};

} // namespace mu_tant
