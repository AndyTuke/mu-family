#include "MixerEngine.h"
#include "VoiceEngine.h"
#include "../FX/FXChain.h"
#include <cmath>

MixerEngine::MixerEngine()
{
    for (auto& p : channelPeaks)       p.store(0.0f);
    for (auto& p : returnPeaks)        p.store(0.0f);
    masterPeak.store(0.0f);
    for (auto& g : sidechainGR)        g.store(0.0f);
    for (auto& g : returnSidechainGR)  g.store(0.0f);
}

void MixerEngine::prepare(double sr, int blockSize)
{
    sampleRate = sr;
    for (auto& e : scEnv)    e = 0.0f;
    for (auto& e : scRetEnv) e = 0.0f;
    for (auto& buf : channelBufs)
        buf.setSize(2, blockSize, false, true, true);
    effectSendBuf.setSize(2, blockSize, false, true, true);
    delaySendBuf .setSize(2, blockSize, false, true, true);
    reverbSendBuf.setSize(2, blockSize, false, true, true);
    masterInsert.prepare(sr, blockSize);
    masterInsert2.prepare(sr, blockSize);
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
                                int                          numSamples,
                                std::array<juce::AudioBuffer<float>*, 8>* directOuts,
                                juce::AudioBuffer<float>*    fxReturnsOut,
                                const RetiredVoices*         retired)
{
    output.clear();
    effectSendBuf.clear();
    delaySendBuf .clear();
    reverbSendBuf.clear();

    const bool anySolo       = hasSolo(numActiveRhythms);
    // #337 follow-up: when a return is soloed, the user wants to hear ONLY that wet
    // signal — the rhythm channels' dry passthrough to master must also be muted, but
    // their FX sends must still run so the soloed return has something to render.
    const bool anyReturnSolo = returns[0].solo || returns[1].solo || returns[2].solo;
    const int  numOutCh      = output.getNumChannels();

    // Clear peaks for channels that have no rhythm this block so their VUs go silent.
    // Same for sidechainGR so a removed rhythm cannot leave a ghost reading on the
    // inactive slot's GR meter.
    for (int r = numActiveRhythms; r < MaxChannels; ++r)
    {
        channelPeaks[r].store(0.0f);
        sidechainGR  [r].store(0.0f);
    }

    // Phase 1: process all voices into their channel buffers and apply headroom trim.
    // We defer pan/gain/mix until Phase 3 so Phase 2 can apply sidechain first.
    const bool hasRetired = retired != nullptr
                         && retired->engines != nullptr
                         && retired->perSlot > 0;
    for (int r = 0; r < numActiveRhythms; ++r)
    {
        auto& buf = channelBufs[r];
        buf.clear();
        if (voices[r]) voices[r]->process(buf, numSamples);

        // Stage 34 Step 2: retired engines (frozen post-swap tail-out) mix into the
        // SAME channel buf so they ride the same channel fader / pan / sends /
        // inserts as the active engine. Each is owned by the caller; when one
        // reports isFullyDrained() the parallel cleanup flag is store-released
        // for the message thread to move-out and destroy off the audio thread.
        // In Step 2 every slot is null (Step 3 wires retire-on-swap), so the inner
        // body never runs — the loop adds the cost of one null check per slot.
        if (hasRetired)
        {
            const int K = retired->perSlot;
            auto* slots = retired->engines      + r * K;
            auto* flags = retired->cleanupFlags + r * K;
            for (int i = 0; i < K; ++i)
            {
                if (auto& slot = slots[i])
                {
                    slot->process(buf, numSamples);
                    if (slot->isFullyDrained())
                        flags[i].store(true, std::memory_order_release);
                }
            }
        }

        buf.applyGain(kHeadroomTrim);
    }

    // Phase 2: apply sidechain ducking per channel.
    for (int r = 0; r < numActiveRhythms; ++r)
        sidechainGR[r].store(0.0f);

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

        // Threshold-triggered: envelope attacks to 1.0 when source exceeds noise floor,
        // releases to 0 when silent. Raw amplitude following produced negligible gain
        // reduction for typical samples (−18 dBFS kick → <1 dB duck at 100% amount).
        constexpr float kScThreshold = 0.001f; // ≈ −60 dBFS post-trim
        const bool sourceActive = !channels[src].mute && !(anySolo && !channels[src].solo);
        float peakGR = 0.0f;
        for (int i = 0; i < numSamples; ++i)
        {
            const float srcLvl = sourceActive
                               ? juce::jmax(std::abs(srcL[i]), std::abs(srcR[i]))
                               : 0.0f;
            const float target = (srcLvl > kScThreshold) ? 1.0f : 0.0f;
            scEnv[r] = (target > scEnv[r])
                     ? atk * scEnv[r] + (1.0f - atk) * target
                     : rel * scEnv[r] + (1.0f - rel) * target;

            const float gain = 1.0f - ch.sidechainAmount * scEnv[r];
            peakGR = juce::jmax(peakGR, ch.sidechainAmount * scEnv[r]);
            tgtL[i] *= gain;
            if (tgtR) tgtR[i] *= gain;
        }
        sidechainGR[r].store(peakGR);
    }

    // Phase 3: pan/gain, route to output bus, FX sends, peak capture.
    for (int r = 0; r < numActiveRhythms; ++r)
    {
        const auto& ch  = channels[r];
        auto&       buf = channelBufs[r];

        // hardMute: rhythm is fully silent — no master output, no FX sends.
        // skipMaster: dry passthrough to master suppressed (because a return is soloed),
        // but FX sends still run so the soloed return has source material to render.
        const bool hardMute   = ch.mute || (anySolo && !ch.solo);
        const bool skipMaster = hardMute || anyReturnSolo;

        if (hardMute)
        {
            channelPeaks[r].store(0.0f);
            continue;
        }

        applyPanGain(buf, ch.level, ch.pan, numSamples);
        channelPeaks[r].store(peakOf(buf, numSamples));

        const int bus = ch.outputBus;  // 0 = master, 1..8 = direct out
        if (bus == 0)
        {
            // Master mix — also feeds FX sends. skipMaster suppresses just the dry add.
            if (!skipMaster)
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
        else
        {
            // Direct out — bypass master mix and FX sends. If the requested bus isn't
            // active in the current host layout, fall through silently (channel is muted).
            const int idx = bus - 1;
            if (directOuts != nullptr && (*directOuts)[(size_t) idx] != nullptr)
            {
                auto* outBuf = (*directOuts)[(size_t) idx];
                const int dstChans = juce::jmin(outBuf->getNumChannels(), buf.getNumChannels());
                for (int c = 0; c < dstChans; ++c)
                    outBuf->addFrom(c, 0, buf, c, 0, numSamples);
            }
        }
    }

    const bool doEffect = hasSignal(effectSendBuf, numSamples);
    const bool doReverb = hasSignal(reverbSendBuf, numSamples);

    // Delay always processes every block — its feedback loop must run continuously
    // even between hits, otherwise echoes never propagate out of the circular buffer.
    fxChain.processSends(effectSendBuf, delaySendBuf, reverbSendBuf,
                         doEffect, true, doReverb);

    // Mix FX returns into main output; capture return peaks post-fader so VU reflects the fader.
    // If fxReturnsOut is provided (bus 9), each post-fader FX return is also added there.
    auto fanOutToFxReturns = [&](const juce::AudioBuffer<float>& src)
    {
        if (fxReturnsOut == nullptr) return;
        const int dstChans = juce::jmin(fxReturnsOut->getNumChannels(), src.getNumChannels());
        for (int c = 0; c < dstChans; ++c)
            fxReturnsOut->addFrom(c, 0, src, c, 0, numSamples);
    };

    // Apply sidechain ducking to FX return channels (triggered by rhythm channel audio).
    for (int ri = 0; ri < 3; ++ri)
    {
        const auto& ret = returns[ri];
        const int src = ret.sidechainSource;
        if (src < 0 || src >= numActiveRhythms || ret.sidechainAmount <= 0.0f)
        {
            returnSidechainGR[ri].store(0.0f);
            continue;
        }

        const float sr_f = (float)sampleRate;
        const float atk  = (ret.sidechainAttackMs > 0.0f)
                         ? std::exp(-1.0f / (ret.sidechainAttackMs  * 0.001f * sr_f)) : 0.0f;
        const float rel  = (ret.sidechainReleaseMs > 0.0f)
                         ? std::exp(-1.0f / (ret.sidechainReleaseMs * 0.001f * sr_f)) : 0.0f;

        auto& retBuf = (ri == 0) ? effectSendBuf
                     : (ri == 1) ? delaySendBuf
                                 : reverbSendBuf;

        const float* srcL = channelBufs[src].getReadPointer(0);
        const float* srcR = channelBufs[src].getNumChannels() > 1
                          ? channelBufs[src].getReadPointer(1) : srcL;
        float* tgtL = retBuf.getWritePointer(0);
        float* tgtR = retBuf.getNumChannels() > 1 ? retBuf.getWritePointer(1) : nullptr;

        constexpr float kScThreshold = 0.001f;
        const bool sourceActive = !channels[src].mute && !(anySolo && !channels[src].solo);
        float peakGR = 0.0f;
        for (int i = 0; i < numSamples; ++i)
        {
            const float srcLvl = sourceActive
                               ? juce::jmax(std::abs(srcL[i]), std::abs(srcR[i]))
                               : 0.0f;
            const float target = (srcLvl > kScThreshold) ? 1.0f : 0.0f;
            scRetEnv[ri] = (target > scRetEnv[ri])
                         ? atk * scRetEnv[ri] + (1.0f - atk) * target
                         : rel * scRetEnv[ri] + (1.0f - rel) * target;
            const float gain = 1.0f - ret.sidechainAmount * scRetEnv[ri];
            peakGR = juce::jmax(peakGR, ret.sidechainAmount * scRetEnv[ri]);
            tgtL[i] *= gain;
            if (tgtR) tgtR[i] *= gain;
        }
        returnSidechainGR[ri].store(peakGR);
    }

    // Re-check after processSends: echo feedback may have produced output even with no
    // channel send signal this block, so we cannot use the pre-processing doEffect flag.
    if (hasSignal(effectSendBuf, numSamples))
    {
        applyPanGain(effectSendBuf, returns[0].level, returns[0].pan, numSamples);
        if (!returns[0].mute && !(anyReturnSolo && !returns[0].solo))
        {
            returnPeaks[0].store(peakOf(effectSendBuf, numSamples));
            for (int c = 0; c < numOutCh; ++c)
                output.addFrom(c, 0, effectSendBuf, c, 0, numSamples);
            fanOutToFxReturns(effectSendBuf);
        }
        else { returnPeaks[0].store(0.0f); }
    }
    else { returnPeaks[0].store(0.0f); }

    if (hasSignal(delaySendBuf, numSamples))
    {
        applyPanGain(delaySendBuf, returns[1].level, returns[1].pan, numSamples);
        if (!returns[1].mute && !(anyReturnSolo && !returns[1].solo))
        {
            returnPeaks[1].store(peakOf(delaySendBuf, numSamples));
            for (int c = 0; c < numOutCh; ++c)
                output.addFrom(c, 0, delaySendBuf, c, 0, numSamples);
            fanOutToFxReturns(delaySendBuf);
        }
        else { returnPeaks[1].store(0.0f); }
    }
    else { returnPeaks[1].store(0.0f); }

    // Re-check after processSends: intra-FX routing (delay→reverb, effect→reverb) may have
    // added signal to reverbSendBuf even when no channels had a direct reverb send.
    if (hasSignal(reverbSendBuf, numSamples))
    {
        applyPanGain(reverbSendBuf, returns[2].level, returns[2].pan, numSamples);
        if (!returns[2].mute && !(anyReturnSolo && !returns[2].solo))
        {
            returnPeaks[2].store(peakOf(reverbSendBuf, numSamples));
            for (int c = 0; c < numOutCh; ++c)
                output.addFrom(c, 0, reverbSendBuf, c, 0, numSamples);
            fanOutToFxReturns(reverbSendBuf);
        }
        else { returnPeaks[2].store(0.0f); }
    }
    else { returnPeaks[2].store(0.0f); }

    // Apply master gain first, then capture peak so master VU reflects the master fader.
    applyPanGain(output, masterLevel, masterPan, numSamples);
    masterPeak.store(peakOf(output, numSamples));

    // Master inserts — post-fader, post-metering, chained Insert 1 → Insert 2.
    masterInsert.process(output, numSamples, output.getNumChannels(), masterInsertParams);
    masterInsert2.process(output, numSamples, output.getNumChannels(), masterInsertParams2);
}
