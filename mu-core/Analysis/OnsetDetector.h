#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <vector>

// Onset (transient) detection for loops, used by the loop slicer and any later product that cuts audio at its
// attacks. Two steps, so a control that only changes the picking never repeats the analysis: computeCurve turns
// the audio into a cached detection curve (the slow step, run on a worker thread), pickOnsets turns the curve into
// sample positions (microseconds, run on the message thread while a knob moves). Never called on the audio thread.
namespace mu_core
{

enum class OnsetMethod
{
    SpectralFlux,   // Bello et al. 2005, Dixon 2006: the general-purpose detector, best for drums
    SuperFlux       // Boeck and Widmer 2013: a max-filtered reference frame, fewer false onsets on vibrato and tonal material
};

// The cached analysis of one loop. flux[i] is the novelty at frame i, scaled so the loudest frame is 1.
struct OnsetCurve
{
    std::vector<float> flux;
    int    hop        = 256;      // samples between frames
    double sampleRate = 48000.0;
    int    numSamples = 0;        // length of the analysed audio
    bool   empty() const noexcept { return flux.empty(); }
};

struct OnsetPickSettings
{
    float sensitivity = 0.5f;   // 0..1: higher picks more (lowers the threshold offset)
    float minGapMs    = 50.0f;  // no two onsets closer than this; the stronger one wins
    float preRollMs   = 2.0f;   // each position is moved this far before the peak so the attack is kept
    bool  snapToZero  = true;   // then to the nearest zero crossing, so a cut does not click
};

// Analyses `audio` (any channel count, mixed to mono) inside the band lowCutHz..highCutHz; 0 for either means no limit.
OnsetCurve computeCurve(const juce::AudioBuffer<float>& audio, double sampleRate, OnsetMethod method,
                        float lowCutHz = 0.0f, float highCutHz = 0.0f);

// Onset positions in source samples, ascending, from a cached curve. `audio` is used only for the zero-crossing snap.
std::vector<int> pickOnsets(const OnsetCurve& curve, const OnsetPickSettings& settings, const juce::AudioBuffer<float>* audio = nullptr);

} // namespace mu_core
