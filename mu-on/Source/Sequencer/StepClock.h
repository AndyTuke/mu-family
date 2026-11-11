#pragma once

#include "Sequencer/StepRow.h"
#include <cmath>
#include <cstdint>
#include <limits>
#include <type_traits>

// The 909 step clock of one row: it fires sample-accurate triggers at each 16th-note step boundary of the
// host/internal transport beat. 16 steps = one bar (each step = a 16th = 0.25 beat); a row may be longer (its numSteps, up to 64). Swing delays the odd
// 16ths; accented cells fire at accentedVelocity(the row's Accent knob). A Track owns one and clocks its own row with the
// beat it renders at. Pure logic, so it is unit-tested headless.
namespace mu_on
{

// How a layer walks its row: the time one step lasts (its Speed), the bars after which it starts over with every other layer
// (the sequence's Reset; 0 = never), and the order it plays its steps in. The defaults are the plain 909 clock: 16ths, no reset,
// forwards. `seed` makes Random repeatable (the same pass and slot always pick the same step).
struct ClockSetup
{
    static constexpr int kForwards = 0, kBackwards = 1, kRandom = 2;
    double   stepBeats  = 0.25;
    double   resetBeats = 0.0;
    int      mode       = kForwards;
    uint32_t seed       = 0;
};

// What the clock hands a Track for one step that fires: its velocity, where in the block, the Hat's Open flag, its
// probability (0..1), whether it is tied to the step before, how many samples the note is held, its transposition
// in semitones, and which step of which pass of the row it is (the pass seeds the probability draw).
struct StepEvent
{
    float velocity; int offset; bool open; float probability; bool tied; int gateSamples; float semitones; int step; long long pass;
    ChordHit chord;   // a Chord track's step: chord, inversion, spread, root degree and octave
    // The step's repeats: how many extra hits follow, the beats between them, the Fade across them, and the beat of the next
    // step that is on (a repeat run ends before it). `velocity` already carries the step's Level.
    int repeats = 0; double repeatBeats = 0.25; int repeatFade = 0; double limitBeat = 0.0;
};

class StepClock
{
public:
    static constexpr double kStepBeats = 0.25;   // a 16th note
    static constexpr float  kNormalVelocity = 0.72f;

    // Velocity of an accented cell for the Accent knob (0..1): from a plain cell's velocity at 0 up to full at 1,
    // so an accent is never quieter than the plain steps beside it.
    static constexpr float accentedVelocity(float accentKnob) noexcept
    {
        return kNormalVelocity + accentKnob * (1.0f - kNormalVelocity);
    }

    // A row's length in steps: its numSteps, or 16 for anything that is not a StepRow.
    template <class Row>
    static auto lengthOf(const Row& r, int) noexcept -> decltype(r.numSteps(), int()) { return r.numSteps(); }
    template <class Row>
    static int lengthOf(const Row&, long) noexcept { return StepRow::kNumSteps; }

    // The pass (one run through a row's `length` steps) a beat falls in, and whether step `step` plays in it: the step's loop is
    // Length bars long and its Bars mask names the bars it plays in (a loop of 6 with bars 1, 2 and 5 ticked plays in passes
    // 1, 2 and 5 of every six).
    static long long passOfBeat(double beat, int length = StepRow::kNumSteps) noexcept { return (long long) std::floor(beat / (kStepBeats * length)); }
    template <class Row>
    static bool loopPlays(const Row& row, int step, long long pass) noexcept
    {
        const int bars = row.stepLoopLength(step);
        if (bars <= 1) return true;
        const int bar = (int) (((pass % bars) + bars) % bars);   // 0-based, whole numbers also for a negative pass
        return (row.stepLoopBars(step) >> bar & 1) != 0;
    }

    // A ghost step's velocity (the Ghost level's share of a normal note) for a row that has ghosts; any other row keeps `v`.
    template <class Row>
    static auto ghostAdjusted(const Row& r, int step, float v, int) noexcept -> decltype(r.isGhost(step), r.ghostLevel(), float())
    {
        return r.isGhost(step) ? kNormalVelocity * r.ghostLevel() : v;
    }
    template <class Row>
    static float ghostAdjusted(const Row&, int, float v, long) noexcept { return v; }

    // A step's Level (0..1), and its repeat settings, for a row that has them; any other row plays every step at full level, once.
    template <class Row>
    static auto levelOf(const Row& r, int step, int) noexcept -> decltype(r.stepLevel(step), float()) { return (float) r.stepLevel(step) * 0.01f; }
    template <class Row>
    static float levelOf(const Row&, int, long) noexcept { return 1.0f; }
    template <class Row>
    static auto repeatsOf(const Row& r, int step, int) noexcept -> decltype(r.stepRepeats(step), int()) { return r.stepRepeats(step); }
    template <class Row>
    static int repeatsOf(const Row&, int, long) noexcept { return 0; }
    template <class Row>
    static auto repeatRateOf(const Row& r, int step, int) noexcept -> decltype(r.stepRepeatRate(step), int()) { return r.stepRepeatRate(step); }
    template <class Row>
    static int repeatRateOf(const Row&, int, long) noexcept { return step_repeats::kDefaultRate; }
    template <class Row>
    static auto repeatFadeOf(const Row& r, int step, int) noexcept -> decltype(r.stepRepeatFade(step), int()) { return r.stepRepeatFade(step); }
    template <class Row>
    static int repeatFadeOf(const Row&, int, long) noexcept { return 0; }

