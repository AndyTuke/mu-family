#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

// #427: abstract base for the 16 voice-filter algorithms. Mirrors the
// InsertFX / SendFX dispatch pattern (see #425/#426). Each concrete subclass
// implements ONE filter type code (see MultiModeFilter's dropdown order):
//   0  Lp12Filter         (SVF 12 dB/oct LP)
//   1  Hp12Filter         (SVF 12 dB/oct HP)
//   2  Bp12Filter         (SVF 12 dB/oct BP)
//   3  Notch12Filter      (SVF BP, dry − BP)
//   4  Lp24Filter         (Ladder 24 dB/oct LP)
//   5  Hp24Filter         (Ladder 24 dB/oct HP)
//   6  Bp24Filter         (Ladder 24 dB/oct BP)
//   7  Lp6Filter          (1-pole 6 dB/oct LP)
//   8  CombPlusFilter     (feedback comb, positive feedback)
//   9  Ap12Filter         (biquad all-pass)
//  10  Notch24Filter      (Ladder BP24, dry − BP)
//  11  Hp6Filter          (1-pole 6 dB/oct HP)
//  12  PeakFilter         (biquad peak / bell, +12 dB)
//  13  LowShelfFilter     (biquad low shelf, +12 dB)
//  14  HighShelfFilter    (biquad high shelf, +12 dB)
//  15  CombMinusFilter    (feedback comb, negative feedback — Karplus feel)
//
// Each algorithm owns its own DSP state (SVF instance, Ladder instance,
// biquad, comb buffer, etc.). MultiModeFilter pre-allocates one of each and
// dispatches via a fixed-index table. Only one algorithm is active per voice
// per block; the inactive ones hold state but consume no CPU.
//
// `cutoffHz` and `resonance` are passed per-call rather than via setters so
// the algorithm doesn't have to keep its own copy in sync. Coefficient-
// recompute skip (#368) is implemented inside each algorithm against its
// own cached last-value members.
class FilterAlgorithmBase
{
public:
    virtual ~FilterAlgorithmBase() = default;

    virtual void prepare(double sampleRate, int blockSize, int numChannels) = 0;
    virtual void reset() = 0;

    virtual void process(juce::AudioBuffer<float>& buf,
                         int numSamples, int numChannels,
                         float cutoffHz, float resonance) = 0;
};
