#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

// FmPercEngine — mu-On's sixth engine: toms and techno percussion by phase-modulation FM, one voice with an
// Algorithm choice (Tom, Conga, Zap, Click, Clank, Wood, Pluck, Burst) and ten fixed knobs. Tune and Ratio are
// absolute (Hz and a ratio); the other eight are normalised 0..1 amounts each algorithm scales through its own table.
// The algorithm is latched at the trigger, so changing it never cuts a ringing tail.
//
// This is the plumbing stage: the interface is final and the voice renders silence until the operators land.
// Allocation-free after prepare().
namespace mu_on
{

class FmPercEngine
{
public:
    void prepare(double sr, int maxBlock)
    {
        sampleRate   = sr > 0.0 ? sr : 44100.0;
        maxBlockSize = juce::jmax(1, maxBlock);
        reset();
    }

    // This block's knobs, in the order of kFmPercDests (MuOnModDest.h), with the Algorithm choice (not a target).
    void setParams(int algorithmIndex, float tuneHz, float ratio, float index, float indexDecay, float feedback,
                   float dive, float diveTime, float decay, float fold, float noise) noexcept
    {
        algorithm = algorithmIndex;
        tune = tuneHz; modRatio = ratio; fmIndex = index; fmEnv = indexDecay; fdbk = feedback;
        diveAmount = dive; diveSpeed = diveTime; ampDecay = decay; foldAmount = fold; noiseAmount = noise;
    }

    // Fire the voice at `onset` samples into the block; velocity scales the level, Index and Dive.
    void trigger(float velocity, int onset = 0) noexcept
    {
        latchedAlgorithm = algorithm;
        pendingVel       = velocity;
        pendingOnset     = juce::jmax(0, onset);
    }

    // Silence the voice at once. Allocation-free.
    void reset() noexcept { active = false; pendingVel = 0.0f; pendingOnset = 0; }

    // Add the voice's block into `buf`. An idle voice returns at once.
    void render(juce::AudioBuffer<float>& buf, int n) noexcept
    {
        juce::ignoreUnused(buf, n);
        if (! active) return;
    }

private:
    double sampleRate = 44100.0;
    int    maxBlockSize = 512;
    bool   active = false;

    int   algorithm = 0, latchedAlgorithm = 0;
    float tune = 90.0f, modRatio = 1.5f, fmIndex = 0.0f, fmEnv = 0.0f, fdbk = 0.0f;
    float diveAmount = 0.0f, diveSpeed = 0.0f, ampDecay = 0.0f, foldAmount = 0.0f, noiseAmount = 0.0f;
    float pendingVel = 0.0f;
    int   pendingOnset = 0;
};

} // namespace mu_on
