#pragma once

#include "EffectSlot.h"
#include "DelaySlot.h"
#include "ReverbSlot.h"

// Owns and sequences the three global FX slots: Effect (insert) → Delay (insert) → Reverb (send).
// Intra-FX routing amounts (Effect→Delay, Effect→Reverb, Delay→Reverb) are set by the mixer
// in Stage 9. For now they default to 0 (signal flows in series only).
class FXChain
{
public:
    FXChain();

    void prepare(double sampleRate, int blockSize);

    // Per-send-bus processing for the mixer.  Each enabled bus is processed wet-only.
    void processSends(juce::AudioBuffer<float>& effectSend,
                      juce::AudioBuffer<float>& delaySend,
                      juce::AudioBuffer<float>& reverbSend,
                      bool doEffect, bool doDelay, bool doReverb);

    void setHostBpm(double bpm);

    // Silence all three slots' tails and feedback (the mixer calls it after a non-finite block). Audio thread.
    void resetState();

    EffectSlot& effectSlot() { return effect; }
    DelaySlot&  delaySlot()  { return delay;  }
    ReverbSlot& reverbSlot() { return reverb; }

    // Intra-FX routing sends (0–1). Wired to mixer channel strips in Stage 9.
    void setEffectToDelaySend(float v)   { effToDelay  = juce::jlimit(0.0f, 1.0f, v); }
    void setEffectToReverbSend(float v)  { effToReverb = juce::jlimit(0.0f, 1.0f, v); }
    void setDelayToReverbSend(float v)   { delToReverb = juce::jlimit(0.0f, 1.0f, v); }

private:
    EffectSlot effect;
    DelaySlot  delay;
    ReverbSlot reverb;

    float effToDelay  = 0.0f;
    float effToReverb = 0.0f;
    float delToReverb = 0.0f;

    // The Effect slot keeps running for a tail after its send goes silent, so a chorus, flanger or phaser
    // plays its delay lines and feedback out instead of freezing them (stale audio replayed on the next
    // send, feedback cut mid-ring). Counted in samples; one second.
    int effectTailBudget = 44100;
    int effectTailLeft   = 0;
};
