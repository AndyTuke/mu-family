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

    // Set filter type by integer code (matches APVTS values 0..14).
    void setType(int typeCode) noexcept     { typeCodeValue = typeCode; }
    void setCutoff(float hz) noexcept       { cutoffHz = hz; }
    void setResonance(float r) noexcept     { resonance = r; }

    // Apply the configured filter in-place to the buffer (first numChannels channels,
    // numSamples samples). Allocation-free.
    void process(juce::AudioBuffer<float>& buffer, int numSamples, int numChannels);

private:
    double currentSampleRate = 44100.0;
    int    typeCodeValue     = 0;
    float  cutoffHz          = 1000.0f;
    float  resonance         = 0.1f;

    juce::dsp::StateVariableTPTFilter<float> svf;
    juce::dsp::LadderFilter<float>           ladder;
    OnePoleLP    lp6[MaxChannels];
    OnePoleHP    hp6[MaxChannels];
    BiquadFilter eq [MaxChannels];

    std::vector<float> combBuffer[MaxChannels];
    int                combWritePos[MaxChannels] = { 0, 0 };

    juce::AudioBuffer<float> notchScratch;

    void configureForCurrentType();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MultiModeFilter)
};
