#pragma once

#include "Sequencer/StepRow.h"
#include <cmath>
#include <type_traits>

// The 909 step clock of one row: it fires sample-accurate triggers at each 16th-note step boundary of the
// host/internal transport beat. 16 steps = one bar (each step = a 16th = 0.25 beat). Swing delays the odd
// 16ths; accented cells fire at accentedVelocity(the row's Accent knob). A Track owns one and clocks its own row with the
// beat it renders at. Pure logic, so it is unit-tested headless.
namespace mu_on
{

// What the clock hands a Track for one step that fires: its velocity, where in the block, the Hat's Open flag, its
// probability (0..1), whether it is tied to the step before, how many samples the note is held, its transposition
// in semitones, and which step of which pass of the row it is (the pass seeds the probability draw).
struct StepEvent
{
    float velocity; int offset; bool open; float probability; bool tied; int gateSamples; float semitones; int step; long long pass;
    ChordHit chord;   // a Chord track's step: chord, inversion, spread, root degree and octave
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

    // The pass (one run through the 16 steps) a beat falls in, and whether step `step`'s loop ratio plays in it
    // ("N:M" plays the Nth of every M passes).
    static long long passOfBeat(double beat) noexcept { return (long long) std::floor(beat / (kStepBeats * StepRow::kNumSteps)); }
    template <class Row>
    static bool loopPlays(const Row& row, int step, long long pass) noexcept
    {
        const auto ratio = kLoopRatios[(size_t) row.stepLoop(step)];
        return ratio.of <= 1 || (((pass % ratio.of) + ratio.of) % ratio.of) == ratio.play - 1;
    }

    // A ghost step's velocity (the Ghost level's share of a normal note) for a row that has ghosts; any other row keeps `v`.
    template <class Row>
    static auto ghostAdjusted(const Row& r, int step, float v, int) noexcept -> decltype(r.isGhost(step), r.ghostLevel(), float())
    {
        return r.isGhost(step) ? kNormalVelocity * r.ghostLevel() : v;
    }
    template <class Row>
    static float ghostAdjusted(const Row&, int, float v, long) noexcept { return v; }

    // Realign to the top of the pattern.
    void reset() noexcept { nextGlobalStep = 0; }

    // Realign to the step the playhead sits in (call when the transport starts playing, wherever it starts):
    // a stop and replay inside one 16th otherwise lands inside the resync window and drops that step.
    void seek(double beat) noexcept { nextGlobalStep = (long long) std::floor(beat / kStepBeats); }

    // Fire every on-step of `row` (a StepRow, or anything with its isOn / isAccent) whose (swing-adjusted) beat lands in the block [beatStart, beatStart +
    // numSamples x beatsPerSample): fire(velocity, sampleOffsetWithinBlock). `swing` 0..0.9, `accentVelocity`
    // the Accent knob 0..1 (StepRow::setGroove's ranges). The caller advances the transport itself; this only emits triggers.
    // Under MIDI clock the transport hands each block the exact span the clock's beat model moved (not always
    // tempo x length); using it keeps consecutive blocks contiguous, so no step falls in a gap.
    // Audio thread: no allocation, `fire` is called inline.
    // A step's loop ratio (the row's own, when the Row has one) picks which passes (16 steps, from the transport
    // start) it plays: "N:M" plays the Nth of every M. A firing that takes more arguments gets the step's Open flag
    // and its probability (0..1), which the caller combines with the layer's and draws against; one that takes a StepEvent gets all
    // of it: tie, hold, transposition and which pass and step it is.
    template <class Row, class Fire>
    void processSpan(const Row& row, double beatStart, int numSamples, double beatsPerSample,
                     float swing, float accentVelocity, Fire&& fire) noexcept
    {
        if (numSamples <= 0 || beatsPerSample <= 0.0) return;

        const double beatEnd = beatStart + beatsPerSample * (double) numSamples;

        // Resync if the transport jumped (host scrub / a missed reset) so we never burst-fire
        // a long backlog: snap nextGlobalStep to just before the current block.
        const long long expected = (long long) std::floor(beatStart / kStepBeats);
        if (nextGlobalStep < expected - 1 || nextGlobalStep > expected + 1)
            nextGlobalStep = expected;

        // Fire each step whose (swing-adjusted) beat lands within [beatStart, beatEnd). The start edge has a 1e-9
        // beat tolerance so a host reporting a block start an ulp past the previous block's end does not skip the step.
        constexpr double kBeatEpsilon = 1e-9;
        for (int guard = 0; guard < StepRow::kNumSteps * 2; ++guard)
        {
            const double t = stepBeat(nextGlobalStep, swing);
            if (t >= beatEnd) break;
            if (t >= beatStart - kBeatEpsilon)
            {
                const int local = localStep(nextGlobalStep);
                const int sampleOffset = juce::jlimit(0, numSamples - 1,
                                                      (int) std::round((t - beatStart) / beatsPerSample));
                const long long pass = nextGlobalStep >= 0 ? nextGlobalStep / StepRow::kNumSteps
                                                           : -((-nextGlobalStep + StepRow::kNumSteps - 1) / StepRow::kNumSteps);
                const bool passPlays = loopPlays(row, local, pass);
                if (passPlays && row.isOn(local))
                {
                    float velocity = row.isAccent(local) ? accentedVelocity(accentVelocity) : kNormalVelocity;
                    velocity = ghostAdjusted(row, local, velocity, 0);   // a ghost step: a share of the normal note
                    // A tied run: how long the note is held (to the end of the last step tied to this one), for a Bass's release.
                    int runSteps = 1;
                    while (local + runSteps < StepRow::kNumSteps && row.tiedFrom(local + runSteps)) ++runSteps;
                    if constexpr (std::is_invocable_v<Fire&, const StepEvent&>)
                        fire(StepEvent { velocity, sampleOffset, row.isOpen(local), (float) row.stepProb(local) * 0.01f, row.tiedFrom(local),
                                         (int) (runSteps * kStepBeats / beatsPerSample), (float) row.stepSemitones(local), local, pass, row.stepChordHit(local) });
                    else if constexpr (std::is_invocable_v<Fire&, float, int, bool, float>) fire(velocity, sampleOffset, row.isOpen(local), (float) row.stepProb(local) * 0.01f);
                    else if constexpr (std::is_invocable_v<Fire&, float, int, bool>)   fire(velocity, sampleOffset, row.isOpen(local));
                    else                                                                fire(velocity, sampleOffset);
                }
            }
            ++nextGlobalStep;
        }
    }

    // Local step (0..15) the playhead sits in for a given beat (the grid's playhead column).
    static int currentStep(double beat) noexcept
    {
        return localStep((long long) std::floor(beat / kStepBeats));
    }

    // Local step (0..15) of global step `g`.
    static int localStep(long long g) noexcept
    {
        return (int) (((g % StepRow::kNumSteps) + StepRow::kNumSteps) % StepRow::kNumSteps);
    }

    // Beat at which global step `g` fires (odd local steps pushed later by swing).
    static double stepBeat(long long g, float swing) noexcept
    {
        const double swingOffset = (localStep(g) & 1) ? (double) swing * 0.5 * kStepBeats : 0.0;
        return (double) g * kStepBeats + swingOffset;
    }

private:
    long long nextGlobalStep = 0;
};

} // namespace mu_on
