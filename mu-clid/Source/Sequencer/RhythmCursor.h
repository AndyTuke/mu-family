#pragma once

#include "Sequencer/GeneratorAliases.h"   // Rhythm, EuclidOverrides, RhythmSequencer (StepLengthBeats, beatToStep)

#include <atomic>
#include <cstdint>
#include <vector>

// mu-Clid's per-layer step cursor (docs/mu-tronic/extraction-plan.md, E5.5a): mu_core::RhythmSequencer's
// arithmetic split in two, verbatim, so a SampleRhythm can clock its own hits. RhythmStepClock is the
// host's half (one per processor): the block's step from the beat, the master-loop modulo and wrap, the
// restart and realign decisions and the step-counter run id. RhythmCursor is one rhythm's half: its
// pattern hand-over from the message thread, its phase, and the step walk that fires, accents and ties.
// For the same beats, patterns and requests both halves together give RhythmSequencer's output bit for
// bit (RhythmCursorEqualityTests). Not wired yet: RhythmSequencer still runs the rhythms.
//
// Why not mu-On's StepClock: that clock fires each 16th at its sample offset with swing and a resync
// window; RhythmSequencer resolves one step per block start with phase origins, a master loop and a
// back-walk of crossed steps. Different arithmetic, so this cursor is mu-Clid's own (Sequencer/).
namespace mu_clid
{

// What the host's step clock decided for one block; every cursor of the block reads the same frame.
struct StepFrame
{
    int      globalStep        = 0;      // the step the block's start beat falls in
    int      effectiveStep     = 0;      // globalStep modulo the master loop (globalStep when free-running)
    int      prevGlobalStep    = -1;     // the previous block's globalStep (-1 after a transport restart)
    bool     restart           = false;  // transport restart: every pattern goes back to its cold start
    bool     realign           = false;  // the step counter restarted or went back: patterns realign to it
    uint32_t epoch             = 0;      // the step-counter run id, after this block's realign
    bool     masterLoopWrapped = false;  // the master loop wrapped this block (the swap boundary for full presets)
};

// The host's half: one per processor, audio thread (the setters are safe from any thread). Host contract,
// as RhythmSequencer::processBlock behaves: skip the clock and every cursor in a block with no live rhythm
// (the sequencer returns before touching its history), and pair each advance with exactly one endBlock after
// every cursor of the block has run, on every path.
class RhythmStepClock
{
public:
    void setMasterLoopSteps(int steps) noexcept { masterLoopSteps.store(juce::jlimit(0, 256, steps), std::memory_order_relaxed); }
    int  getMasterLoopSteps() const noexcept     { return masterLoopSteps.load(std::memory_order_relaxed); }
    void resetWrapDetector() noexcept            { wrapResetRequested.store(true, std::memory_order_release); }
    int  getMasterLoopCurrentStep() const noexcept { return masterLoopCurrentStep.load(std::memory_order_relaxed); }

    // Audio thread, once per playing block, before any cursor: the frame for `beatPosition`.
    StepFrame advance(double beatPosition) noexcept;
    // Audio thread, after every cursor walked the block: the frame's step becomes the previous one.
    void endBlock(const StepFrame& f) noexcept { prevGlobalStep = f.globalStep; }

    // The previous block's effective step (-1 = no history): a modulated pattern length re-derives the
    // phase from it (RhythmCursor::tryApplyModulatedPattern).
    int      lastEffective() const noexcept { return lastEffectiveStep; }
    uint32_t currentEpoch() const noexcept  { return frameEpoch.load(std::memory_order_relaxed); }

private:
    std::atomic<int>      masterLoopSteps { 0 };
    std::atomic<int>      masterLoopCurrentStep { 0 };
    std::atomic<bool>     wrapResetRequested { false };
    std::atomic<uint32_t> frameEpoch { 0 };
    // Audio-thread private.
    int    lastEffectiveStep = -1;
    int    lastLoopSteps     = 0;
    int    prevGlobalStep    = -1;
    double lastGoodBeat      = 0.0;
};

// One rhythm's half: owned by the rhythm, audio thread except where noted.
class RhythmCursor
{
public:
    static constexpr uint32_t kAnyFrameEpoch = 0xffffffffu;

