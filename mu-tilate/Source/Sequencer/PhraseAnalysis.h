#pragma once

#include "Analysis/OnsetDetector.h"   // mu-core
#include "Persistence/PhraseParams.h"
#include "Sequencer/SliceList.h"

#include <juce_audio_basics/juce_audio_basics.h>

// The Phrase layer's detection step: the cached curve, and from it the slice markers for the layer's detection settings.
// Both are message-thread-safe pure functions; the curve is the slow one and runs on a worker.
namespace mu_tilate
{

// The detection settings as the layer's parameters hold them.
struct DetectSettings
{
    int   method      = 0;        // detectMethodNames(): Transients, Soft-tonal, Even
    float sensitivity = 0.5f;
    float minGapMs    = 50.0f;
    float preRollMs   = 2.0f;
    float lowCutHz    = 20.0f;
    float highCutHz   = 20000.0f;
    int   evenParts   = 8;        // the number of slices for the Even method
};

// Slow step (a worker): the novelty curve of `audio` for the method and band. Empty for the Even method (nothing to analyse).
mu_core::OnsetCurve analyseLoop(const juce::AudioBuffer<float>& audio, double sampleRate, const DetectSettings& s);

// Fast step: the slice list for `loopLength` samples from the curve and the settings. Hand-placed (pinned) markers of
// `previous` survive. The Even method divides the loop into evenParts slices and needs no curve.
SliceList detectSlices(const mu_core::OnsetCurve& curve, const juce::AudioBuffer<float>& audio,
                       const DetectSettings& s, const SliceList* previous = nullptr);

} // namespace mu_tilate
