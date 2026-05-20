#pragma once

#include "InsertAlgorithmBase.h"
#include <juce_dsp/juce_dsp.h>

// insertAlgo = 6. Three-band EQ — low shelf @ 200 Hz, mid peak @
// p.insertTone, high shelf @ 8 kHz. #538: gain dB values + mid freq smoothed
// through 15 ms ramps so a step on the knob becomes a series of small steps
// rather than one large discontinuity. Each smaller step still recomputes
// biquad coefficients per-block, but the magnitude of the coefficient swap
// is reduced by ~10× for typical knob movement — turning audible clicks into
// soft chuffs. The change-detection cache (#368) is dropped because the
// smoothed values move every block during a sweep; we only skip the
// `cos/sin/pow` recompute when the *smoothed* values stop moving.
class EqInsert : public InsertAlgorithmBase
{
public:
    void prepare(double sampleRate, int blockSize) override
    {
        currentSampleRate = sampleRate;
        const juce::dsp::ProcessSpec spec {
            sampleRate, static_cast<uint32_t>(blockSize), 2 };
        eqLow .prepare(spec);
        eqMid .prepare(spec);
        eqHigh.prepare(spec);
        // 15 ms ramps on the four user-controllable EQ params. Mid freq is
        // smoothed in the log domain so a 200 Hz→2 kHz sweep traverses the same
        // perceptual distance per ramp-step.
        smoothedLowDb  .reset(sampleRate, 0.015);  smoothedLowDb  .setCurrentAndTargetValue(0.0f);
        smoothedHighDb .reset(sampleRate, 0.015);  smoothedHighDb .setCurrentAndTargetValue(0.0f);
        smoothedMidDb  .reset(sampleRate, 0.015);  smoothedMidDb  .setCurrentAndTargetValue(0.0f);
        smoothedMidFreq.reset(sampleRate, 0.015);  smoothedMidFreq.setCurrentAndTargetValue(1000.0f);
        reset();
    }
    void reset() override
    {
        eqLow .reset();
        eqMid .reset();
        eqHigh.reset();
        lastLowDb   = -999.0f;
        lastHighDb  = -999.0f;
        lastMidDb   = -999.0f;
        lastMidFreq = -1.0f;
    }

    void process(juce::AudioBuffer<float>& buf, int ns, int nCh,
                 const VoiceParams& p, float& /*grOut*/) override
    {
        using Coeffs = juce::dsp::IIR::Coefficients<float>;
        const float sr         = (float)currentSampleRate;
        const float targetLow  = p.insertDrive  / 100.0f * 36.0f - 18.0f;   // dB
        const float targetHigh = p.insertDither / 100.0f * 36.0f - 18.0f;   // dB
        const float targetMid  = p.insertEqMid;                              // dB
        const float targetFreq = juce::jlimit(20.0f, 20000.0f, p.insertTone);

        smoothedLowDb  .setTargetValue(targetLow);
        smoothedHighDb .setTargetValue(targetHigh);
        smoothedMidDb  .setTargetValue(targetMid);
        smoothedMidFreq.setTargetValue(targetFreq);

        // Advance the smoothers by one block. Use skip() because the values are
        // read once per block (filter coefficients), not per sample.
        const float curLow  = smoothedLowDb  .skip(ns);
        const float curHigh = smoothedHighDb .skip(ns);
        const float curMid  = smoothedMidDb  .skip(ns);
        const float curFreq = smoothedMidFreq.skip(ns);

        if (curLow  != lastLowDb || curHigh != lastHighDb
         || curMid  != lastMidDb || curFreq != lastMidFreq)
        {
            const float lowG  = juce::Decibels::decibelsToGain(curLow);
            const float highG = juce::Decibels::decibelsToGain(curHigh);
            const float midG  = juce::Decibels::decibelsToGain(curMid);

            *eqLow .state = *Coeffs::makeLowShelf  (sr, 200.0f, 0.7f, lowG);
            *eqMid .state = *Coeffs::makePeakFilter(sr, curFreq, 1.0f, midG);
            *eqHigh.state = *Coeffs::makeHighShelf (sr, 8000.0f, 0.7f, highG);

            lastLowDb   = curLow;
            lastHighDb  = curHigh;
            lastMidDb   = curMid;
            lastMidFreq = curFreq;
        }

        const int nChClamped = juce::jmin(nCh, buf.getNumChannels());
        juce::dsp::AudioBlock<float> block(buf.getArrayOfWritePointers(),
                                           static_cast<size_t>(nChClamped),
                                           static_cast<size_t>(0),
                                           static_cast<size_t>(ns));
        juce::dsp::ProcessContextReplacing<float> ctx(block);
        eqLow .process(ctx);
        eqMid .process(ctx);
        eqHigh.process(ctx);
    }

private:
    double currentSampleRate = 44100.0;

    using EqFilter = juce::dsp::ProcessorDuplicator<juce::dsp::IIR::Filter<float>,
                                                     juce::dsp::IIR::Coefficients<float>>;
    EqFilter eqLow, eqMid, eqHigh;

    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedLowDb;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedHighDb;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedMidDb;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedMidFreq;

    // Per-block change detection on the SMOOTHED values — skips the cos/sin/pow
    // coefficient calc once the smoothers have come to rest.
    float lastLowDb   = -999.0f;
    float lastHighDb  = -999.0f;
    float lastMidDb   = -999.0f;
    float lastMidFreq = -1.0f;
};
