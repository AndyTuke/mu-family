#pragma once

#include <atomic>
#include <cstdint>
#include <vector>

namespace mu_tant
{

// Per-envelope options that gate whether the envelope actually fires on a
// given pass through the pattern. See docs/mu-tant/design-sequencer.md
// "Per-envelope options" for the source-of-truth descriptions.
struct GateEnvelopeOptions
{
    bool    reverse           = false;  // play as attack (0→1) instead of decay (1→0)
    float   probability       = 1.0f;   // 0..1 fire-chance evaluated at pattern wrap
    int     loopN             = 1;      // fire on pass N of M (1-based)
    int     loopM             = 1;      // M-cycle length (1..8); 1 disables loop-N-of-M
    bool    firstOnly         = false;  // fire only on the very first pass
    bool    onStagedForChange = false;  // fire only while a hot-swap is staged-but-uncommitted
};

// One drawable decay envelope locked to a single grid cell.
//
// `cell` is the 0-based subdivision index within the 2-bar pattern. The
// number of cells depends on the pattern's subdivision (see GatePattern::
// totalCells()).
//
// `curveBend` in [-1, +1] controls the shape:
//   -1 → concave (faster initial drop, audible gap inside the cell)
//    0 → linear  (1.0 → 0.0 over the cell — default)
//   +1 → convex  (sustains near 1.0, drops only at the cell's end)
//
// `value(phase01)` evaluates the envelope at a within-cell phase 0..1.
// `reverse` swaps phase polarity so the envelope plays as an attack.
struct GateEnvelope
{
    int                  cell      = 0;
    float                curveBend = 0.0f;
    GateEnvelopeOptions  options;

    // Evaluate the gate value at a within-cell phase 0..1.
    // Pure: no allocation, no thread-affinity. The audio thread reads this.
    float value(float phase01) const noexcept;
};

// Drawable 2-bar gate pattern. See docs/mu-tant/design-sequencer.md.
//
// Storage is a flat std::vector<GateEnvelope> — the message thread mutates
// (under a spin-lock owned by the caller, mirroring VoiceSlot::modLock); the
// audio thread reads via cellEnvelope(cellIdx) which is bounded-array
// indexed and allocation-free.
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

    Subdivision subdivision = Subdivision::Sixteenth;
    std::vector<GateEnvelope> envelopes;

    // Total cells in the pattern given the current subdivision (kTotalBars * denom).
    int totalCells() const noexcept;

    // Returns a pointer to the envelope owning the given cell (or nullptr).
    // The owning envelope is the one whose `cell` is the closest <= cellIdx
    // before the next envelope's cell, accounting for the "adding an
    // overlapping envelope shortens the underlying one" rule from the design:
    // there's at most one envelope per cell, found by direct match.
    const GateEnvelope* cellEnvelope(int cellIdx) const noexcept;

    // Add an envelope at cellIdx, replacing any existing envelope at that
    // cell. Returns a pointer to the stored envelope. Message-thread-only.
    GateEnvelope* addOrReplaceEnvelope(const GateEnvelope& env);

    // Remove the envelope at cellIdx if any. No-op if none. Message-thread-only.
    void removeEnvelopeAt(int cellIdx);

    // Returns the span (in cells) the envelope at envIdx covers — capped by
    // the next envelope's cell, or the pattern end. Used by both the audio
    // evaluator + the editor renderer.
    int envelopeSpan(int envIdx) const noexcept;

    // ── Audio-thread gate evaluation ─────────────────────────────────────────
    // Returns the 0..1 gate value at the given absolute song beat position
    // (wrapped mod the 2-bar pattern). 1.0 when the pattern is empty (no
    // gating — continuous drone). When non-empty: the envelope owning the
    // current cell drives the value; cells with no envelope return 0 (silent).
    // Uses a per-call cell cache so the per-sample cost is O(1) except on cell
    // changes. Cache state is audio-thread-only (each voice owns one pattern).
    float gateAt(double beatPos) const noexcept;

    // Invalidate the gate cache — call once at the top of each block's gate
    // pass (under the editLock) so a between-block edit can't leave a dangling
    // cached envelope pointer.
    void resetGateCache() const noexcept { cachedCell = -1; cachedEnv = nullptr; }

    // Spin-lock guarding envelope mutation (UI thread) vs gate reads (audio
    // thread). The audio thread tryLocks for the gate pass only; the UI holds
    // it briefly around add/remove. CopyableSpinLock-style atomic flag.
    mutable std::atomic<bool> editLock { false };

private:
    mutable int                 cachedCell = -1;
    mutable const GateEnvelope* cachedEnv  = nullptr;
};

} // namespace mu_tant
