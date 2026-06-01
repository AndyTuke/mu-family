#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include "Audio/Filters/FilterAlgorithmBase.h"
#include "Audio/Filters/Hp24Filter.h"

#include <array>
#include <memory>

// Reusable multi-mode filter wrapping 16 distinct algorithms. Designed to
// drop into any plugin needing a flexible filter slot. Owns all DSP state
// and scratch buffers; the caller only needs to supply sample rate, block
// size, channel count, and per-block params.
//
// Filter type codes (kept identical to the legacy VoiceEngine int codes so
// the APVTS choice parameter does not need remapping):
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
//
// implementation refactored from a single 200-line switch in
// MultiModeFilter.cpp into per-algorithm classes living in
// Source/Audio/Processing/Filters/. MultiModeFilter is now a thin orchestrator
// that pre-allocates one instance of each algorithm and dispatches via a
// fixed-index table. Each algorithm owns its own DSP state. Public API is
// unchanged so call sites (VoiceEngine, etc.) need no edits.
class MultiModeFilter
{
public:
    static constexpr int MaxChannels   = 2;
    static constexpr int kNumFilterAlgos = 16;

    MultiModeFilter();

    void prepare(double sampleRate, int blockSize, int numChannels);
    void reset();

    // Set filter type by integer code (matches APVTS values 0..15).
    void setType(int typeCode) noexcept     { typeCodeValue = juce::jlimit(0, kNumFilterAlgos - 1, typeCode); }
    void setCutoff(float hz) noexcept       { cutoffHz = hz; }
    void setResonance(float r) noexcept     { resonance = r; }
    // Pre-filter valve saturation depth (0 = bypass, 1 = full warmth).
    void setDrive(float d) noexcept         { smoothedDrive.setTargetValue(juce::jlimit(0.0f, 1.0f, d)); }
    // Post-filter 4-pole high-pass cutoff in Hz (0 = bypass).
    void setLowCut(float hz) noexcept       { smoothedLowCutHz.setTargetValue(juce::jmax(0.0f, hz)); }

    // Apply the configured filter in-place to the buffer (first numChannels
    // channels, numSamples samples). Allocation-free.
    void process(juce::AudioBuffer<float>& buffer, int numSamples, int numChannels);

private:
    int    typeCodeValue = 0;
    float  cutoffHz      = 1000.0f;
    float  resonance     = 0.1f;

    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedDrive;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedLowCutHz;
    Hp24Filter lowCutFilter;

    std::array<std::unique_ptr<FilterAlgorithmBase>, kNumFilterAlgos> algorithms;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MultiModeFilter)
};
