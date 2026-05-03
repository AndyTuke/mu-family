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
    // Sequential processing with intra-FX routing.

    if (doEffect) effect.processReturn(effectSend);

    // Route effect output into the delay and reverb send buses.
    if (doEffect)
    {
        const int nSamples = effectSend.getNumSamples();
        if (effToDelay > 0.001f)
            for (int ch = 0; ch < juce::jmin(effectSend.getNumChannels(), delaySend.getNumChannels()); ++ch)
                delaySend.addFrom(ch, 0, effectSend, ch, 0, nSamples, effToDelay);
        if (effToReverb > 0.001f)
            for (int ch = 0; ch < juce::jmin(effectSend.getNumChannels(), reverbSend.getNumChannels()); ++ch)
                reverbSend.addFrom(ch, 0, effectSend, ch, 0, nSamples, effToReverb);
    }

    if (doDelay) delay.processReturn(delaySend);

    // Route delay output into the reverb send bus.
    if (doDelay && delToReverb > 0.001f)
    {
        const int nSamples = delaySend.getNumSamples();
        for (int ch = 0; ch < juce::jmin(delaySend.getNumChannels(), reverbSend.getNumChannels()); ++ch)
            reverbSend.addFrom(ch, 0, delaySend, ch, 0, nSamples, delToReverb);
    }

    if (doReverb) reverb.processReturn(reverbSend);
}

void FXChain::setHostBpm(double bpm)
{
    delay.setHostBpm(bpm);
    effect.getEchoDelay().setHostBpm(bpm);
}
