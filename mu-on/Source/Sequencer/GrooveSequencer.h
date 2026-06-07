#pragma once

#include "StepPattern.h"
#include <functional>

// GrooveSequencer — clocks the StepPattern off the host/internal transport beat and
// fires sample-accurate triggers at each 16th-note step boundary. 16 steps = one bar
// (each step = a 16th = 0.25 beat). Swing delays the odd 16ths; accented cells fire at
// a higher velocity. Pure logic (juce_audio_basics-free beyond ValueTree in the pattern),
// so it is unit-tested headless.
namespace mu_on
{

class GrooveSequencer
{
public:
    // fire(track, velocity 0..1, sampleOffsetWithinBlock)
    using TriggerFn = std::function<void(int track, float velocity, int sampleOffset)>;

    explicit GrooveSequencer(StepPattern& p) : pattern(p) {}

    void prepare(double sr) noexcept { sampleRate = sr > 0.0 ? sr : 44100.0; }
    void setSwing(float swing01)     noexcept { swing = juce::jlimit(0.0f, 0.9f, swing01); }
    void setAccentVelocity(float v)  noexcept { accentVel = juce::jlimit(0.0f, 1.0f, v); }

    // Realign to the top of the pattern (call when the transport (re)starts from 0).
    void reset() noexcept { nextGlobalStep = 0; }

    // Fire every step boundary that falls in the block beginning at `beatStart`. The
    // caller advances the transport itself; this only emits triggers.
    void process(double beatStart, int numSamples, double bpm, const TriggerFn& fire);

    // Local step (0..15) the playhead sits in for a given beat — for the UI grid.
    static int currentStep(double beat) noexcept
    {
        const long long g = (long long) std::floor(beat / kStepBeats);
        const int local = (int) (((g % StepPattern::kNumSteps) + StepPattern::kNumSteps) % StepPattern::kNumSteps);
        return local;
    }

private:
    static constexpr double kStepBeats = 0.25;   // a 16th note

    // Beat at which global step `g` fires (odd local steps pushed later by swing).
    double stepBeat(long long g) const noexcept
    {
        const int local = (int) (((g % StepPattern::kNumSteps) + StepPattern::kNumSteps) % StepPattern::kNumSteps);
        const double swingOffset = (local & 1) ? (double) swing * 0.5 * kStepBeats : 0.0;
        return (double) g * kStepBeats + swingOffset;
    }

    StepPattern& pattern;
    double    sampleRate    = 44100.0;
    float     swing         = 0.0f;
    float     accentVel     = 1.0f;
    float     normalVel     = 0.72f;
    long long nextGlobalStep = 0;
};

} // namespace mu_on