    // Which row step plays at timeline slot `slot` (0..length-1) of pass `pass`: Forwards plays the row in order, Backwards from
    // the end, Random any step (a hash of seed, pass and slot, so it is the same every time it is asked).
    static int playedStep(int slot, long long pass, int length, int mode, uint32_t seed) noexcept
    {
        if (mode == ClockSetup::kBackwards) return length - 1 - slot;
        if (mode == ClockSetup::kRandom)
        {
            uint32_t x = seed ^ (uint32_t) (pass * 2654435761ll) ^ ((uint32_t) slot * 0x9E3779B9u);
            x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15; x *= 0x846ca68bu; x ^= x >> 16;
            return (int) (x % (uint32_t) length);
        }
        return slot;
    }

    // The pass (one run through `length` steps) of step count `k` in reset period `period`; it keeps counting across resets, so
    // conditions and probability keep moving.
    static long long passOf(long long k, long long period, int length, const ClockSetup& c) noexcept
    {
        const long long floorDiv = k >= 0 ? k / length : -((-k + length - 1) / length);
        const long long perPeriod = c.resetBeats > 0.0 ? (long long) std::ceil(c.resetBeats / (c.stepBeats * length) - 1.0e-9) : 0;
        return floorDiv + period * perPeriod;
    }

    // Where a beat falls on a layer's own timeline: the reset period, the step count from the period's start, the pass, the slot
    // within the row's length and the row step that plays there. This is what the grid's playhead shows.
    struct Position { long long period = 0; long long k = 0; long long pass = 0; int slot = 0; int step = 0; };
    static Position positionAt(double beat, int length, const ClockSetup& c) noexcept
    {
        Position p;
        length = length > 0 ? length : 1;
        const double origin = c.resetBeats > 0.0 ? std::floor(beat / c.resetBeats) * c.resetBeats : 0.0;
        p.period = c.resetBeats > 0.0 ? (long long) std::floor(beat / c.resetBeats) : 0;
        p.k = (long long) std::floor((beat - origin) / c.stepBeats + 1.0e-9);
        p.slot = localStep(p.k, length);
        p.pass = passOf(p.k, p.period, length, c);
        p.step = playedStep(p.slot, p.pass, length, c.mode, c.seed);
        return p;
    }

    // The beat of the next step after timeline slot `slot` that is on (in the order the layer plays, across the row's wrap, at most
    // one pass), which ends a repeat run; one pass later if none is.
    template <class Row>
    static double nextOnStepBeat(const Row& row, int slot, long long pass, int length, double stepBeatNow, const ClockSetup& c) noexcept
    {
        for (int ahead = 1; ahead <= length; ++ahead)
        {
            const int s = playedStep(localStep(slot + ahead, length), pass + (slot + ahead) / length, length, c.mode, c.seed);
            if (row.isOn(s)) return stepBeatNow + (double) ahead * c.stepBeats;
        }
        return stepBeatNow + (double) length * c.stepBeats;
    }

    // The clock keeps no position of its own now (it reads it from the beat), so these stay only for callers that still announce a
    // start or a jump.
    void reset() noexcept {}
    void seek(double) noexcept {}

    // How this clock walks its row (a Track sets it each block from its Speed, Play Mode and the sequence's Reset).
    void setup(const ClockSetup& s) noexcept { cfg = s; }
    const ClockSetup& getSetup() const noexcept { return cfg; }

