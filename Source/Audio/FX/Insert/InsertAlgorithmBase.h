#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include "Audio/VoiceParams.h"

// abstract base for per-voice insert effect algorithms. Mirrors the
// FX-rack pattern in Source/Audio/Processing/SendFX/EffectAlgorithmBase.h but
// stays in-place (no oversampling, no send-mode wrapping) because inserts run
// inside the voice chain's per-sample inner loop.
//
// Each concrete algorithm is one driveChar code (see VoiceParams::driveChar):
//   1 = SoftClip, 2 = HardClip, 3 = Fold, 4 = Bitcrusher, 5 = Clipper,
//   6 = 3-Band EQ, 7 = Compressor, 8 = Limiter, 9 = RingMod, 10 = TapeSat.
//   0 = None — handled by NoneInsert (a no-op for dispatch consistency).
//
// All algorithms own their DSP state internally. process() runs in-place on the
// supplied buffer; the algorithm reads whichever VoiceParams fields it needs.
// `grOut` is filled with the algorithm's peak gain reduction normalised to 0..1
// (1 ≡ 24 dB). Only Compressor / Limiter write non-zero; everyone else leaves
// it at 0. InsertProcessor stores the result into its UI-visible atomic.
class InsertAlgorithmBase
{
public:
    virtual ~InsertAlgorithmBase() = default;

    virtual void prepare(double sampleRate, int blockSize) = 0;

    // Clear all DSP state (delay lines, envelope followers, DC blockers, etc.)
    // without re-allocating. Called from InsertProcessor::reset() — should be
    // safe to invoke from the audio thread under suspendProcessing.
    virtual void reset() = 0;

    virtual void process(juce::AudioBuffer<float>& buf,
                         int ns, int nCh,
                         const VoiceParams& p,
                         float& grOut) = 0;
};
