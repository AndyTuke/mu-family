#pragma once

#include "Audio/FX/Insert/InsertAlgorithmBase.h"
#include "Audio/FX/Algorithms/FxUse.h"
#include "Audio/FX/Algorithms/InsertSupport.h"
#include "Audio/FX/Algorithms/HilbertIir.h"
#include <cmath>

namespace mu_audio
{
// insertAlgo = 26. Frequency shifter: every partial moves by the same number of Hz, so a harmonic note becomes an inharmonic,
// metallic stack (a pitch shifter would move them by the same ratio). Single sideband, unlike RingMod, which keeps both
// the sum and the difference.
//   - Shift     -1000..1000 Hz: the coarse shift (negative moves down).
//   - Fine      -20..20 Hz: added to Shift; slow beating and barber-pole phasing at small values.
//   - Feedback  0..80 %: the shifted output back into the input through a fixed 8 kHz low-pass (a spiral of repeats).
//   - Mix       0..100 %: dry / wet; 0 leaves the signal untouched.
// The analytic signal comes from HilbertIir (accurate from about 0.002 of the sample rate: about 90 Hz at 44.1 kHz); it is
// rotated by a quadrature oscillator, wet = I cos(phi) - Q sin(phi), and phi advances by the shift each sample.
// Content within the shift of Nyquist folds back. A shifter preserves power, so there is no makeup gain. No look-ahead.
class FreqShiftInsert : public InsertAlgorithmBase
{
public:
    static constexpr FxUse kUse = FxUse::Insert;

    void prepare(double sampleRate, int) override
    {
        sr = sampleRate > 0.0 ? sampleRate : 44100.0;
        shiftSm.reset(sr, 0.015);
        fbSm.reset(sr, 0.015);
        mixSm.reset(sr, 0.015);
        fbLpCoeff = std::exp(-juce::MathConstants<float>::twoPi * 8000.0f / (float) sr);
        reset();
    }

    void reset() override
    {
        for (auto& h : hilbert) h.reset();
        fbState[0] = fbState[1] = 0.0f;
        osc = { 1.0, 0.0 };
        primed = false;
        wasActive = true;
        sinceStep = 0;
    }

    void process(juce::AudioBuffer<float>& buf, int ns, int nCh,
                 const VoiceParams& p, float& /*grOut*/) override
    {
        using namespace insert_support;
        const int nChState = juce::jmin(nCh, 2);
        if (nChState <= 0 || ns <= 0) return;
        // Slot 0 = Shift Hz, 1 = Fine Hz, 2 = Feedback 0..80 %, 3 = Mix 0..100 %.
        const float shift = juce::jlimit(-1000.0f, 1000.0f, finiteOr(insertSlot(p, 0), 60.0f))
                          + juce::jlimit(-20.0f, 20.0f, finiteOr(insertSlot(p, 1), 0.0f));
        const float fb  = juce::jlimit(0.0f, 0.8f, finiteOr(insertSlot(p, 2), 0.0f) * 0.01f);
        const float mix = juce::jlimit(0.0f, 1.0f, finiteOr(insertSlot(p, 3), 50.0f) * 0.01f);
        if (! primed)
        {
            shiftSm.setCurrentAndTargetValue(shift);
            fbSm.setCurrentAndTargetValue(fb);
            mixSm.setCurrentAndTargetValue(mix);
            primed = true;
        }
        shiftSm.setTargetValue(shift);
        fbSm.setTargetValue(fb);
        mixSm.setTargetValue(mix);

        // Mix 0 and settled: untouched; the network is cleared so it restarts clean when the mix comes back.
        if (mix <= 0.0f && ! mixSm.isSmoothing() && mixSm.getCurrentValue() <= 0.0f)
        {
            if (wasActive)
            {
                for (auto& h : hilbert) h.reset();
                fbState[0] = fbState[1] = 0.0f;
                wasActive = false;
            }
            return;
        }
        wasActive = true;

        auto chans = buf.getArrayOfWritePointers();
        const double twoPiOverSr = 2.0 * juce::MathConstants<double>::pi / sr;
        // Per sample: the Hilbert pair of each channel is rotated by the shared oscillator and mixed back; the oscillator step
        // is refreshed every 16 samples (or whenever the shift glides) by recomputing the rotation increment.
        for (int i = 0; i < ns; ++i)
        {
            const float sh = shiftSm.getNextValue();
            const float g  = fbSm.getNextValue();
            const float m  = mixSm.getNextValue();
            if (sinceStep == 0 || shiftSm.isSmoothing())
            {
                const double dphi = twoPiOverSr * (double) sh;
                stepC = std::cos(dphi);
                stepS = std::sin(dphi);
            }
            sinceStep = (sinceStep + 1) & 15;
            const float c = (float) osc.c, s = (float) osc.s;
            for (int ch = 0; ch < nChState; ++ch)
            {
                const float x = sane(chans[ch][i]);
                float in = x + g * fbState[ch];
                in = juce::jlimit(-1.0e4f, 1.0e4f, in);
                float I, Q;
                hilbert[ch].process(in, I, Q);
                const float wet = I * c - Q * s;
                fbState[ch] = flush((1.0f - fbLpCoeff) * wet + fbLpCoeff * fbState[ch]);
                chans[ch][i] = x + m * (wet - x);
            }
            // Rotate the oscillator by one step (a complex multiply).
            const double nc = osc.c * stepC - osc.s * stepS;
            const double ns2 = osc.s * stepC + osc.c * stepS;
            osc = { nc, ns2 };
        }
        // Renormalise once per block so the rotation does not drift in amplitude.
        const double mag = std::sqrt(osc.c * osc.c + osc.s * osc.s);
        if (mag > 0.0) { osc.c /= mag; osc.s /= mag; } else osc = { 1.0, 0.0 };
    }

private:
    struct Rot { double c = 1.0, s = 0.0; };

    double sr = 44100.0;
    HilbertIir hilbert[2];
    float  fbState[2] = {};
    float  fbLpCoeff = 0.0f;
    Rot    osc;
    double stepC = 1.0, stepS = 0.0;
    int    sinceStep = 0;
    bool   primed = false;
    bool   wasActive = true;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> shiftSm, fbSm, mixSm;
};
}  // namespace mu_audio
