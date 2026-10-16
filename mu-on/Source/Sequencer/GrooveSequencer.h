#pragma once

#include "StepPattern.h"
#include <array>
#include <cmath>
#include <functional>

// The 909 step clock, one per row: it fires sample-accurate triggers at each 16th-note step boundary of the
// host/internal transport beat. 16 steps = one bar (each step = a 16th = 0.25 beat). Swing delays the odd
// 16ths; accented cells fire at the row's accent velocity. Pure logic, so it is unit-tested headless.
//
// StepClock is one row's cursor (a Track owns one and clocks its own row with the beat it renders at);
// GrooveSequencer clocks every row of a StepPattern with one StepClock each (the tests' whole-grid view).
// Both run the same arithmetic, so a row clocked alone fires exactly what the grid clock fired for it.
namespace mu_on
{

class StepClock
{
public:
    static constexpr double kStepBeats = 0.25;   // a 16th note
    static constexpr float  kNormalVelocity = 0.72f;

    // Realign to the top of the pattern.
    void reset() noexcept { nextGlobalStep = 0; }

    // Fire every on-step of `row` (a StepRow, or anything with its isOn / isAccent) whose (swing-adjusted) beat lands in the block [beatStart, beatStart +
    // numSamples x beatsPerSample): fire(velocity, sampleOffsetWithinBlock). `swing` 0..0.9, `accentVelocity`
    // 0..1 (StepRow::setGroove's ranges). The caller advances the transport itself; this only emits triggers.
    // Under MIDI clock the transport hands each block the exact span the clock's beat model moved (not always
    // tempo x length); using it keeps consecutive blocks contiguous, so no step falls in a gap.
    // Audio thread: no allocation, `fire` is called inline.
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

        // Fire each step whose (swing-adjusted) beat lands within [beatStart, beatEnd).
        for (int guard = 0; guard < StepPattern::kNumSteps * 2; ++guard)
        {
            const double t = stepBeat(nextGlobalStep, swing);
            if (t >= beatEnd) break;
            if (t >= beatStart)
            {
                const int local = localStep(nextGlobalStep);
                const int sampleOffset = juce::jlimit(0, numSamples - 1,
                                                      (int) std::round((t - beatStart) / beatsPerSample));
                if (row.isOn(local))
                    fire(row.isAccent(local) ? accentVelocity : kNormalVelocity, sampleOffset);
            }
            ++nextGlobalStep;
        }
    }

    // Local step (0..15) of global step `g`.
    static int localStep(long long g) noexcept
    {
        return (int) (((g % StepPattern::kNumSteps) + StepPattern::kNumSteps) % StepPattern::kNumSteps);
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

class GrooveSequencer
{
public:
    // fire(track, velocity 0..1, sampleOffsetWithinBlock)
    using TriggerFn = std::function<void(int track, float velocity, int sampleOffset)>;

    explicit GrooveSequencer(StepPattern& p) : pattern(p) {}

    void prepare(double sr) noexcept { sampleRate = sr > 0.0 ? sr : 44100.0; }
    void setSwing(float swing01)     noexcept { swing = juce::jlimit(0.0f, 0.9f, swing01); }
    void setAccentVelocity(float v)  noexcept { accentVel = juce::jlimit(0.0f, 1.0f, v); }

    // How many rows (from the top) fire: the live layer count. Rows above it are spare slots, still clocked.
    void setActiveTracks(int n)      noexcept { activeTracks = juce::jlimit(0, StepPattern::kNumTracks, n); }

    // Realign to the top of the pattern (call when the transport (re)starts from 0).
    void reset() noexcept { for (auto& c : clocks) c.reset(); }

    // Fire every step boundary that falls in the block beginning at `beatStart`. The
    // caller advances the transport itself; this only emits triggers.
    void process(double beatStart, int numSamples, double bpm, const TriggerFn& fire);

    // The same, for a block whose rate is given directly (beats per sample); see StepClock::processSpan.
    void processSpan(double beatStart, int numSamples, double beatsPerSample, const TriggerFn& fire);

    // Local step (0..15) the playhead sits in for a given beat — for the UI grid.
    static int currentStep(double beat) noexcept
    {
        return StepClock::localStep((long long) std::floor(beat / StepClock::kStepBeats));
    }

private:
    // A view of one pattern row as a StepRow-like source for StepClock.
    struct PatternRow
    {
        const StepPattern& p;
        int track;
        bool isOn(int s) const noexcept     { return p.isOn(track, s); }
        bool isAccent(int s) const noexcept { return p.isAccent(track, s); }
    };

    StepPattern& pattern;
    std::array<StepClock, StepPattern::kNumTracks> clocks;
    double    sampleRate    = 44100.0;
    float     swing         = 0.0f;
    float     accentVel     = 1.0f;
    int       activeTracks  = StepPattern::kNumTracks;
};

} // namespace mu_on
