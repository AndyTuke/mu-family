#pragma once

#include <juce_data_structures/juce_data_structures.h>
#include "Plugin/HotSwapBoundary.h"
#include <array>
#include <atomic>

namespace mu_tant
{

// Hot-swap staging state machine for mu-tant (product-side; see backlog
// #880 / #883). Holds the per-voice pending slots plus a single full-preset
// pending slot and orchestrates the store-release / load-acquire handshake;
// the actual state application (replaceState / readVoiceDataFromState /
// loadVoicePreset body) lives in PluginProcessor, which drains committed swaps
// from here. Keeping the apply out of this class avoids any PluginProcessor
// coupling, so the staging logic is unit-testable on its own.
//
// Threading contract:
//   - The payload `tree` is only ever touched on the MESSAGE thread (stage*
//     writes it, take* reads/moves it) — the message loop serialises those, so
//     the tree itself needs no lock.
//   - The audio thread touches ONLY the atomic flags: checkBoundaries() reads
//     `isReady` (acquire) and sets `boundaryReached` (release).
class VoiceHotSwapStager
{
public:
    static constexpr int kMaxVoices = 8;

    // One staged payload. The expensive parse + wavetable disk pre-load happen
    // at stage time; the commit is a fast in-memory apply.
    struct PendingSwap
    {
        juce::ValueTree   tree;
        std::atomic<bool> isReady         { false };
        std::atomic<bool> boundaryReached { false };

        PendingSwap() = default;
        PendingSwap(const PendingSwap&) = delete;
        PendingSwap& operator=(const PendingSwap&) = delete;
    };

    // ── Message thread: staging ──────────────────────────────────────────────
    // Stage a per-voice (.muPattern) preset for voice `v`, superseding any swap
    // already pending on that slot. `isReady=false` first so a concurrent
    // checkBoundaries() can't observe a half-written slot.
    void stageVoice(int v, juce::ValueTree&& tree) noexcept
    {
        if (v < 0 || v >= kMaxVoices) return;
        auto& s = voiceSwaps[(size_t) v];
        s.isReady.store(false, std::memory_order_release);
        s.boundaryReached.store(false, std::memory_order_relaxed);
        s.tree = std::move(tree);
        s.isReady.store(true, std::memory_order_release);
    }

    // Stage a full preset. A full preset replaces every voice, so it supersedes
    // all pending per-voice swaps.
    void stageFull(juce::ValueTree&& tree) noexcept
    {
        for (auto& s : voiceSwaps)
        {
            s.isReady.store(false, std::memory_order_release);
            s.boundaryReached.store(false, std::memory_order_relaxed);
            s.tree = juce::ValueTree();
        }
        fullSwap.isReady.store(false, std::memory_order_release);
        fullSwap.boundaryReached.store(false, std::memory_order_relaxed);
        fullSwap.tree = std::move(tree);
        fullSwap.isReady.store(true, std::memory_order_release);
    }

    void cancelVoice(int v) noexcept
    {
        if (v < 0 || v >= kMaxVoices) return;
        auto& s = voiceSwaps[(size_t) v];
        s.isReady.store(false, std::memory_order_release);
        s.boundaryReached.store(false, std::memory_order_relaxed);
        s.tree = juce::ValueTree();
    }

    bool hasVoicePending(int v) const noexcept
    {
        return v >= 0 && v < kMaxVoices
            && voiceSwaps[(size_t) v].isReady.load(std::memory_order_acquire);
    }
    bool hasFullPending() const noexcept
    {
        return fullSwap.isReady.load(std::memory_order_acquire);
    }

    // ── Audio thread: boundary detection ─────────────────────────────────────
    // For each ready swap, consult the boundary predicate and flag those that
    // reached their loop point this block. Returns true if anything was newly
    // flagged (caller then calls triggerAsyncUpdate()).
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
        {
            auto& s = voiceSwaps[(size_t) v];
            if (s.isReady.load(std::memory_order_acquire)
                && ! s.boundaryReached.load(std::memory_order_relaxed)
                && hotswap::swapBoundaryReached(playing, wasPlaying, oldPos, newPos,
                                                voicePatBeats[(size_t) v]))
            {
                s.boundaryReached.store(true, std::memory_order_release);
                any = true;
            }
        }
        if (fullSwap.isReady.load(std::memory_order_acquire)
            && ! fullSwap.boundaryReached.load(std::memory_order_relaxed)
            && hotswap::swapBoundaryReached(playing, wasPlaying, oldPos, newPos, fullPatBeats))
        {
            fullSwap.boundaryReached.store(true, std::memory_order_release);
            any = true;
        }
        return any;
    }

    // ── Message thread: commit drain ─────────────────────────────────────────
    // If voice `v` has a flagged, ready swap, move its tree into `out`, clear
    // the slot, and return true. The caller then applies `out`.
    bool takeVoice(int v, juce::ValueTree& out) noexcept
    {
        if (v < 0 || v >= kMaxVoices) return false;
        auto& s = voiceSwaps[(size_t) v];
        if (! (s.isReady.load(std::memory_order_acquire)
               && s.boundaryReached.load(std::memory_order_acquire)))
            return false;
        out = std::move(s.tree);
        s.tree = juce::ValueTree();
        s.boundaryReached.store(false, std::memory_order_relaxed);
        s.isReady.store(false, std::memory_order_release);
        return true;
    }

    bool takeFull(juce::ValueTree& out) noexcept
    {
        if (! (fullSwap.isReady.load(std::memory_order_acquire)
               && fullSwap.boundaryReached.load(std::memory_order_acquire)))
            return false;
        out = std::move(fullSwap.tree);
        fullSwap.tree = juce::ValueTree();
        fullSwap.boundaryReached.store(false, std::memory_order_relaxed);
        fullSwap.isReady.store(false, std::memory_order_release);
        return true;
    }

private:
    std::array<PendingSwap, kMaxVoices> voiceSwaps;
    PendingSwap                         fullSwap;
};

} // namespace mu_tant
