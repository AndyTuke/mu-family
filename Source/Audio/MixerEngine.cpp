#include "MixerEngine.h"
#include "VoiceEngine.h"
#include "../FX/FXChain.h"
#include <cmath>

MixerEngine::MixerEngine()
{
    for (auto& p : channelPeaks) p.set(0.0f);
    for (auto& p : returnPeaks)  p.set(0.0f);
    masterPeak.set(0.0f);
}

void MixerEngine::prepare(double sr, int blockSize)
{
    sampleRate = sr;
    for (auto& e : scEnv) e = 0.0f;
    for (auto& buf : channelBufs)
        buf.setSize(2, blockSize, false, true, true);
    effectSendBuf.setSize(2, blockSize, false, true, true);
    delaySendBuf .setSize(2, blockSize, false, true, true);
    reverbSendBuf.setSize(2, blockSize, false, true, true);
}

bool MixerEngine::hasSolo(int numActive) const
{
    for (int i = 0; i < numActive; ++i)
        if (channels[i].solo) return true;
    return false;
}

float MixerEngine::peakOf(const juce::AudioBuffer<float>& buf, int numSamples)
{
    float p = 0.0f;
    for (int c = 0; c < buf.getNumChannels(); ++c)
        p = juce::jmax(p, buf.getMagnitude(c, 0, numSamples));
    return p;
}

bool MixerEngine::hasSignal(const juce::AudioBuffer<float>& buf, int numSamples)
{
    for (int c = 0; c < buf.getNumChannels(); ++c)
        if (buf.getMagnitude(c, 0, numSamples) > 1e-9f)
            return true;
    return false;
}

void MixerEngine::applyPanGain(juce::AudioBuffer<float>& buf,
                                float level, float pan, int numSamples)
{
    // Linear pan law: left attenuates as pan moves right, and vice versa.
    const float gL = level * (1.0f - juce::jmax(0.0f,  pan));
    const float gR = level * (1.0f + juce::jmin(0.0f,  pan));
    if (buf.getNumChannels() > 0) buf.applyGain(0, 0, numSamples, gL);
    if (buf.getNumChannels() > 1) buf.applyGain(1, 0, numSamples, gR);
}