    // Fire every on-step of `row` (a StepRow, or anything with its isOn / isAccent) whose (swing-adjusted) beat lands in the block [beatStart, beatStart +
    // numSamples x beatsPerSample): fire(velocity, sampleOffsetWithinBlock). `swing` 0..0.9, `accentVelocity`
    // the Accent knob 0..1 (StepRow::setGroove's ranges). The caller advances the transport itself; this only emits triggers.
    // Under MIDI clock the transport hands each block the exact span the clock's beat model moved (not always
    // tempo x length); using it keeps consecutive blocks contiguous, so no step falls in a gap.
    // Audio thread: no allocation, `fire` is called inline.
    // A step's loop (the row's own, when the Row has one) picks which passes (runs through the row, from the transport
    // start) it plays: its Length in loops and the ones ticked within it. A firing that takes more arguments gets the step's Open flag
    // and its probability (0..1), which the caller combines with the layer's and draws against; one that takes a StepEvent gets all
    // of it: tie, hold, transposition and which pass and step it is.
    template <class Row, class Fire>
    void processSpan(const Row& row, double beatStart, int numSamples, double beatsPerSample,
                     float swing, float accentVelocity, Fire&& fire) const noexcept
    {
        if (numSamples <= 0 || beatsPerSample <= 0.0 || cfg.stepBeats <= 0.0) return;

        const double beatEnd = beatStart + beatsPerSample * (double) numSamples;
        const double sb = cfg.stepBeats;
        const int length = juce::jmax(1, lengthOf(row, 0));

        // The block's steps, period by period (one period without a Reset): a step lands at its slot's beat, an odd slot pushed
        // later by swing, and a step that would land after the period's end is not played (the layer starts over there).
        // The start edge has a 1e-9 beat tolerance so a host reporting a block start an ulp past the previous block's end does not skip the step.
        constexpr double kBeatEpsilon = 1e-9;
        const long long firstPeriod = cfg.resetBeats > 0.0 ? (long long) std::floor(beatStart / cfg.resetBeats) : 0;
        const long long lastPeriod  = cfg.resetBeats > 0.0 ? (long long) std::floor((beatEnd - kBeatEpsilon) / cfg.resetBeats) : 0;
        int guard = 0;
        for (long long period = firstPeriod; period <= lastPeriod && guard < StepRow::kMaxSteps * 4; ++period)
        {
            const double origin = cfg.resetBeats > 0.0 ? (double) period * cfg.resetBeats : 0.0;
            const double periodEnd = cfg.resetBeats > 0.0 ? origin + cfg.resetBeats : std::numeric_limits<double>::infinity();
            long long k = (long long) std::floor((beatStart - origin) / sb) - 1;   // one step early: a swung step of the one before lands here
            if (cfg.resetBeats > 0.0) k = juce::jmax(0ll, k);
            for (; guard < StepRow::kMaxSteps * 4; ++k, ++guard)
            {
                const int slot = localStep(k, length);
                const double t = origin + (double) k * sb + ((slot & 1) ? (double) swing * 0.5 * sb : 0.0);
                if (t >= beatEnd || t >= periodEnd) break;
                if (t < beatStart - kBeatEpsilon) continue;
                const int sampleOffset = juce::jlimit(0, numSamples - 1, (int) std::round((t - beatStart) / beatsPerSample));
                const long long pass = passOf(k, period, length, cfg);
                const int local = playedStep(slot, pass, length, cfg.mode, cfg.seed);   // the row step that plays in this slot
                const bool passPlays = loopPlays(row, local, pass);
                if (passPlays && row.isOn(local))
                {
                    float velocity = row.isAccent(local) ? accentedVelocity(accentVelocity) : kNormalVelocity;
                    velocity = ghostAdjusted(row, local, velocity, 0);   // a ghost step: a share of the normal note
                    velocity *= levelOf(row, local, 0);                  // the step's own Level
                    // A tied run: how long the note is held (to the end of the last step tied to this one), for a Bass's release. Ties
                    // follow the row's order, so only a layer playing forwards has them.
                    const bool forwards = cfg.mode == ClockSetup::kForwards;
                    int runSteps = 1;
                    while (forwards && local + runSteps < length && row.tiedFrom(local + runSteps)) ++runSteps;
                    if constexpr (std::is_invocable_v<Fire&, const StepEvent&>)
                        fire(StepEvent { velocity, sampleOffset, row.isOpen(local), (float) row.stepProb(local) * 0.01f, forwards && row.tiedFrom(local),
                                         (int) (runSteps * sb / beatsPerSample), (float) row.stepSemitones(local), local, pass, row.stepChordHit(local),
                                         repeatsOf(row, local, 0), step_repeats::rateBeats(repeatRateOf(row, local, 0)), repeatFadeOf(row, local, 0),
                                         nextOnStepBeat(row, slot, pass, length, t, cfg) });
                    else if constexpr (std::is_invocable_v<Fire&, float, int, bool, float>) fire(velocity, sampleOffset, row.isOpen(local), (float) row.stepProb(local) * 0.01f);
                    else if constexpr (std::is_invocable_v<Fire&, float, int, bool>)   fire(velocity, sampleOffset, row.isOpen(local));
                    else                                                                fire(velocity, sampleOffset);
                }
            }
        }
    }

    // Step (0..length-1) the playhead sits in for a given beat (the grid's playhead column).
    static int currentStep(double beat, int length = StepRow::kNumSteps) noexcept
    {
        return localStep((long long) std::floor(beat / kStepBeats), length);
    }

    // Step (0..length-1) of global step `g`.
    static int localStep(long long g, int length = StepRow::kNumSteps) noexcept
    {
        return (int) (((g % length) + length) % length);
    }

    // Beat at which global step `g` fires (odd local steps pushed later by swing).
    static double stepBeat(long long g, float swing, int length = StepRow::kNumSteps) noexcept
    {
        const double swingOffset = (localStep(g, length) & 1) ? (double) swing * 0.5 * kStepBeats : 0.0;
        return (double) g * kStepBeats + swingOffset;
    }

private:
    ClockSetup cfg;
};

} // namespace mu_on
