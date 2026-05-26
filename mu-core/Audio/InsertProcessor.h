#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <memory>
#include <atomic>
#include <vector>

#include "AudioFilters.h"
#include "VoiceParams.h"
#include "Audio/FX/Insert/InsertAlgorithmBase.h"

// Self-contained insert-effect processor used by both VoiceEngine (per-rhythm)
// and MixerEngine (master bus). Owns all DSP state; parameters are passed in
// as a const VoiceParams& each block so there is no internal copy to keep in
// sync.
//
// refactored from a single ~600-line switch statement into a dispatch
// table over insertAlgo → InsertAlgorithmBase*. Each insertAlgo code maps to a
// concrete subclass living in Source/Audio/Processing/InsertFX/. All algorithms are pre-
// allocated in the constructor so prepare() / param-change paths never heap-
// allocate — matches the FX-rack pattern fixed under #402.
//
// Compressor (insertAlgo = 7) and Limiter (insertAlgo = 8) share a single
// CompressorLimiterInsert instance: the dispatch table aliases both slots to
// the same object so switching between them preserves the envelope-follower
// state (bit-identical to the pre-refactor switch-case behaviour).
class InsertProcessor
{
public:
    InsertProcessor();

    void prepare(double sampleRate, int blockSize);
    void reset();

    // Apply the selected insert effect (insertAlgo dispatch + post-drive tone
    // filter for cases 0–5) in place.
    void process(juce::AudioBuffer<float>& buf, int ns, int nCh, const VoiceParams& p);

    // Peak gain-reduction for the current block, 0..1 (1 ≡ 24 dB).
    // Written by the audio thread inside process(); read by the UI at 30 Hz.
    // Non-zero only during Compressor (7) / Limiter (8) modes.
    std::atomic<float> grReduction { 0.0f };

    static constexpr int kNumInsertAlgos = 14;   // + Karplus + Vocoder + VocoderSt

private:

    // Ownership: 10 distinct algorithm instances (Comp + Lim share one). Held
    // in a vector reserved at construction time so the raw pointers stored in
    // `dispatch[]` below stay valid for the InsertProcessor's lifetime.
    std::vector<std::unique_ptr<InsertAlgorithmBase>> owned;

    // Dispatch: insertAlgo value (0..10) → pointer into `owned`. Indices 7 and
    // 8 alias the same CompressorLimiterInsert instance.
    std::array<InsertAlgorithmBase*, kNumInsertAlgos> dispatch { };

    double currentSampleRate = 44100.0;

    // Post-drive tone filter — runs after the algorithm dispatch ONLY for
    // insertAlgo < 6, where p.insertTone is interpreted as a 1-pole LP cutoff.
    // EQ (6), Compressor/Limiter (7, 8), RingMod (9), TapeSat (10) repurpose
    // insertTone for other meanings and skip this step. Lives here (not in any
    // single algorithm) because it's a property of the drive-family, not of
    // any one algorithm's DSP.
    OnePoleLP postDriveTone[2];
};