    // What one block's walk produced for this rhythm.
    struct Hit
    {
        bool fired      = false;   // a hit fired (several crossed steps collapse to one)
        bool accented   = false;   // that hit was accented (Ring C)
        bool tied       = false;   // the step walked just before it was also a hit
        bool loopWrapped = false;  // the pattern started a pass in this block (the per-rhythm swap boundary)
    };

    RhythmCursor();

    // Message thread: rebuild the pattern from `r` (clamping its generators to `stepCap`), absorbed at the
    // next block without firing the step the position sits on.
    void updatePattern(Rhythm& r, int stepCap);
    // Message thread, after a hot-swap commit: rebuild and start the new pattern at its step 0 on
    // `originEff` (kAnyFrameEpoch, or the epoch the origin was recorded in; a later run falls back to 0).
    void requestSwapStart(Rhythm& r, int stepCap, int originEff, uint32_t epoch = kAnyFrameEpoch);
    // Message thread, layer lock held: the rhythm moved to another slot; the next block absorbs the
    // current step with the phase it already had (what RhythmSequencer's reorder hand-over amounts to).
    void requestAbsorb();
    // Message thread, layer lock held: back to the never-used state.
    void clear();

    // Audio thread: realign, absorb a hand-over, then walk the steps crossed since the last block.
    Hit advance(const StepFrame& f) noexcept;

    // Audio thread: recompute the pattern from modulated Euclid values (try-lock; false = try next block).
    bool tryApplyModulatedPattern(const Rhythm& r, const EuclidOverrides& ov, int lastEffectiveStep) noexcept;

    // Audio thread (UI copies them out): the last walked step, accent step and pattern length.
    int lastStep() const noexcept       { return lastStepIndex; }
    int lastAccentStep() const noexcept { return lastAccentStepIndex; }
    int patternLength() const noexcept  { return (int) safePattern.size(); }
    // The pattern's length in beats, the per-rhythm swap boundary (negative: no pattern yet).
    double loopBeats() const noexcept
    {
        const int n = patternLength();
        return n > 0 ? n * RhythmSequencer::StepLengthBeats : -1.0;
    }
    // Where the pattern last started a pass, as an effective step (relaxed: the swap stager reads it).
    int boundaryEffective() const noexcept { return boundaryEff.load(std::memory_order_relaxed); }

private:
    enum PendingKind : uint8_t { kNone = 0, kStart, kAbsorb };

    // Message-thread copies and the hand-over, guarded by patternLock (the audio thread only tries it).
    std::atomic<bool> patternLock { false };
    std::vector<bool> cachedPattern, cachedCPattern;
    bool              patternUpdated = false;
    uint8_t           pendingKind    = kNone;
    int               cachedOrigin   = 0;
    uint32_t          cachedEpoch    = 0;

    // Audio-thread state (reserved to 256 steps, so assign() never allocates).
    std::vector<bool> safePattern, safeCPattern;
    int  lastStepIndex       = -1;
    int  lastAccentStepIndex = 0;
    int  phaseOrigin         = 0;
    bool wasLastStepHit      = false;
    bool stepSounded         = false;
    std::atomic<int> boundaryEff { 0 };
    std::vector<bool> scratchPatA, scratchPatB, scratchEuclid, scratchEuclidC;

    void rebuildCachedLocked(Rhythm& r, int stepCap);
    static int floorMod(int a, int n) noexcept { const int m = a % n; return m < 0 ? m + n : m; }
    static void ensureMinPatternLength(std::vector<bool>& pattern)
    {
        if (pattern.size() == 1)
            pattern.push_back(pattern.front());
    }
};

} // namespace mu_clid
