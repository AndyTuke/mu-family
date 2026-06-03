#pragma once

#include <atomic>
#include <cstdint>
#include <thread>
#include <vector>

namespace mu_tant
{

// One drawable attack/decay envelope occupying a contiguous region of the
// 2-bar gate grid. See docs/mu-tant/design-sequencer.md.
//
//   startCell    0-based subdivision index where the region begins.
//   lengthCells  region span in cells (>=1). A pencil click makes 1; start/end
//                grab handles extend a region to multiple cells.
//   split        peak position 0..1 within the region (the attack/decay split):
//                  0 -> instant attack, pure decay (the gate default)
//                  1 -> pure attack, no decay
//   attackBend   -1..+1 bend of the rising attack line  (- concave, + convex)
//   decayBend    -1..+1 bend of the falling decay line  (- concave, + convex)
//   reverse      mirror the shape in time, swapping attack and decay.
//
// `value(phase01, gap01)` is the single source of truth shared by the audio
// evaluator and the editor renderer: it evaluates the gate 0..1 at a
// within-region phase 0..1, after applying the per-voice Gap (a trailing
// fraction of the region forced to silence). Pure: no allocation, no
// thread-affinity.
struct GateEnvelope
{
    int   startCell   = 0;
    int   lengthCells = 1;
    float split       = 0.0f;
    float attackBend  = 0.0f;
    float decayBend   = 0.0f;
    bool  reverse     = false;

    // Per-envelope probability. 0..1 chance this envelope fires each pattern
    // loop; evaluated via a deterministic per-(loop,cell) hash so the decision
    // is stable across the duration of one loop and different each loop.
    float probability = 1.0f;

    float value(float phase01, float gap01 = 0.0f) const noexcept;

    // Returns false when the probability rule suppresses this envelope this loop.
    bool playsOnLoop(int localLoopCount) const noexcept;

    // True if this region covers the given cell.
    bool covers(int cellIdx) const noexcept
    {
        return cellIdx >= startCell && cellIdx < startCell + lengthCells;
    }

private:
    // Forward attack/decay shape at region-phase p in [0,1] (ignores gap +
    // reverse, which value() handles).
    float shape(float p) const noexcept;
};

// Drawable gate pattern — 1 to kMaxPatternBars bars long, always viewed in
// a 2-bar window that scrolls smoothly. See docs/mu-tant/design-sequencer.md.
//
// Storage is a sorted, non-overlapping std::vector<GateEnvelope> (by startCell).
// The message thread mutates under editLock; the audio thread reads via gateAt()
// which is bounded-array indexed and allocation-free.
class GatePattern
{
public:
    enum class Subdivision : int
    {
        Quarter      = 4,
        Eighth       = 8,
        Sixteenth    = 16,
        ThirtySecond = 32
    };

    static constexpr int kMaxPatternBars = 16;  // maximum editable length

    // Mutable pattern length (1..kMaxPatternBars bars). Written by the UI;
    // read by the audio thread inside gateAt (always under editLock).
    int patternLengthBars = 2;

    // Minimum gate-open time. A "0 attack" envelope opens instantly (0→1), which
    // clicks; the gater slew-limits the RISING edge so the gate can't open faster
    // than this. 10 ms spans a meaningful fraction of a low-drone cycle (e.g. half
    // a 50 Hz period), so the amplitude ramp is gradual enough to be click-free —
    // a shorter floor (~3 ms) still ticked on sustained low content. mu-tant is a
    // drone, not percussive, so the longer ramp costs no transient. Attacks drawn
    // slower than this are unaffected (the slew only engages when the envelope
    // rises faster than the cap).
    static constexpr float kMinAttackMs = 10.0f;

    Subdivision subdivision = Subdivision::Sixteenth;
    std::vector<GateEnvelope> envelopes;   // sorted by startCell, non-overlapping

    // Lock-free flag: true iff envelopes is non-empty. Written by the message
    // thread (inside editLock context) after any mutation; read by the audio
    // thread as a pre-lock early-out so envelopes is never touched without
    // the lock. Relaxed ordering is sufficient — the audio thread's
    // compare_exchange_strong (acquire) on editLock is the synchronisation
    // point for the actual vector access.
    std::atomic<bool> hasEnvelopes { false };

    // Total cells in the pattern given the current subdivision (patternLengthBars * denom).
    int totalCells() const noexcept;

    // The envelope whose region covers the given cell, or nullptr. Message- and
    // audio-thread safe (read-only linear scan).
    const GateEnvelope* envelopeAtCell(int cellIdx) const noexcept;

    // ── Editing (message-thread-only, under editLock) ────────────────────────
    // Insert `env`, first removing any existing envelope that overlaps its
    // region so the set stays non-overlapping. Returns a pointer to the stored
    // envelope (valid until the next mutation).
    GateEnvelope* addEnvelope(const GateEnvelope& env);

    // Remove whichever envelope covers cellIdx (if any). No-op if none.
    void removeEnvelopeCovering(int cellIdx);

