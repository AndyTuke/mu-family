#include "FXChain.h"

FXChain::FXChain() = default;

void FXChain::prepare(double sampleRate, int blockSize)
{
    effect.prepare(sampleRate, blockSize);
    delay.prepare(sampleRate, blockSize);
    reverb.prepare(sampleRate, blockSize);

    scratchBuf.setSize(2, blockSize);
}

void FXChain::process(juce::AudioBuffer<float>& buffer)
{
    // --- Effect (insert) ---
    // Capture post-effect signal for intra-FX sends if needed.
    effect.process(buffer);

    // --- Intra-FX routing: Effect → Delay ---
    // If effToDelay > 0, mix effect return into delay input.
    // For Stage 8 this path exists but effToDelay defaults to 0.
    if (effToDelay > 0.001f)
    {
        // Mixer in Stage 9 will properly route the effect return channel via send knobs.
        juce::ignoreUnused(scratchBuf);
    }

    // --- Delay (insert) ---
    delay.process(buffer);

    // --- Intra-FX routing: Effect/Delay → Reverb ---
    // (Handled via send knobs wired in Stage 9 mixer)

    // --- Reverb (send) ---
    reverb.process(buffer);
}

void FXChain::processSends(juce::AudioBuffer<float>& effectSend,
                           juce::AudioBuffer<float>& delaySend,
                           juce::AudioBuffer<float>& reverbSend,
                           bool doEffect, bool doDelay, bool doReverb)
{
    if (doEffect) effect.processReturn(effectSend);
    if (doDelay)  delay.processReturn(delaySend);
    if (doReverb) reverb.processReturn(reverbSend);
}

void FXChain::setHostBpm(double bpm)
{
    delay.setHostBpm(bpm);
}
