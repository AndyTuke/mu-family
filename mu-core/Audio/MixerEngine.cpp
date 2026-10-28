#include "MixerEngine.h"
#include "VoiceEngine.h"
#include "Audio/FX/Slots/FXChain.h"
#include <cmath>

MixerEngine::MixerEngine()
{
    for (auto& b : channelLastBus)    b = kBusUnset;
    for (auto& p : channelPeaks)       p.store(0.0f);
    for (auto& p : returnPeaks)        p.store(0.0f);
    masterPeak.store(0.0f);
    for (auto& p : channelPeaksL)      p.store(0.0f);
    for (auto& p : channelPeaksR)      p.store(0.0f);
    for (auto& p : returnPeaksL)       p.store(0.0f);
    for (auto& p : returnPeaksR)       p.store(0.0f);
    masterPeakL.store(0.0f);
    masterPeakR.store(0.0f);
    for (auto& g : sidechainGR)        g.store(0.0f);
    for (auto& g : returnSidechainGR)  g.store(0.0f);
    for (size_t b = 0; b < buses.size(); ++b)
    {
        busPeaks[b].store(0.0f);  busPeaksL[b].store(0.0f);  busPeaksR[b].store(0.0f);
        busSidechainGR[b].store(0.0f);
        // Each bus insert seeds its noise from its own index, apart from every layer's.
        for (int i = 0; i < kBusInserts; ++i)
            buses[b].insert[i].processor.setSlotIndex(MaxChannels + (int) b * kBusInserts + i);
    }
}

void MixerEngine::prepare(double sr, int blockSize)
{
    blockSize = juce::jmax(1, blockSize);   // the block driver sizes its scratch the same way, so the two always agree
    sampleRate = sr;
    for (auto& e : scEnv)    e = 0.0f;
    for (auto& e : scRetEnv) e = 0.0f;
    for (auto& e : scBusEnv) e = 0.0f;
    // Gains and bus routing start fresh: the first block after a prepare applies its targets without a ramp.
    for (auto& g : channelRamp) g = GainRamp{};
    for (auto& g : busRamp)     g = GainRamp{};
    for (auto& b : channelLastBus) b = kBusUnset;
    for (auto& buf : channelBufs)
        buf.setSize(2, blockSize, false, true, true);
    effectSendBuf.setSize(2, blockSize, false, true, true);
    delaySendBuf .setSize(2, blockSize, false, true, true);
    reverbSendBuf.setSize(2, blockSize, false, true, true);
    for (auto& b : returnScSource)
        b.setSize(2, blockSize, false, true, true);
    extScCapture .setSize(2, blockSize, false, true, true);
    masterInsert.prepare(sr, blockSize);
    masterInsert2.prepare(sr, blockSize);
    // Everything a bus needs is sized here, so the audio thread never allocates for one.
    for (size_t b = 0; b < buses.size(); ++b)
    {
        busBufs[b]    .setSize(2, blockSize, false, true, true);
        busTap[b]     .setSize(2, blockSize, false, true, true);
        busScSource[b].setSize(2, blockSize, false, true, true);
        for (auto& ins : buses[b].insert)
            ins.processor.prepare(sr, blockSize);
    }
}

float MixerEngine::peakOf(const juce::AudioBuffer<float>& buf, int numSamples)
{
    float p = 0.0f;
    for (int c = 0; c < buf.getNumChannels(); ++c)
        p = juce::jmax(p, buf.getMagnitude(c, 0, numSamples));
    return p;
}

void MixerEngine::storePeak(const juce::AudioBuffer<float>& buf, int numSamples,
                            std::atomic<float>& both, std::atomic<float>& left, std::atomic<float>& right)
{
    const int   chans = buf.getNumChannels();
    const float pl    = chans > 0 ? buf.getMagnitude(0, 0, numSamples) : 0.0f;
    const float pr    = chans > 1 ? buf.getMagnitude(1, 0, numSamples) : pl;
    // The combined peak covers every channel: reuse the two scans above, then any channels past the pair.
    float all = juce::jmax(pl, pr);
    for (int c = 2; c < chans; ++c)
        all = juce::jmax(all, buf.getMagnitude(c, 0, numSamples));
    both .store(all);
    left .store(pl);
    right.store(pr);
}