void MixerEngine::processBlock(juce::AudioBuffer<float>&    output,
                                int                          numActiveRhythms,
                                std::unique_ptr<VoiceEngine>* voices,
                                FXChain&                     fxChain,
                                int                          numSamples)
{
    output.clear();
    effectSendBuf.clear();
    delaySendBuf .clear();
    reverbSendBuf.clear();

    const bool anySolo  = hasSolo(numActiveRhythms);
    const int  numOutCh = output.getNumChannels();

    // Clear peaks for channels that have no rhythm this block so their VUs go silent.
    for (int r = numActiveRhythms; r < MaxChannels; ++r)
        channelPeaks[r].set(0.0f);

    // Phase 1: process all voices into their channel buffers and apply headroom trim.
    // We defer pan/gain/mix until Phase 3 so Phase 2 can apply sidechain first.
    for (int r = 0; r < numActiveRhythms; ++r)
    {
        auto& buf = channelBufs[r];
        buf.clear();
        voices[r]->process(buf, numSamples);
        buf.applyGain(kHeadroomTrim);
    }

    // Phase 2: apply sidechain ducking per channel.
    for (int r = 0; r < numActiveRhythms; ++r)
    {
        const auto& ch  = channels[r];
        const int   src = ch.sidechainSource;
        if (src < 0 || src >= numActiveRhythms || src == r) continue;
        if (ch.sidechainAmount <= 0.0f) continue;

        const float sr_f = (float)sampleRate;
        const float atk  = (ch.sidechainAttackMs  > 0.0f)
                         ? std::exp(-1.0f / (ch.sidechainAttackMs  * 0.001f * sr_f)) : 0.0f;
        const float rel  = (ch.sidechainReleaseMs > 0.0f)
                         ? std::exp(-1.0f / (ch.sidechainReleaseMs * 0.001f * sr_f)) : 0.0f;

        const float* srcL = channelBufs[src].getReadPointer(0);
        const float* srcR = channelBufs[src].getNumChannels() > 1
                          ? channelBufs[src].getReadPointer(1) : srcL;
        float* tgtL = channelBufs[r].getWritePointer(0);
        float* tgtR = channelBufs[r].getNumChannels() > 1
                    ? channelBufs[r].getWritePointer(1) : nullptr;

        for (int i = 0; i < numSamples; ++i)
        {
            const float srcLvl = juce::jmax(std::abs(srcL[i]), std::abs(srcR[i]));
            scEnv[r] = (srcLvl > scEnv[r])
                     ? atk * scEnv[r] + (1.0f - atk) * srcLvl
                     : rel * scEnv[r] + (1.0f - rel) * srcLvl;

            const float gain = 1.0f - ch.sidechainAmount * scEnv[r];
            tgtL[i] *= gain;
            if (tgtR) tgtR[i] *= gain;
        }
    }

    // Phase 3: pan/gain, mix to output, FX sends, peak capture.
    for (int r = 0; r < numActiveRhythms; ++r)
    {
        const auto& ch  = channels[r];
        auto&       buf = channelBufs[r];

        if (ch.mute || (anySolo && !ch.solo))
        {
            channelPeaks[r].set(0.0f);
            continue;
        }

        applyPanGain(buf, ch.level, ch.pan, numSamples);
        channelPeaks[r].set(peakOf(buf, numSamples));

        for (int c = 0; c < numOutCh; ++c)
            output.addFrom(c, 0, buf, c, 0, numSamples);

        if (ch.sendEffect > 0.0f)
            for (int c = 0; c < numOutCh; ++c)
                effectSendBuf.addFrom(c, 0, buf, c, 0, numSamples, ch.sendEffect);
        if (ch.sendDelay > 0.0f)
            for (int c = 0; c < numOutCh; ++c)
                delaySendBuf.addFrom(c, 0, buf, c, 0, numSamples, ch.sendDelay);
        if (ch.sendReverb > 0.0f)
            for (int c = 0; c < numOutCh; ++c)
                reverbSendBuf.addFrom(c, 0, buf, c, 0, numSamples, ch.sendReverb);
    }

    const bool doEffect = hasSignal(effectSendBuf, numSamples);
    const bool doReverb = hasSignal(reverbSendBuf, numSamples);

    // Delay always processes every block — its feedback loop must run continuously
    // even between hits, otherwise echoes never propagate out of the circular buffer.
    fxChain.processSends(effectSendBuf, delaySendBuf, reverbSendBuf,
                         doEffect, true, doReverb);

    // Mix FX returns into main output; capture return peaks post-fader so VU reflects the fader.
    if (doEffect)
    {
        applyPanGain(effectSendBuf, returns[0].level, returns[0].pan, numSamples);
        if (!returns[0].mute)
        {
            returnPeaks[0].set(peakOf(effectSendBuf, numSamples));
            for (int c = 0; c < numOutCh; ++c)
                output.addFrom(c, 0, effectSendBuf, c, 0, numSamples);
        }
        else { returnPeaks[0].set(0.0f); }
    }
    else { returnPeaks[0].set(0.0f); }

    const float rawDelayPeak = peakOf(delaySendBuf, numSamples);
    if (rawDelayPeak > 1e-9f)
    {
        applyPanGain(delaySendBuf, returns[1].level, returns[1].pan, numSamples);
        if (!returns[1].mute)
        {
            returnPeaks[1].set(peakOf(delaySendBuf, numSamples));
            for (int c = 0; c < numOutCh; ++c)
                output.addFrom(c, 0, delaySendBuf, c, 0, numSamples);
        }
        else { returnPeaks[1].set(0.0f); }
    }
    else { returnPeaks[1].set(0.0f); }

    // Re-check after processSends: intra-FX routing (delay→reverb, effect→reverb) may have
    // added signal to reverbSendBuf even when no channels had a direct reverb send.
    if (hasSignal(reverbSendBuf, numSamples))
    {
        applyPanGain(reverbSendBuf, returns[2].level, returns[2].pan, numSamples);
        if (!returns[2].mute)
        {
            returnPeaks[2].set(peakOf(reverbSendBuf, numSamples));
            for (int c = 0; c < numOutCh; ++c)
                output.addFrom(c, 0, reverbSendBuf, c, 0, numSamples);
        }
        else { returnPeaks[2].set(0.0f); }
    }
    else { returnPeaks[2].set(0.0f); }

    // Apply master gain first, then capture peak so master VU reflects the master fader.
    applyPanGain(output, masterLevel, masterPan, numSamples);
    masterPeak.set(peakOf(output, numSamples));
}
