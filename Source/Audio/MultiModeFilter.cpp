#include "MultiModeFilter.h"
#include <cmath>

namespace
{
    // Resonance scaling: user-facing filterRes 0..1 with a knee at 0.8 that lands
    // on each engine's self-oscillation threshold, and the top 0.8..1.0 fifth of
    // the knob covering "well past self-oscillation" (the screaming/ringing zone).
    // Self-oscillation thresholds per engine:
    //   SVF:    0.7  (max engine 0.95)
    //   Ladder: 0.95 (max engine 1.0)
    //   Comb:   0.95 (max engine 0.99)
    //   Biquad: Q 6.5 (max Q 9.9)
    //
    // Piecewise linear: 0..0.8 → 0..threshold, 0.8..1.0 → threshold..max.
    inline float kneeMap(float userRes, float threshold, float maxValue) noexcept
    {
        const float r = juce::jlimit(0.0f, 1.0f, userRes);
        return r <= 0.8f ? r * (threshold / 0.8f)
                         : threshold + (r - 0.8f) * (maxValue - threshold) * 5.0f;
    }

    inline float resForSvf    (float r) noexcept { return kneeMap(r, 0.7f,  0.95f); }
    inline float resForLadder (float r) noexcept { return kneeMap(r, 0.95f, 1.0f);  }
    inline float resForComb   (float r) noexcept { return kneeMap(r, 0.95f, 0.99f); }

    // Biquad Q is the only mapping that doesn't start at 0 — the lowest useful
    // Q is ~0.1 (very wide, gentle resonance), the knee sits at Q 6.5, and the
    // top of the knob reaches Q 9.9 for very narrow/sharp peaks. Same piecewise
    // structure as kneeMap but with the offset.
    inline float resForBiquadQ(float r) noexcept
    {
        const float clamped = juce::jlimit(0.0f, 1.0f, r);
        return clamped <= 0.8f ? 0.1f + clamped * (6.5f - 0.1f) / 0.8f
                               : 6.5f + (clamped - 0.8f) * (9.9f - 6.5f) / 0.2f;
    }
}

void MultiModeFilter::prepare(double sampleRate, int blockSize, int numChannels)
{
    currentSampleRate = sampleRate;

    const auto chans = static_cast<juce::uint32>(juce::jlimit(1, MaxChannels, numChannels));
    svf   .prepare({ sampleRate, static_cast<juce::uint32>(blockSize), chans });
    ladder.prepare({ sampleRate, static_cast<juce::uint32>(blockSize), chans });
    ladder.setDrive(1.0f);

    notchScratch.setSize(MaxChannels, blockSize, false, true, false);

    // Cutoff smoother: 5 ms multiplicative ramp. Log-space ramp matches the
    // perceptual scale of frequency, so envelope sweeps trace musically-even
    // curves regardless of where in the audible band they're operating.
    smoothedCutoff.reset(sampleRate, 0.005);
    smoothedCutoff.setCurrentAndTargetValue(juce::jmax(20.0f, cutoffHz));

    // Comb buffer max delay. Floor at 40 Hz (was 20 Hz) — comb resonance below
    // 40 Hz is rarely musical on percussion, and halving the floor halves the
    // per-instance memory (~75 KB → ~38 KB at 192 kHz).
    const int maxCombSamples = static_cast<int>(sampleRate / 40.0) + 4;
    for (int ch = 0; ch < MaxChannels; ++ch)
    {
        combBuffer[ch].assign(maxCombSamples, 0.0f);
        combWritePos[ch] = 0;
        lp6[ch].reset();
        hp6[ch].reset();
        eq [ch].reset();
        // 15 Hz HP — well below the comb's lowest fundamental so a real
        // 20 Hz comb peak passes through almost untouched, but DC bias on
        // the feedback path gets stripped.
        combDcBlocker[ch].prepare(15.0f, static_cast<float>(sampleRate));
        combDcBlocker[ch].reset();
    }

    eqLastCutoff = -1.0f;
    eqLastRes    = -1.0f;
    eqLastGain   = -999.0f;
    eqLastType   = -1;

    configureForCurrentType();
}

void MultiModeFilter::reset()
{
    svf.reset();
    ladder.reset();
    for (int ch = 0; ch < MaxChannels; ++ch)
    {
        lp6[ch].reset();
        hp6[ch].reset();
        eq [ch].reset();
        combDcBlocker[ch].reset();
        std::fill(combBuffer[ch].begin(), combBuffer[ch].end(), 0.0f);
        combWritePos[ch] = 0;
    }
    eqLastCutoff = -1.0f;
    eqLastRes    = -1.0f;
    eqLastGain   = -999.0f;
    eqLastType   = -1;
    smoothedCutoff.setCurrentAndTargetValue(juce::jmax(20.0f, cutoffHz));
}

