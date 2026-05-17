#pragma once
#include <juce_dsp/juce_dsp.h>
#include "AudioFilters.h"
#include "VoiceParams.h"

// Self-contained insert-effect processor used by both VoiceEngine (per-rhythm)
// and MixerEngine (master bus). Owns all mutable DSP state; parameters are
// passed in as a const VoiceParams& each block so there is no internal copy
// to keep in sync.
class InsertProcessor
{
public:
    void prepare(double sampleRate, int blockSize);
    void reset();

    // Apply insert effect (driveChar switch + post-drive tone filter) in-place.
    void process(juce::AudioBuffer<float>& buf, int ns, int nCh, const VoiceParams& p);

    // Peak gain-reduction for the current block, 0..1 (1 ≡ 24 dB).
    // Written by the audio thread inside process(); read by the UI at 30 Hz.
    // Non-zero only during Compressor (7) / Limiter (8) modes.
    std::atomic<float> grReduction { 0.0f };

private:
    double currentSampleRate = 44100.0;

    float     prevDriveX[2]     = {};
    OnePoleLP bitAaFilter[2];
    float     bitRateCounter[2] = {};
    float     bitRateHeld[2]    = {};
    juce::Random rng;
    OnePoleLP toneFilter[2];
    float     compEnvelope[2]   = {};
    float     ringPhase[2]      = {};   // ring mod: per-channel carrier phase
    float     dcBlockIn[2]      = {};   // tape sat: DC block input state
    float     dcBlockOut[2]     = {};   // tape sat: DC block output state

    using EqFilter = juce::dsp::ProcessorDuplicator<juce::dsp::IIR::Filter<float>,
                                                     juce::dsp::IIR::Coefficients<float>>;
    EqFilter eqLow, eqMid, eqHigh;
    float eqLastDriveDrive = -1.0f;
    float eqLastDrvDither  = -1.0f;
    float eqLastMidGain    = -999.0f;
    float eqLastDriveTone  = -1.0f;
};
