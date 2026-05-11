#include "FXChain.h"

FXChain::FXChain() = default;

void FXChain::prepare(double sampleRate, int blockSize)
{
    effect.prepare(sampleRate, blockSize);
    delay.prepare(sampleRate, blockSize);
    reverb.prepare(sampleRate, blockSize);
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

    // Run reverb if channels sent directly, OR if intra-FX routing added signal this block.
    const bool reverbNeeded = doReverb
        || (doDelay  && delToReverb > 0.001f)
        || (doEffect && effToReverb > 0.001f);
    if (reverbNeeded) reverb.processReturn(reverbSend);
}

void FXChain::setHostBpm(double bpm)
{
    delay.setHostBpm(bpm);
    effect.getEchoDelay().setHostBpm(bpm);
}
