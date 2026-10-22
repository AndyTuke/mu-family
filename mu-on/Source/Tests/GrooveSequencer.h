#pragma once

#include "Sequencer/StepClock.h"
#include "Sequencer/StepPattern.h"
#include <array>
#include <functional>

// The whole-grid clock the sequencer tests drive: every row of a StepPattern clocked by its own StepClock (the
// arithmetic each Track runs on its own row), firing the live rows. Test support only: nothing that ships uses
// it (a Track clocks its own row; the step rows' playhead uses StepClock::currentStep).
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

    // How many rows (from the top) fire: the live layer count. Rows above it are spare slots, still clocked.
    void setActiveTracks(int n)      noexcept { activeTracks = juce::jlimit(0, StepPattern::kNumTracks, n); }

    // Realign to the top of the pattern (call when the transport (re)starts from 0).
    void reset() noexcept { for (auto& c : clocks) c.reset(); }

    // Realign every row's clock to the step the playhead sits in (the transport started there).
    void seek(double beat) noexcept { for (auto& c : clocks) c.seek(beat); }

    // Fire every step boundary that falls in the block beginning at `beatStart`. The
    // caller advances the transport itself; this only emits triggers.
    void process(double beatStart, int numSamples, double bpm, const TriggerFn& fire);

    // The same, for a block whose rate is given directly (beats per sample); see StepClock::processSpan.
    void processSpan(double beatStart, int numSamples, double beatsPerSample, const TriggerFn& fire);

    // Local step (0..15) the playhead sits in for a given beat — for the UI grid.
    static int currentStep(double beat) noexcept { return StepClock::currentStep(beat); }

private:
    // A view of one pattern row as a StepRow-like source for StepClock.
    struct PatternRow
    {
        const StepPattern& p;
        int track;
        bool isOn(int s) const noexcept     { return p.isOn(track, s); }
        bool isAccent(int s) const noexcept { return p.isAccent(track, s); }
        bool tiedFrom(int) const noexcept   { return false; }
        int  stepSemitones(int) const noexcept { return 0; }
        int  stepLoop(int) const noexcept   { return 0; }   // every pass (the test support has no per-step loop ratio)
    };

    StepPattern& pattern;
    std::array<StepClock, StepPattern::kNumTracks> clocks;
    double    sampleRate    = 44100.0;
    float     swing         = 0.0f;
    float     accentVel     = 1.0f;
    int       activeTracks  = StepPattern::kNumTracks;
};

} // namespace mu_on
