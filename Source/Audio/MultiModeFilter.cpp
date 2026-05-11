#include "MultiModeFilter.h"
#include <cmath>

void MultiModeFilter::prepare(double sampleRate, int blockSize, int numChannels)
{
    currentSampleRate = sampleRate;

    const auto chans = static_cast<juce::uint32>(juce::jlimit(1, MaxChannels, numChannels));
    svf   .prepare({ sampleRate, static_cast<juce::uint32>(blockSize), chans });
    ladder.prepare({ sampleRate, static_cast<juce::uint32>(blockSize), chans });
    ladder.setDrive(1.0f);

    notchScratch.setSize(MaxChannels, blockSize, false, true, false);

    // Comb max delay = sr / 20 Hz + a few guard samples; cleared on prepare.
    const int maxCombSamples = static_cast<int>(sampleRate / 20.0) + 4;
    for (int ch = 0; ch < MaxChannels; ++ch)
    {
        combBuffer[ch].assign(maxCombSamples, 0.0f);
        combWritePos[ch] = 0;
        lp6[ch].reset();
        hp6[ch].reset();
        eq [ch].reset();
    }

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
        std::fill(combBuffer[ch].begin(), combBuffer[ch].end(), 0.0f);
        combWritePos[ch] = 0;
    }
}

void MultiModeFilter::configureForCurrentType()
{
    using T = juce::dsp::StateVariableTPTFilterType;
    using M = juce::dsp::LadderFilter<float>::Mode;

    const float res = juce::jmax(0.01f, resonance);

    switch (typeCodeValue)
    {
        case 0:  svf.setType(T::lowpass);    svf.setCutoffFrequency(cutoffHz); svf.setResonance(res); break;
        case 1:  svf.setType(T::highpass);   svf.setCutoffFrequency(cutoffHz); svf.setResonance(res); break;
        case 2:  svf.setType(T::bandpass);   svf.setCutoffFrequency(cutoffHz); svf.setResonance(res); break;
        case 3:  svf.setType(T::bandpass);   svf.setCutoffFrequency(cutoffHz); svf.setResonance(res); break;
        case 4:  ladder.setMode(M::LPF24);   ladder.setCutoffFrequencyHz(cutoffHz); ladder.setResonance(res); break;
        case 5:  ladder.setMode(M::HPF24);   ladder.setCutoffFrequencyHz(cutoffHz); ladder.setResonance(res); break;
        case 6:  ladder.setMode(M::BPF24);   ladder.setCutoffFrequencyHz(cutoffHz); ladder.setResonance(res); break;
        case 10: ladder.setMode(M::BPF24);   ladder.setCutoffFrequencyHz(cutoffHz); ladder.setResonance(res); break;
        // 7=LP6, 8=Comb, 9=AP12, 11=HP6, 12-14=EQ: per-block coefficient compute in process().
        default: svf.setType(T::lowpass);    svf.setCutoffFrequency(cutoffHz); svf.setResonance(res); break;
    }
}

