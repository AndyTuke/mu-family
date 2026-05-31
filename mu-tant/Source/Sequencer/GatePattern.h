#pragma once

#include <atomic>
#include <cstdint>
#include <vector>

namespace mu_tant
{

// One drawable attack/decay envelope occupying a contiguous region of the
// 2-bar gate grid. See docs/mu-tant/design-sequencer.md.
//
//   startCell    0-based subdivision index where the region begins.
//   lengthCells  region span in cells (>=1). A pencil click makes 1; glue
//                merges several into one wider region.
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

    // Per-envelope playback options. Evaluated once per pattern loop by the
    // audio gate, so changes take effect on the next loop boundary.
    float   probability = 1.0f;   // 0..1 chance this envelope fires each loop
    uint8_t loopMask    = 0x01;   // bitmask: bit N set = play on position N of every loopM loops
    int     loopM       = 1;      // cycle length in loops (1..8); positions 0..loopM-1

    float value(float phase01, float gap01 = 0.0f) const noexcept;

    // Returns false when loopCount/probability rule suppresses this envelope.
    // Uses a deterministic per-(loop,cell) hash for probability so the
    // decision is consistent within a loop pass and different each loop.
    bool playsOnLoop(int loopCount) const noexcept;

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

// Drawable 2-bar gate pattern. See docs/mu-tant/design-sequencer.md.
//
// Storage is a flat std::vector<GateEnvelope> kept sorted by startCell and
// non-overlapping. The message thread mutates (under editLock); the audio
// thread reads via gateAt() which is bounded-array indexed and allocation-free.
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

    // 2 bars in 4/4 — design-spec constant.
    static constexpr int kTotalBars = 2;

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

    // Total cells in the pattern given the current subdivision (kTotalBars * denom).
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

    // Glue: merge every envelope intersecting [firstCell, lastCell] into one
    // envelope filling that whole range. The merged envelope's split /
    // attackBend / decayBend are the average of the envelopes that were there
    // (defaults if the range was empty). reverse is cleared.
    void mergeRange(int firstCell, int lastCell);

    // ── Audio-thread gate evaluation ─────────────────────────────────────────
    // Returns the 0..1 gate value at the given absolute beat position (wrapped
    // mod the 2-bar pattern), with the per-voice Gap applied. 1.0 when the
    // pattern is empty (no gating — continuous drone). Cells with no envelope
    // return 0 (silent). Caches the cell->envelope lookup so per-sample cost is
    // O(1) except on cell changes; cache state is audio-thread-only.
    // `loopCount` is the number of completed pattern loops; used to evaluate
    // per-envelope loopN/loopM and probability rules.
    float gateAt(double beatPos, float gap01, int loopCount = 0) const noexcept;

    // Invalidate the gate cache — call once at the top of each block's gate
    // pass (under editLock) so a between-block edit can't leave a dangling
    // cached envelope pointer.
    void resetGateCache() const noexcept { cachedCell = -1; cachedEnv = nullptr; }

    // Copy the pattern *content* (subdivision + envelopes) from another pattern,
    // leaving this pattern's editLock untouched and resetting the gate cache.
    // Used when voices are added/removed and the per-voice patterns shift slots.
    // Call on the message thread while the audio thread is excluded (the
    // processor holds its voicesLock during the shift). GatePattern can't be
    // copy/move-assigned directly because of the embedded std::atomic editLock.
    void copyDataFrom(const GatePattern& other) noexcept
    {
        subdivision = other.subdivision;
        envelopes   = other.envelopes;
        gateLevel   = 0.0f;
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
// tryLock around the envelope read. Allocation-free.
// `loopCount` is the number of completed pattern loops; forwarded to gateAt so
// per-envelope loopN/loopM and probability rules are evaluated correctly.
void applyGateBlock(GatePattern& pattern, float* left, float* right, int numSamples,
                    float gap01, bool bypassed, bool playing,
                    double beatStart, double beatsPerSample, double sampleRate,
                    int loopCount = 0) noexcept;

} // namespace mu_tant
