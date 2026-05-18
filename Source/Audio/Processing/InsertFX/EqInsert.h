#pragma once

#include "InsertAlgorithmBase.h"
#include <juce_dsp/juce_dsp.h>

// #425: driveChar = 6. Three-band EQ — low shelf @ 200 Hz, mid peak @
// p.driveTone, high shelf @ 8 kHz. Coefficient computation is cached on the
// last-seen control values so unchanged params skip the cos/sin/pow calls.
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
        reset();
    }
    void reset() override
    {
        eqLow .reset();
        eqMid .reset();
        eqHigh.reset();
        lastDriveDrive = -1.0f;
        lastDrvDither  = -1.0f;
        lastMidGain    = -999.0f;
        lastDriveTone  = -1.0f;
    }

    void process(juce::AudioBuffer<float>& buf, int ns, int nCh,
                 const VoiceParams& p, float& /*grOut*/) override
    {
        using Coeffs = juce::dsp::IIR::Coefficients<float>;
        const float sr          = (float)currentSampleRate;
        const float curDriveDrv = p.driveDrive;
        const float curDrvDit   = p.drvDither;
        const float curMidGain  = p.eqMidGain;
        const float curMidFreq  = juce::jlimit(20.0f, 20000.0f, p.driveTone);

        if (curDriveDrv != lastDriveDrive || curDrvDit  != lastDrvDither
         || curMidGain  != lastMidGain    || curMidFreq != lastDriveTone)
        {
            const float lowG  = juce::Decibels::decibelsToGain(curDriveDrv / 100.0f * 36.0f - 18.0f);
            const float highG = juce::Decibels::decibelsToGain(curDrvDit   / 100.0f * 36.0f - 18.0f);
            const float midG  = juce::Decibels::decibelsToGain(curMidGain);

            *eqLow .state = *Coeffs::makeLowShelf  (sr, 200.0f,    0.7f, lowG);
            *eqMid .state = *Coeffs::makePeakFilter(sr, curMidFreq, 1.0f, midG);
            *eqHigh.state = *Coeffs::makeHighShelf (sr, 8000.0f,   0.7f, highG);

            lastDriveDrive = curDriveDrv;
            lastDrvDither  = curDrvDit;
            lastMidGain    = curMidGain;
            lastDriveTone  = curMidFreq;
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

    // #368 change-detection cache so cos/sin/pow only runs when a param moves.
    float lastDriveDrive = -1.0f;
    float lastDrvDither  = -1.0f;
    float lastMidGain    = -999.0f;
    float lastDriveTone  = -1.0f;
};