void MultiModeFilter::configureForCurrentType()
{
    using T = juce::dsp::StateVariableTPTFilterType;
    using M = juce::dsp::LadderFilter<float>::Mode;

    // Only configures the *engine* (SVF mode / Ladder mode). The per-block
    // process() pushes cutoff and resonance to whichever engine is active for
    // the current type, so types that fall outside this switch are intentional
    // no-ops here — their engines are picked up directly in process().
    switch (typeCodeValue)
    {
        case 0:  svf.setType(T::lowpass);    break;
        case 1:  svf.setType(T::highpass);   break;
        case 2:                              // BP12
        case 3:  svf.setType(T::bandpass);   break;   // Notch12 = dry − BP via SVF bandpass
        case 4:  ladder.setMode(M::LPF24);   break;
        case 5:  ladder.setMode(M::HPF24);   break;
        case 6:                              // BP24
        case 10: ladder.setMode(M::BPF24);   break;   // Notch24 = dry − BP via Ladder bandpass
        default: break;                               // 7/8/9/11/12–15 configured in process()
    }
}

void MultiModeFilter::process(juce::AudioBuffer<float>& buffer, int numSamples, int numChannels)
{
    const int nCh = juce::jmin(numChannels, MaxChannels, buffer.getNumChannels());
    const int ns  = juce::jmin(numSamples, buffer.getNumSamples(), notchScratch.getNumSamples());
    if (ns <= 0 || nCh <= 0) return;

    const int   fType         = typeCodeValue;
    const float sr            = static_cast<float>(currentSampleRate);
    // Cutoff safety clamp: SVF / Ladder / biquad coefficients lose accuracy
    // above ~sr/2.2. Clamp to sr·0.45 (well inside the stable range) and floor
    // at 20 Hz. Drives the smoother's TARGET — every per-sample read pulls a
    // smoothed value from the SmoothedValue inside the loops below.
    const float maxSafeCutoff = sr * 0.45f;
    const float safeCutoff    = juce::jlimit(20.0f, maxSafeCutoff, cutoffHz);
    smoothedCutoff.setTargetValue(safeCutoff);

    if (fType <= 3) // LP12 / HP12 / BP12 / Notch via SVF
    {
        svf.setResonance(resForSvf(resonance));

        if (fType == 3) // Notch: save pre-filter dry for dry − BP
            for (int ch = 0; ch < nCh; ++ch)
                notchScratch.copyFrom(ch, 0, buffer, ch, 0, ns);

        // Per-sample cutoff update so envelope/mod sweeps trace a smooth log
        // ramp inside each block, eliminating block-rate staircase artifacts.
        for (int i = 0; i < ns; ++i)
        {
            const float c = smoothedCutoff.getNextValue();
            svf.setCutoffFrequency(c);
            for (int ch = 0; ch < nCh; ++ch)
            {
                auto* data = buffer.getWritePointer(ch);
                data[i] = svf.processSample(ch, data[i]);
            }
        }

        if (fType == 3)
            for (int ch = 0; ch < nCh; ++ch)
            {
                auto*       data = buffer.getWritePointer(ch);
                const auto* dry  = notchScratch.getReadPointer(ch);
                for (int i = 0; i < ns; ++i)
                    data[i] = dry[i] - data[i];
            }
    }
    else if ((fType >= 4 && fType <= 6) || fType == 10) // Ladder LP24/HP24/BP24, Notch24
    {
        // LadderFilter::processSample is protected, so the SVF path's per-sample
        // approach won't compile against the Ladder. Use 32-sample sub-blocks
        // instead — cutoff updates every ~0.67 ms at 48 kHz, fine enough that
        // the staircase from block-rate updates becomes inaudible.
        ladder.setResonance(resForLadder(resonance));

        const bool isNotch = (fType == 10);
        if (isNotch)
            for (int ch = 0; ch < nCh; ++ch)
                notchScratch.copyFrom(ch, 0, buffer, ch, 0, ns);

        constexpr int kSubBlock = 32;
        int processed = 0;
        while (processed < ns)
        {
            const int chunk = juce::jmin(kSubBlock, ns - processed);
            const float c = smoothedCutoff.getNextValue();
            if (chunk > 1) smoothedCutoff.skip(chunk - 1);
            ladder.setCutoffFrequencyHz(c);

            juce::dsp::AudioBlock<float> sub(buffer.getArrayOfWritePointers(),
                                             static_cast<size_t>(nCh),
                                             static_cast<size_t>(processed),
                                             static_cast<size_t>(chunk));
            juce::dsp::ProcessContextReplacing<float> subCtx(sub);
            ladder.process(subCtx);
            processed += chunk;
        }

        if (isNotch)
            for (int ch = 0; ch < nCh; ++ch)
            {
                auto*       data = buffer.getWritePointer(ch);
                const auto* dry  = notchScratch.getReadPointer(ch);
                for (int i = 0; i < ns; ++i)
                    data[i] = dry[i] - data[i];
            }
    }
    else if (fType == 7) // LP 6 dB/oct
    {
        for (int i = 0; i < ns; ++i)
        {
            const float c = smoothedCutoff.getNextValue();
            for (int ch = 0; ch < nCh; ++ch)
            {
                lp6[ch].prepare(c, sr);
                auto* data = buffer.getWritePointer(ch);
                data[i] = lp6[ch].process(data[i]);
            }
        }
    }
    else if (fType == 11) // HP 6 dB/oct
    {
        for (int i = 0; i < ns; ++i)
        {
            const float c = smoothedCutoff.getNextValue();
            for (int ch = 0; ch < nCh; ++ch)
            {
                hp6[ch].prepare(c, sr);
                auto* data = buffer.getWritePointer(ch);
                data[i] = hp6[ch].process(data[i]);
            }
        }
    }
    else if (fType == 9 || fType >= 12) // AP12 / Peak / LoShelf / HiShelf via biquad
    {
        // Biquad coefficient recompute is the expensive path (pow/cos/sin/sqrt
        // per `setX` call) so we don't push it per sample — instead the smoothed
        // cutoff is sampled once per block, the cache check skips recompute on
        // stable parameters, and the smoother still ramps the *target* between
        // blocks so successive blocks land on a smooth curve.
        const float c    = smoothedCutoff.skip(ns);   // advance smoother by ns
        const float q    = resForBiquadQ(resonance);
        // Peak / LoShf / HiShf use the user-facing gain knob (#6). AP12 ignores
        // gain entirely — it's a pure phase-shift filter.
        const float gain = juce::jlimit(-18.0f, 18.0f, gainDbValue);

        if (c != eqLastCutoff || resonance != eqLastRes || fType != eqLastType
                              || gain != eqLastGain)
        {
            for (int ch = 0; ch < nCh; ++ch)
            {
                if      (fType == 9)  eq[ch].setAllPass  (c, q, sr);
                else if (fType == 12) eq[ch].setPeak     (c, q, gain, sr);
                else if (fType == 13) eq[ch].setLowShelf (c, q, gain, sr);
                else                  eq[ch].setHighShelf(c, q, gain, sr);
            }
            eqLastCutoff = c;
            eqLastRes    = resonance;
            eqLastType   = fType;
            eqLastGain   = gain;
        }

        for (int ch = 0; ch < nCh; ++ch)
        {
            auto* data = buffer.getWritePointer(ch);
            for (int i = 0; i < ns; ++i)
                data[i] = eq[ch].process(data[i]);
        }
    }
    else if (fType == 8 || fType == 15) // Feedback comb — positive (8) or negative (15) feedback
    {
        // Comb+ (type 8): y[n] = x[n] + g·y[n-D] → peaks at f0, 2f0, 3f0… (f0 = sr/D)
        // Comb- (type 15): y[n] = x[n] − g·y[n-D] → peaks at f0/2, 3f0/2, 5f0/2…
        //
        // Hermite (4-point Catmull-Rom) interpolation on the delay read replaces
        // the previous linear interp — gives a cleaner resonant peak at short
        // delays (high cutoff), where linear interp smeared the response.
        //
        // 1-pole HP at 15 Hz on the feedback path strips any DC bias from the
        // input before it can accumulate to `1 / (1 - g)` gain through the loop.
        //
        // Cutoff floor matches the buffer's f0_min = sr/40 — below that the
        // delay would exceed the allocated buffer.
        const float gMag = resForComb(resonance);
        const float g    = (fType == 8) ? gMag : -gMag;

        for (int ch = 0; ch < nCh; ++ch)
        {
            auto&     buf     = combBuffer[ch];
            int&      wPos    = combWritePos[ch];
            const int bufSize = static_cast<int>(buf.size());
            auto*     data    = buffer.getWritePointer(ch);
            auto&     dcHP    = combDcBlocker[ch];

            for (int i = 0; i < ns; ++i)
            {
                const float c = smoothedCutoff.getNextValue();
                const float delayF = juce::jmax(2.0f, sr / juce::jmax(40.0f, c));

                const float readF = static_cast<float>(wPos) - delayF;
                const int   baseI = static_cast<int>(std::floor(readF));
                const float frac  = readF - static_cast<float>(baseI);

                // xm1 newer than x0, x1/x2 older — matches DelaySlot's hermiteDelay.
                const int r0  = ((baseI       % bufSize) + bufSize) % bufSize;
                const int rm1 = (((baseI + 1) % bufSize) + bufSize) % bufSize;
                const int r1  = (((baseI - 1) % bufSize) + bufSize) % bufSize;
                const int r2  = (((baseI - 2) % bufSize) + bufSize) % bufSize;

                const float xm1 = buf[rm1];
                const float x0  = buf[r0];
                const float x1  = buf[r1];
                const float x2  = buf[r2];

                const float c0 = x0;
                const float c1 = 0.5f * (x1 - xm1);
                const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
                const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
                const float delayed = ((c3 * frac + c2) * frac + c1) * frac + c0;

                const float feedback = dcHP.process(g * delayed);
                const float out      = data[i] + feedback;
                buf[wPos] = out;
                data[i]   = out;
                wPos = (wPos + 1) % bufSize;
            }
        }
    }
}