void MixerEngine::clearPeak(std::atomic<float>& both, std::atomic<float>& left, std::atomic<float>& right)
{
    both.store(0.0f);
    left.store(0.0f);
    right.store(0.0f);
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

void MixerEngine::applyPanGainRamped(juce::AudioBuffer<float>& buf, float level, float pan, int numSamples, GainRamp& ramp)
{
    const float tL = level * (1.0f - juce::jmax(0.0f, pan));
    const float tR = level * (1.0f + juce::jmin(0.0f, pan));
    if (! ramp.primed) { ramp.l = tL; ramp.r = tR; ramp.primed = true; }
    if (buf.getNumChannels() > 0) buf.applyGainRamp(0, 0, numSamples, ramp.l, tL);
    if (buf.getNumChannels() > 1) buf.applyGainRamp(1, 0, numSamples, ramp.r, tR);
    ramp.l = tL;
    ramp.r = tR;
}

namespace
{
// Level-following sidechain duck: the envelope follows the source level relative to
// kScFullDuckLevel (a full duck at or above it, proportional below) with the strip's attack and
// release; the target is scaled by 1 - amount x envelope. A pure threshold gate held the duck at
// full depth for as long as a decaying source stayed above -60 dBFS (a kick's tail lasts about a
// second), so the target never recovered between hits. The release has a floor so the follower
// rides over the source's own audio-rate ripple (a bass note's rectified waveform).
// Returns the peak reduction.
float applySidechainDuck(const float* srcL, const float* srcR, bool sourceActive,
                         float* tgtL, float* tgtR, int numSamples, float amount,
                         float attackMs, float releaseMs, double sampleRate, float& env) noexcept
{
    constexpr float kScFullDuckLevel = 0.25f;   // post-trim source level that ducks fully (about -12 dBFS)
    constexpr float kScMinReleaseMs  = 30.0f;
    const float sr  = (float) sampleRate;
    const float atk = attackMs  > 0.0f ? std::exp(-1.0f / (attackMs  * 0.001f * sr)) : 0.0f;
    const float rel = std::exp(-1.0f / (juce::jmax(releaseMs, kScMinReleaseMs) * 0.001f * sr));
    float peakGR = 0.0f;
    for (int i = 0; i < numSamples; ++i)
    {
        const float srcLvl = sourceActive ? juce::jmax(std::abs(srcL[i]), std::abs(srcR[i])) : 0.0f;
        const float target = juce::jmin(1.0f, srcLvl / kScFullDuckLevel);
        env = target > env ? atk * env + (1.0f - atk) * target
                           : rel * env + (1.0f - rel) * target;
        const float gr = amount * env;
        peakGR = juce::jmax(peakGR, gr);
        tgtL[i] *= 1.0f - gr;
        if (tgtR != nullptr) tgtR[i] *= 1.0f - gr;
    }
    return peakGR;
}
} // namespace

void MixerEngine::processBlock(juce::AudioBuffer<float>&    output,
                                int                          numActiveChannels,
                                std::unique_ptr<VoiceEngine>* voices,
                                FXChain&                     fxChain,
                                int                          numSamples,
                                std::array<juce::AudioBuffer<float>*, MaxChannels>* directOuts,
                                juce::AudioBuffer<float>*    fxReturnsOut,
                                const RetiredVoices*         retired,
                                const RenderChannelFn*       renderChannel)
{
    // Strips whose follower a type change asked to be zeroed start this block from silence.
    for (auto& request : scEnvResetRequest)
        if (request.load(std::memory_order_acquire) && request.exchange(false, std::memory_order_acq_rel))
            scEnv[&request - scEnvResetRequest] = 0.0f;

    output.clear();
    effectSendBuf.clear();
    delaySendBuf .clear();
    reverbSendBuf.clear();
    for (auto& b : busBufs) b.clear();

    // Routing and audibility, snapshotted once so the whole block agrees. A channel feeds bus chBus[r] (-1 =
    // the master); a non-zero outputBus (DAW direct out) wins and feeds neither. Solo is judged over paths: a
    // strip is on a soloed path when it, its bus or (for a bus) any of its channels is soloed, and with any
    // solo active every strip off such a path is cut. A muted bus is cut from the output only: the channels'
    // own sends are taken before it and are unaffected.
    int  chBus[MaxChannels];
    int  chOut[MaxChannels];
    bool chAudible[MaxChannels];
    int   busSrc[kNumBuses > 0 ? kNumBuses : 1] {};
    float busAmt[kNumBuses > 0 ? kNumBuses : 1] {};
    bool busAudible[kNumBuses > 0 ? kNumBuses : 1] {};
    bool busSoloMember[kNumBuses > 0 ? kNumBuses : 1] {};
    bool anySolo = false;
    for (int r = 0; r < numActiveChannels; ++r)
    {
        const auto& ch = channels[r];
        const int assign = ch.busAssign.load(std::memory_order_relaxed);
        chOut[r] = ch.outputBus.load(std::memory_order_relaxed);
        chBus[r] = (kNumBuses > 0 && chOut[r] == 0 && assign >= 1 && assign <= kNumBuses)
                     ? assign - 1 : -1;
        if (ch.solo.load(std::memory_order_relaxed))
        {
            anySolo = true;
            if (chBus[r] >= 0) busSoloMember[chBus[r]] = true;
        }
    }
    for (size_t b = 0; b < buses.size(); ++b)
    {
        if (buses[b].solo.load(std::memory_order_relaxed)) anySolo = true;
        // A bus's sidechain source and amount are read once here, so phases 2c and 3b agree on them.
        busSrc[b] = buses[b].sidechainSource.load(std::memory_order_relaxed);
        busAmt[b] = buses[b].sidechainAmount.load(std::memory_order_relaxed);
    }
    for (int r = 0; r < numActiveChannels; ++r)
    {
        const auto& ch = channels[r];
        const bool onSoloPath = ch.solo.load(std::memory_order_relaxed)
                             || (chBus[r] >= 0 && buses[(size_t) chBus[r]].solo.load(std::memory_order_relaxed));
        chAudible[r] = ! ch.mute.load(std::memory_order_relaxed) && ! (anySolo && ! onSoloPath);
    }
    for (size_t b = 0; b < buses.size(); ++b)
        busAudible[b] = ! buses[b].mute.load(std::memory_order_relaxed)
                     && ! (anySolo && ! (buses[b].solo.load(std::memory_order_relaxed) || busSoloMember[b]));

    // follow-up: when a return is soloed, the user wants to hear ONLY that wet
    // signal — the channels' dry passthrough to master must also be muted, but
    // their FX sends must still run so the soloed return has something to render.
    const bool anyReturnSolo = returns[0].solo.load(std::memory_order_relaxed)
                            || returns[1].solo.load(std::memory_order_relaxed)
                            || returns[2].solo.load(std::memory_order_relaxed);
    const int  numOutCh      = output.getNumChannels();

    // Clear peaks for inactive channels this block so their VUs go silent.
    // Same for sidechainGR so a removed channel cannot leave a ghost reading on the
    // inactive slot's GR meter.
    for (int r = numActiveChannels; r < MaxChannels; ++r)
    {
        clearPeak(channelPeaks[r], channelPeaksL[r], channelPeaksR[r]);
        sidechainGR  [r].store(0.0f);
    }

    // Phase 1: process all voices into their channel buffers and apply headroom trim.
    // We defer pan/gain/mix until Phase 3 so Phase 2 can apply sidechain first.
    const bool hasRetired = retired != nullptr
                         && retired->engines != nullptr
                         && retired->perSlot > 0;
    const bool useRenderHook = renderChannel != nullptr && *renderChannel;
    for (int r = 0; r < numActiveChannels; ++r)
    {
        auto& buf = channelBufs[r];
        buf.clear();
        if (useRenderHook)            (*renderChannel)(r, buf, numSamples);
        else if (voices && voices[r]) voices[r]->process(buf, numSamples);

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

        if (channelTap != nullptr) channelTap->capture(r, buf, numSamples);   // tests only (null otherwise)
        buf.applyGain(kHeadroomTrim);
    }

    // Phase 2: apply sidechain ducking per channel.
    for (int r = 0; r < numActiveChannels; ++r)
        sidechainGR[r].store(0.0f);

    for (int r = 0; r < numActiveChannels; ++r)
    {
        const auto& ch  = channels[r];
        // Snapshot all fields used in this block so we don't re-load atomics per sample.
        const int   src    = ch.sidechainSource.load(std::memory_order_relaxed);
        const float scAmt  = ch.sidechainAmount.load(std::memory_order_relaxed);
        if (scAmt <= 0.0f) continue;

        const bool isExt = (src == kExtSidechainSrc);
        if (!isExt && (src < 0 || src >= numActiveChannels || src == r)) continue;
        if  (isExt && extScL == nullptr) continue;

        const float* srcL = isExt ? extScL : channelBufs[src].getReadPointer(0);
        const float* srcR = isExt ? extScR
                          : (channelBufs[src].getNumChannels() > 1
                             ? channelBufs[src].getReadPointer(1) : srcL);
        float* tgtL = channelBufs[r].getWritePointer(0);
        float* tgtR = channelBufs[r].getNumChannels() > 1
                    ? channelBufs[r].getWritePointer(1) : nullptr;
        // External DAW sidechain always active; internal respects mute/solo state.
        const bool sourceActive = isExt || chAudible[src];
        sidechainGR[r].store(applySidechainDuck(srcL, srcR, sourceActive, tgtL, tgtR, numSamples, scAmt,
                                                ch.sidechainAttackMs.load(std::memory_order_relaxed),
                                                ch.sidechainReleaseMs.load(std::memory_order_relaxed),
                                                sampleRate, scEnv[r]));
    }

    // Phase 2b: a return that ducks from a channel listens to that channel before its fader and pan, so keep a
    // copy now (Phase 3 applies them in place). Only the sources a return actually uses are copied.
    for (int ri = 0; ri < 3; ++ri)
    {
        const int src = returns[ri].sidechainSource.load(std::memory_order_relaxed);
        if (returns[ri].sidechainAmount.load(std::memory_order_relaxed) <= 0.0f) continue;
        if (src < 0 || src >= numActiveChannels) continue;   // external or invalid: handled in the duck pass
        for (int c = 0; c < returnScSource[ri].getNumChannels(); ++c)
            returnScSource[ri].copyFrom(c, 0, channelBufs[src], juce::jmin(c, channelBufs[src].getNumChannels() - 1), 0, numSamples);
    }

    // Phase 2c: a bus that ducks from a channel listens to it before the channel's fader and pan, so keep a
    // copy now too (a bus that ducks from a lower bus reads that bus's tap instead).
    for (size_t b = 0; b < buses.size(); ++b)
    {
        const int src = busSrc[b];
        if (busAmt[b] <= 0.0f) continue;
        if (src < 0 || src >= numActiveChannels) continue;
        for (int c = 0; c < busScSource[b].getNumChannels(); ++c)
            busScSource[b].copyFrom(c, 0, channelBufs[src], juce::jmin(c, channelBufs[src].getNumChannels() - 1), 0, numSamples);
    }

    // Phase 3: pan/gain, route to a bus or the master, FX sends, peak capture.
    for (int r = 0; r < numActiveChannels; ++r)
    {
        const auto& ch  = channels[r];
        auto&       buf = channelBufs[r];

        // hardMute: rhythm is fully silent — no bus or master output, no FX sends.
        // skipMaster: dry passthrough to master suppressed (because a return is soloed),
        // but FX sends still run so the soloed return has source material to render.
        // A strip that just went silent (muted, or off the soloed path) still renders this block with its gain
        // ramping to zero, so the cut is not a click; from then on it is skipped.
        auto& ramp = channelRamp[r];
        const int prevBus = channelLastBus[r] == kBusUnset ? chBus[r] : channelLastBus[r];
        channelLastBus[r] = chBus[r];
        const bool hardMute   = ! chAudible[r];
        const bool fadingOut  = hardMute && ramp.primed && (ramp.l > 0.0f || ramp.r > 0.0f);
        const bool skipMaster = (hardMute && ! fadingOut) || anyReturnSolo;

        if (hardMute && ! fadingOut)
        {
            if (! ramp.primed) { ramp.l = ramp.r = 0.0f; ramp.primed = true; }
            clearPeak(channelPeaks[r], channelPeaksL[r], channelPeaksR[r]);
            continue;
        }

        applyPanGainRamped(buf, hardMute ? 0.0f : ch.level.load(std::memory_order_relaxed),
                                ChannelState::effective(ch.pan, ch.panMod), numSamples, ramp);
        storePeak(buf, numSamples, channelPeaks[r], channelPeaksL[r], channelPeaksR[r]);

        const int bus = chOut[r];  // 0 = master, 1..8 = direct out (the block's snapshot)
        if (bus == 0)
        {
            // Dry path — also feeds FX sends. A bus-routed channel always sums into its bus (the bus decides
            // what reaches the master, and its own sends need the signal even while a return is soloed);
            // a master-routed one is added to the output unless skipMaster suppresses it.
            // A bus reassigned since the last block crossfades from the old destination to the new one.
            auto routeTo = [&](int dest, float g0, float g1)
            {
                if (dest < 0 && skipMaster) return;
                auto& dst = dest >= 0 ? busBufs[(size_t) dest] : output;
                for (int c = 0; c < numOutCh; ++c)
                    dst.addFromWithRamp(c, 0, buf.getReadPointer(c), numSamples, g0, g1);
            };
            if (prevBus != chBus[r]) { routeTo(prevBus, 1.0f, 0.0f); routeTo(chBus[r], 0.0f, 1.0f); }
            else                     routeTo(chBus[r], 1.0f, 1.0f);

            const float sEff = ChannelState::effective(ch.sendEffect, ch.sendEffectMod);
            const float sDly = ChannelState::effective(ch.sendDelay,  ch.sendDelayMod);
            const float sRev = ChannelState::effective(ch.sendReverb, ch.sendReverbMod);
            if (sEff > 0.0f)
                for (int c = 0; c < numOutCh; ++c)
                    effectSendBuf.addFrom(c, 0, buf, c, 0, numSamples, sEff);
            if (sDly > 0.0f)
                for (int c = 0; c < numOutCh; ++c)
                    delaySendBuf.addFrom(c, 0, buf, c, 0, numSamples, sDly);
            if (sRev > 0.0f)
                for (int c = 0; c < numOutCh; ++c)
                    reverbSendBuf.addFrom(c, 0, buf, c, 0, numSamples, sRev);
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

    // Phase 3b: the group buses, in index order (channels -> buses -> returns -> master is the fixed order, so
    // no loop can form). Per bus: sidechain duck, the insert chain, then fader / pan, peak, sends and the
    // add to the output. First mark which buses a later strip ducks from, so only those keep a tap.
    bool busTapNeeded[kNumBuses > 0 ? kNumBuses : 1] {};
    {
        auto markIfBus = [&](int src, float amount)
        {
            int sb = 0;
            if (amount > 0.0f && mu_mix::isBusSource(src, MaxChannels, kNumBuses, &sb)) busTapNeeded[sb] = true;
        };
        for (size_t b = 0; b < buses.size(); ++b)
            markIfBus(busSrc[b], busAmt[b]);
        for (const auto& ret : returns)
            markIfBus(ret.sidechainSource.load(std::memory_order_relaxed), ret.sidechainAmount.load(std::memory_order_relaxed));
    }

    for (int b = 0; b < kNumBuses; ++b)
    {
        auto& bus = buses[(size_t) b];
        auto& buf = busBufs[(size_t) b];
        busSidechainGR[(size_t) b].store(0.0f);

        // Duck from a channel (its pre-fader copy), a LOWER bus (that bus's tap, already complete) or the
        // external input; any other source (a later bus, itself) is ignored, which keeps the graph acyclic.
        const float scAmt = busAmt[b];
        const int   src   = busSrc[b];
        if (scAmt > 0.0f)
        {
            int srcBus = -1;
            const bool isExt = (src == kExtSidechainSrc);
            const bool isBus = mu_mix::isBusSource(src, MaxChannels, kNumBuses, &srcBus) && srcBus < b;
            const bool isCh  = src >= 0 && src < numActiveChannels;
            if ((isExt && extScL != nullptr) || isBus || isCh)
            {
                const auto& scBuf = isBus ? busTap[(size_t) srcBus] : busScSource[(size_t) b];
                const float* srcL = isExt ? extScL : scBuf.getReadPointer(0);
                const float* srcR = isExt ? extScR : (scBuf.getNumChannels() > 1 ? scBuf.getReadPointer(1) : srcL);
                float* tgtL = buf.getWritePointer(0);
                float* tgtR = buf.getNumChannels() > 1 ? buf.getWritePointer(1) : nullptr;
                const bool sourceActive = isExt || (isBus ? busAudible[srcBus] : chAudible[src]);
                busSidechainGR[(size_t) b].store(applySidechainDuck(srcL, srcR, sourceActive, tgtL, tgtR, numSamples, scAmt,
                                                                    bus.sidechainAttackMs.load(std::memory_order_relaxed),
                                                                    bus.sidechainReleaseMs.load(std::memory_order_relaxed),
                                                                    sampleRate, scBusEnv[b]));
            }
        }

        // Insert chain: insert 0 then 1, each the layer insert, parameters read once for the block. They run
        // even for a bus that is cut from the output, so their state and tails stay in step.
        for (auto& ins : bus.insert)
            ins.processor.process(buf, numSamples, buf.getNumChannels(), ins.snapshot());

        if (busTapNeeded[b])
            for (int c = 0; c < busTap[(size_t) b].getNumChannels(); ++c)
                busTap[(size_t) b].copyFrom(c, 0, buf, juce::jmin(c, buf.getNumChannels() - 1), 0, numSamples);

        // A muted bus (or one off the soloed path) is cut from the output and its sends; the channels'
        // sends were taken in Phase 3 and are not touched.
        // A bus that just went silent ramps to zero over this block first, as a channel does.
        auto& busGain = busRamp[(size_t) b];
        const bool busFading = ! busAudible[b] && busGain.primed && (busGain.l > 0.0f || busGain.r > 0.0f);
        if (! busAudible[b] && ! busFading)
        {
            if (! busGain.primed) { busGain.l = busGain.r = 0.0f; busGain.primed = true; }
            clearPeak(busPeaks[(size_t) b], busPeaksL[(size_t) b], busPeaksR[(size_t) b]);
            continue;
        }

        applyPanGainRamped(buf, busAudible[b] ? bus.level.load(std::memory_order_relaxed) : 0.0f,
                                ChannelState::effective(bus.pan, bus.panMod), numSamples, busGain);
        storePeak(buf, numSamples, busPeaks[(size_t) b], busPeaksL[(size_t) b], busPeaksR[(size_t) b]);

        if (! anyReturnSolo)
            for (int c = 0; c < numOutCh; ++c)
                output.addFrom(c, 0, buf, c, 0, numSamples);

        const float sEff = ChannelState::effective(bus.sendEffect, bus.sendEffectMod);
        const float sDly = ChannelState::effective(bus.sendDelay,  bus.sendDelayMod);
        const float sRev = ChannelState::effective(bus.sendReverb, bus.sendReverbMod);
        if (sEff > 0.0f)
            for (int c = 0; c < numOutCh; ++c)
                effectSendBuf.addFrom(c, 0, buf, c, 0, numSamples, sEff);
        if (sDly > 0.0f)
            for (int c = 0; c < numOutCh; ++c)
                delaySendBuf.addFrom(c, 0, buf, c, 0, numSamples, sDly);
        if (sRev > 0.0f)
            for (int c = 0; c < numOutCh; ++c)
                reverbSendBuf.addFrom(c, 0, buf, c, 0, numSamples, sRev);
    }

    const bool doEffect = hasSignal(effectSendBuf, numSamples);
    const bool doReverb = hasSignal(reverbSendBuf, numSamples);

    // Delay always processes every block — its feedback loop must run continuously
    // even between hits, otherwise echoes never propagate out of the circular buffer.
    // The send buffers are allocated at the prepared block size and an FX slot processes the whole buffer it
    // is given, so hand it views of this block's real length: a host block shorter than the prepared size
    // would otherwise advance every delay line, LFO and reverb tail by the full prepared size per call.
    // (A view over the buffer's own storage: no allocation.)
    const int fxSamples = juce::jmin(numSamples, effectSendBuf.getNumSamples());
    juce::AudioBuffer<float> effectBus (effectSendBuf.getArrayOfWritePointers(), effectSendBuf.getNumChannels(), 0, fxSamples);
    juce::AudioBuffer<float> delayBus  (delaySendBuf .getArrayOfWritePointers(), delaySendBuf .getNumChannels(), 0, fxSamples);
    juce::AudioBuffer<float> reverbBus (reverbSendBuf.getArrayOfWritePointers(), reverbSendBuf.getNumChannels(), 0, fxSamples);
    fxChain.processSends(effectBus, delayBus, reverbBus, doEffect, true, doReverb);

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
        // Snapshot atomic fields once per return per block.
        const int   src    = ret.sidechainSource.load(std::memory_order_relaxed);
        const float scAmt  = ret.sidechainAmount.load(std::memory_order_relaxed);
        if (scAmt <= 0.0f)
        {
            returnSidechainGR[ri].store(0.0f);
            continue;
        }

        const bool isExt = (src == kExtSidechainSrc);
        int srcBus = -1;
        const bool isBus = mu_mix::isBusSource(src, MaxChannels, kNumBuses, &srcBus);
        if (!isExt && !isBus && (src < 0 || src >= numActiveChannels))
        {
            returnSidechainGR[ri].store(0.0f);
            continue;
        }
        if (isExt && extScL == nullptr)
        {
            returnSidechainGR[ri].store(0.0f);
            continue;
        }

        auto& retBuf = (ri == 0) ? effectSendBuf
                     : (ri == 1) ? delaySendBuf
                                 : reverbSendBuf;

        // Internal sources come from the pre-fader copy taken in Phase 2b (a channel) or the bus's tap (a bus).
        const auto& scBuf = isBus ? busTap[(size_t) srcBus] : returnScSource[ri];
        const float* srcL = isExt ? extScL : scBuf.getReadPointer(0);
        const float* srcR = isExt ? extScR
                          : (scBuf.getNumChannels() > 1 ? scBuf.getReadPointer(1) : srcL);
        float* tgtL = retBuf.getWritePointer(0);
        float* tgtR = retBuf.getNumChannels() > 1 ? retBuf.getWritePointer(1) : nullptr;
        const bool sourceActive = isExt || (isBus ? busAudible[srcBus] : chAudible[src]);
        returnSidechainGR[ri].store(applySidechainDuck(srcL, srcR, sourceActive, tgtL, tgtR, numSamples, scAmt,
                                                       ret.sidechainAttackMs.load(std::memory_order_relaxed),
                                                       ret.sidechainReleaseMs.load(std::memory_order_relaxed),
                                                       sampleRate, scRetEnv[ri]));
    }

    // Re-check after processSends: echo feedback may have produced output even with no
    // channel send signal this block, so we cannot use the pre-processing doEffect flag.
    if (hasSignal(effectSendBuf, numSamples))
    {
        applyPanGain(effectSendBuf, returns[0].level.load(std::memory_order_relaxed),
                                    returns[0].pan.load(std::memory_order_relaxed), numSamples);
        if (!returns[0].mute.load(std::memory_order_relaxed)
            && !(anyReturnSolo && !returns[0].solo.load(std::memory_order_relaxed)))
        {
            storePeak(effectSendBuf, numSamples, returnPeaks[0], returnPeaksL[0], returnPeaksR[0]);
            for (int c = 0; c < numOutCh; ++c)
                output.addFrom(c, 0, effectSendBuf, c, 0, numSamples);
            fanOutToFxReturns(effectSendBuf);
        }
        else { clearPeak(returnPeaks[0], returnPeaksL[0], returnPeaksR[0]); }
    }
    else { clearPeak(returnPeaks[0], returnPeaksL[0], returnPeaksR[0]); }

    if (hasSignal(delaySendBuf, numSamples))
    {
        applyPanGain(delaySendBuf, returns[1].level.load(std::memory_order_relaxed),
                                   returns[1].pan.load(std::memory_order_relaxed), numSamples);
        if (!returns[1].mute.load(std::memory_order_relaxed)
            && !(anyReturnSolo && !returns[1].solo.load(std::memory_order_relaxed)))
        {
            storePeak(delaySendBuf, numSamples, returnPeaks[1], returnPeaksL[1], returnPeaksR[1]);
            for (int c = 0; c < numOutCh; ++c)
                output.addFrom(c, 0, delaySendBuf, c, 0, numSamples);
            fanOutToFxReturns(delaySendBuf);
        }
        else { clearPeak(returnPeaks[1], returnPeaksL[1], returnPeaksR[1]); }
    }
    else { clearPeak(returnPeaks[1], returnPeaksL[1], returnPeaksR[1]); }

    // Re-check after processSends: intra-FX routing (delay→reverb, effect→reverb) may have
    // added signal to reverbSendBuf even when no channels had a direct reverb send.
    if (hasSignal(reverbSendBuf, numSamples))
    {
        applyPanGain(reverbSendBuf, returns[2].level.load(std::memory_order_relaxed),
                                    returns[2].pan.load(std::memory_order_relaxed), numSamples);
        if (!returns[2].mute.load(std::memory_order_relaxed)
            && !(anyReturnSolo && !returns[2].solo.load(std::memory_order_relaxed)))
        {
            storePeak(reverbSendBuf, numSamples, returnPeaks[2], returnPeaksL[2], returnPeaksR[2]);
            for (int c = 0; c < numOutCh; ++c)
                output.addFrom(c, 0, reverbSendBuf, c, 0, numSamples);
            fanOutToFxReturns(reverbSendBuf);
        }
        else { clearPeak(returnPeaks[2], returnPeaksL[2], returnPeaksR[2]); }
    }
    else { clearPeak(returnPeaks[2], returnPeaksL[2], returnPeaksR[2]); }

    // Master inserts — run on the summed bus BEFORE the fader/meter (standard master-bus
    // processing, so a master compressor/limiter sees a consistent pre-fader level), chained
    // Insert 1 → Insert 2. Assemble local VoiceParams from the atomic fields so
    // InsertProcessor::process gets a stable snapshot for this block (no race while the
    // message thread updates).
    VoiceParams mi1, mi2;
    mi1.insertAlgo = masterInsert1Algo.load(std::memory_order_relaxed);
    for (int i = 0; i < VoiceParams::kInsertSlotCount; ++i)
        mi1.insertParam[i] = masterInsert1Param[i].load(std::memory_order_relaxed);
    mi2.insertAlgo = masterInsert2Algo.load(std::memory_order_relaxed);
    for (int i = 0; i < VoiceParams::kInsertSlotCount; ++i)
        mi2.insertParam[i] = masterInsert2Param[i].load(std::memory_order_relaxed);
    masterInsert.process(output, numSamples, output.getNumChannels(), mi1);
    masterInsert2.process(output, numSamples, output.getNumChannels(), mi2);

    // Apply the master fader, then capture the peak — so the master VU reflects the true
    // post-insert, post-fader output that actually leaves the mixer.
    applyPanGain(output, masterLevel.load(std::memory_order_relaxed),
                         masterPan.load(std::memory_order_relaxed), numSamples);

    // NaN / infinity containment: one non-finite sample in any feedback state (a delay line, a reverb tail, a
    // master insert) would otherwise repeat forever. Adding x * 0 over the block gives NaN if any sample is
    // NaN or infinite and exactly zero otherwise (one cheap pass, no branch per sample). On a hit the block is
    // silenced and the master-bus state that can hold the poison is cleared; a strip's own engine state is
    // not reachable from here.
    float probe = 0.0f;
    for (int c = 0; c < output.getNumChannels(); ++c)
    {
        const float* p = output.getReadPointer(c);
        for (int i = 0; i < numSamples; ++i)
            probe += p[i] * 0.0f;
    }
    if (! std::isfinite(probe))
    {
        output.clear(0, numSamples);
        fxChain.resetState();
        masterInsert.reset();
        masterInsert2.reset();
        for (auto& bus : buses)
            for (auto& ins : bus.insert)
                ins.processor.reset();
        // A NaN that reached a ducking envelope would latch it, so clear those too.
        for (auto& e : scEnv)    e = 0.0f;
        for (auto& e : scRetEnv) e = 0.0f;
        for (auto& e : scBusEnv) e = 0.0f;
        nonFiniteBlocks.fetch_add(1, std::memory_order_relaxed);
    }

    storePeak(output, numSamples, masterPeak, masterPeakL, masterPeakR);
}