    // Merge every envelope intersecting [firstCell, lastCell] into one envelope
    // filling that whole range. The merged envelope's split / attackBend /
    // decayBend are the average of the merged envelopes (defaults if empty).
    // reverse is cleared. Used internally and by tests; no UI tool exposes this.
    void mergeRange(int firstCell, int lastCell);

    // ── Audio-thread gate evaluation ─────────────────────────────────────────
    // Returns the 0..1 gate value at the given absolute beat position (wrapped
    // mod the pattern length), with the per-voice Gap applied. 1.0 when the
    // pattern is empty (no gating — continuous drone). Cells with no envelope
    // return 0 (silent). Caches the cell->envelope lookup so per-sample cost is
    // O(1) except on cell changes; cache state is audio-thread-only.
    // The function computes its own local loop count from beatPos for probability
    // evaluation — the caller need not maintain a separate loop counter.
    float gateAt(double beatPos, float gap01) const noexcept;

    // Invalidate the gate cache — call once at the top of each block's gate
    // pass (under editLock) so a between-block edit can't leave a dangling
    // cached envelope pointer.
    void resetGateCache() const noexcept { cachedCell = -1; cachedEnv = nullptr; }

    // Copy the pattern *content* (subdivision + envelopes) from another pattern,
    // leaving this pattern's editLock untouched and resetting the gate cache.
    // Used when voices are added/removed and the per-voice patterns shift slots.
    // Call on the message thread while the audio thread is excluded (the
    // processor holds its voicesLock during the shift). Acquires other.editLock
    // to guard against a concurrent GatingDesigner edit on the source pattern
    // (e.g. user drawing on voice N while voice N is being removed). GatePattern
    // can't be copy/move-assigned directly because of the embedded std::atomic
    // editLock.
    void copyDataFrom(const GatePattern& other) noexcept
    {
        // Capped spin matching GatingDesigner::withLock. The UI holds editLock
        // only briefly around single-envelope mutations; 1000 yields covers any
        // contention comfortably. If not acquired by the cap, skip the copy —
        // the source pattern remains unchanged and the caller can retry.
        {
            constexpr int kMaxSpins = 1000;
            bool acquired = false;
            for (int i = 0; i < kMaxSpins; ++i)
            {
                bool expected = false;
                if (other.editLock.compare_exchange_strong(expected, true, std::memory_order_acquire))
                    { acquired = true; break; }
                std::this_thread::yield();
            }
            if (!acquired) return;
        }
        subdivision       = other.subdivision;
        patternLengthBars = other.patternLengthBars;
        envelopes         = other.envelopes;
        other.editLock.store(false, std::memory_order_release);

        hasEnvelopes.store(!envelopes.empty(), std::memory_order_relaxed);
        gateLevel   = 0.0f;
        filterLevel = 1.0f;
        resetGateCache();
    }

    // Spin-lock guarding envelope mutation (UI thread) vs gate reads (audio
    // thread). The audio thread tryLocks for the gate pass only; the UI holds
    // it briefly around edits.
    mutable std::atomic<bool> editLock { false };

    // Audio-thread slew state for the rising-edge de-click (see kMinAttackMs).
    // Persists across blocks for edge continuity; NOT reset by resetGateCache.
    // Reset to 0 on copyDataFrom + whenever the gate is held closed.
    float gateLevel = 0.0f;

    // Audio-thread slew state for filter envelope — applies the same kMinAttackMs
    // rise-limiting to prevent filter clicks when gate and filter envelopes are
    // out of sync. Persists across blocks; reset to 1.0 on copyDataFrom.
    float filterLevel = 1.0f;

private:
    void sortEnvelopes();

    mutable int                 cachedCell = -1;
    mutable const GateEnvelope* cachedEnv  = nullptr;
};

// ── Block-level gater (shared by the audio engine + the audio test harness) ──
//
// The gater multiplies a voice's post-filter output by the gate value. Its
// block-level behaviour:
//   bypassed              → Pass    (raw drone passes — audition / configure)
//   stopped               → Silence (gate closed — nothing audible on load/stop)
//   playing, empty pattern→ Silence (no envelopes drawn → nothing passes)
//   playing, has envelopes→ Envelope(per-sample envelope gate from beatStart)
enum class GateMode { Pass, Silence, Envelope };

GateMode gateModeFor(bool bypassed, bool playing, bool patternEmpty) noexcept;

// Apply the gater to a voice's stereo channel pointers in place (`right` may be
// null for mono). Pure except for the pattern's audio-thread gate cache + a
// tryLock around the envelope read. Allocation-free. Each pattern internally
// wraps the beat position at its own patternLengthBars and computes per-loop
// probability rolls — no loopCount argument needed.
void applyGateBlock(GatePattern& pattern, float* left, float* right, int numSamples,
                    float gap01, bool bypassed, bool playing,
                    double beatStart, double beatsPerSample, double sampleRate) noexcept;

} // namespace mu_tant
