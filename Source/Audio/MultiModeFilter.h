#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>
#include "AudioFilters.h"

#include <vector>

// Reusable multi-mode filter wrapping a SVF, JUCE LadderFilter, 1-pole IIRs,
// biquads, and a feedback comb. Designed to drop into any plugin needing a
// 16-type filter slot. Owns all DSP state and scratch buffers; the caller only
// needs to supply sample rate, block size, channel count, and per-block params.
//
// Filter type codes (kept identical to the legacy VoiceEngine int codes so the
// APVTS choice parameter does not need remapping):
//   0  LP12       (SVF lowpass)
//   1  HP12       (SVF highpass)
//   2  BP12       (SVF bandpass)
//   3  Notch12    (SVF bandpass, dry − BP)
//   4  LP24       (JUCE LadderFilter LPF24)
//   5  HP24       (JUCE LadderFilter HPF24)
//   6  BP24       (JUCE LadderFilter BPF24)
//   7  LP6        (1-pole IIR low-pass per channel)
//   8  Comb+      (feedback comb, positive feedback — peaks at f0, 2f0, 3f0…)
//   9  AP12       (biquad all-pass)
//  10  Notch24    (LadderFilter BPF24, dry − BP)
//  11  HP6        (1-pole IIR high-pass per channel)
//  12  Peak       (biquad peak/bell, +12 dB)
//  13  LowShelf   (biquad low shelf, +12 dB)
//  14  HighShelf  (biquad high shelf, +12 dB)
//  15  Comb-      (feedback comb, negative feedback — peaks at f0/2, 3f0/2, 5f0/2…)
class MultiModeFilter
{
public:
    static constexpr int MaxChannels = 2;

    MultiModeFilter() = default;

    void prepare(double sampleRate, int blockSize, int numChannels);
    void reset();

    // Set filter type by integer code (matches APVTS values 0..15).
    void setType(int typeCode) noexcept     { typeCodeValue = typeCode; }
    void setCutoff(float hz) noexcept       { cutoffHz = hz; }
    void setResonance(float r) noexcept     { resonance = r; }
    // Filter gain in dB. Only used by Peak (12) / LowShelf (13) / HighShelf (14) modes.
    // Other types ignore this. Range -18..+18 dB; 0 = flat (peak/shelf has no effect).
    void setGainDb(float gainDb) noexcept   { gainDbValue = gainDb; }

    // Apply the configured filter in-place to the buffer (first numChannels channels,
    // numSamples samples). Allocation-free.
    void process(juce::AudioBuffer<float>& buffer, int numSamples, int numChannels);

private:
    double currentSampleRate = 44100.0;
    int    typeCodeValue     = 0;
    float  cutoffHz          = 1000.0f;
    float  resonance         = 0.1f;
    float  gainDbValue       = 0.0f;

    // Per-sample cutoff smoother. Block-rate cutoff updates produce audible
    // staircase artifacts during fast envelope sweeps (e.g. 10 ms attacks);
    // SmoothedValue<Multiplicative> ramps geometrically inside each block so
    // the SVF/Ladder coefficient updates trace a smooth log-frequency curve.
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> smoothedCutoff;

    juce::dsp::StateVariableTPTFilter<float> svf;
    juce::dsp::LadderFilter<float>           ladder;
    OnePoleLP    lp6[MaxChannels];
    OnePoleHP    hp6[MaxChannels];
    BiquadFilter eq [MaxChannels];
    // DC blocker on the comb feedback path — without it, DC bias on the input
    // accumulates through the feedback loop (1 / (1 - g) gain at g→1) and can
    // drift the buffer toward clipping on long sustains. 15 Hz HP, below the
    // lowest reachable comb fundamental (~20 Hz at minimum cutoff).
    OnePoleHP    combDcBlocker[MaxChannels];

    std::vector<float> combBuffer[MaxChannels];
    int                combWritePos[MaxChannels] = { 0, 0 };

    juce::AudioBuffer<float> notchScratch;

    // Coefficient-cache state for the biquad path (types 9, 12–14). The biquad
    // setX helpers each call pow/cos/sin/sqrt — re-running them every block
    // wastes cycles when cutoff/res/type/gain haven't changed.
    float eqLastCutoff = -1.0f;
    float eqLastRes    = -1.0f;
    float eqLastGain   = -999.0f;
    int   eqLastType   = -1;

    void configureForCurrentType();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MultiModeFilter)
};