void MultiModeFilter::process(juce::AudioBuffer<float>& buffer, int numSamples, int numChannels)
{
    const int nCh = juce::jmin(numChannels, MaxChannels, buffer.getNumChannels());
    const int ns  = juce::jmin(numSamples, buffer.getNumSamples(), notchScratch.getNumSamples());
    if (ns <= 0 || nCh <= 0) return;

    const int fType = typeCodeValue;

    juce::dsp::AudioBlock<float> block(buffer.getArrayOfWritePointers(),
                                       static_cast<size_t>(nCh),
                                       static_cast<size_t>(0),
                                       static_cast<size_t>(ns));
    juce::dsp::ProcessContextReplacing<float> ctx(block);

    if (fType <= 3) // LP12 / HP12 / BP12 / Notch via SVF
    {
        svf.setCutoffFrequency(cutoffHz);

        if (fType == 3) // Notch: save pre-filter dry for dry − BP
            for (int ch = 0; ch < nCh; ++ch)
                notchScratch.copyFrom(ch, 0, buffer, ch, 0, ns);

        svf.process(ctx);

        if (fType == 3)
            for (int ch = 0; ch < nCh; ++ch)
            {
                auto*       data = buffer.getWritePointer(ch);
                const auto* dry  = notchScratch.getReadPointer(ch);
                for (int i = 0; i < ns; ++i)
                    data[i] = dry[i] - data[i];
            }
    }
    else if (fType >= 4 && fType <= 6) // LP24 / HP24 / BP24 via LadderFilter
    {
        ladder.setCutoffFrequencyHz(cutoffHz);
        ladder.process(ctx);
    }
    else if (fType == 10) // Notch24: dry − BP24
    {
        ladder.setCutoffFrequencyHz(cutoffHz);
        for (int ch = 0; ch < nCh; ++ch)
            notchScratch.copyFrom(ch, 0, buffer, ch, 0, ns);
        ladder.process(ctx);
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
        const float sr = static_cast<float>(currentSampleRate);
        for (int ch = 0; ch < nCh; ++ch)
        {
            lp6[ch].prepare(cutoffHz, sr);
            auto* data = buffer.getWritePointer(ch);
            for (int i = 0; i < ns; ++i)
                data[i] = lp6[ch].process(data[i]);
        }
    }
    else if (fType == 11) // HP 6 dB/oct
    {
        const float sr = static_cast<float>(currentSampleRate);
        for (int ch = 0; ch < nCh; ++ch)
        {
            hp6[ch].prepare(cutoffHz, sr);
            auto* data = buffer.getWritePointer(ch);
            for (int i = 0; i < ns; ++i)
                data[i] = hp6[ch].process(data[i]);
        }
    }
    else if (fType == 9 || fType >= 12) // AP12 / Peak / LoShelf / HiShelf via biquad
    {
        const float sr = static_cast<float>(currentSampleRate);
        const float q  = 0.1f + resonance * 9.9f; // 0..0.99 → 0.1..9.9
        for (int ch = 0; ch < nCh; ++ch)
        {
            if      (fType == 9)  eq[ch].setAllPass  (cutoffHz, q, sr);
            else if (fType == 12) eq[ch].setPeak     (cutoffHz, q, 12.0f, sr);
            else if (fType == 13) eq[ch].setLowShelf (cutoffHz, q, 12.0f, sr);
            else                  eq[ch].setHighShelf(cutoffHz, q, 12.0f, sr);
            auto* data = buffer.getWritePointer(ch);
            for (int i = 0; i < ns; ++i)
                data[i] = eq[ch].process(data[i]);
        }
    }
    else if (fType == 8 || fType == 15) // Feedback comb — positive (8) or negative (15) feedback
    {
        // Comb+ (type 8): y[n] = x[n] + g·y[n-D] → peaks at f0, 2f0, 3f0… (f0 = sr/D)
        // Comb- (type 15): y[n] = x[n] − g·y[n-D] → peaks at f0/2, 3f0/2, 5f0/2… (Karplus-Strong feel)
        const float delayF = static_cast<float>(currentSampleRate) / juce::jmax(20.0f, cutoffHz);
        const float g      = (fType == 8) ? resonance : -resonance;
        for (int ch = 0; ch < nCh; ++ch)
        {
            auto&     buf     = combBuffer[ch];
            int&      wPos    = combWritePos[ch];
            const int bufSize = static_cast<int>(buf.size());
            auto*     data    = buffer.getWritePointer(ch);
            for (int i = 0; i < ns; ++i)
            {
                const float readF = static_cast<float>(wPos) - delayF;
                const int   r0    = ((static_cast<int>(std::floor(readF)) % bufSize) + bufSize) % bufSize;
                const int   r1    = (r0 + 1) % bufSize;
                const float frac  = readF - std::floor(readF);
                const float delayed = buf[r0] + frac * (buf[r1] - buf[r0]);
                const float out   = data[i] + g * delayed;
                buf[wPos] = out;
                data[i]   = out;
                wPos = (wPos + 1) % bufSize;
            }
        }
    }
}
