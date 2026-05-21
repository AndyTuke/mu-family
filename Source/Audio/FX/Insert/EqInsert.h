#pragma once

#include "InsertAlgorithmBase.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <cmath>

// Three-band EQ with per-sample-smoothed biquad coefficients.
//
// Each of the three biquad filters (low shelf, mid peak, high shelf) carries
// five SmoothedValue<float> members for b0/b1/b2/a1/a2. Target coefficients
// are computed once per block from the user knob values (cookbook formulas
// from RBJ Audio EQ Cookbook); the coefficients themselves then ramp toward
// those targets over 15 ms while the filter runs. This eliminates the
// zipper noise that a direct per-block coefficient swap produces — even on
// fast Mid-Hz sweeps the audible response moves smoothly because the biquad
// coefficients themselves never step.
//
// Stability: for peak/shelf filters at the Q values used here (0.7–1.0) and
// the user-reachable gain range (±18 dB), the pole positions stay safely
// inside the unit circle for every intermediate coefficient set the smoother
// traverses, so no instability can arise during a ramp.
class EqInsert : public InsertAlgorithmBase
{
public:
    void prepare(double sampleRate, int /*blockSize*/) override
    {
        currentSampleRate = sampleRate;
        for (auto* band : { &lowL, &lowR, &midL, &midR, &highL, &highR })
            band->prepare(sampleRate);
        // Wait for the first process() call to know the actual target. Until
        // then the filter sits at identity (passthrough) so initial samples
        // before the user has touched any knob play through cleanly.
        firstBlock = true;
    }

    void reset() override
    {
        for (auto* band : { &lowL, &lowR, &midL, &midR, &highL, &highR })
            band->resetState();
        firstBlock = true;
    }

    void process(juce::AudioBuffer<float>& buf, int ns, int nCh,
                 const VoiceParams& p, float& /*grOut*/) override
    {
        const float sr      = static_cast<float>(currentSampleRate);
        const float lowDb   = p.insertDrive  / 100.0f * 36.0f - 18.0f;
        const float highDb  = p.insertDither / 100.0f * 36.0f - 18.0f;
        const float midDb   = p.insertEqMid;
        const float midFreq = juce::jlimit(20.0f, 20000.0f, p.insertTone);

        // Push the latest target coefficients into each band. On the first
        // block after prepare/reset, snap to the targets instantly so the
        // initial response is the user's chosen EQ rather than a 15 ms ramp
        // up from passthrough.
        const bool instant = firstBlock;
        for (auto* band : { &lowL, &lowR })
            band->setLowShelf(200.0f, 0.7f, lowDb, sr, instant);
        for (auto* band : { &midL, &midR })
            band->setPeak(midFreq, 1.0f, midDb, sr, instant);
        for (auto* band : { &highL, &highR })
            band->setHighShelf(8000.0f, 0.7f, highDb, sr, instant);
        firstBlock = false;

        // Cascade the three bands per channel: low → mid → high.
        const int nChClamped = juce::jmin(nCh, buf.getNumChannels());
        for (int ch = 0; ch < nChClamped; ++ch)
        {
            auto*       data = buf.getWritePointer(ch);
            SmoothBiquad& l   = (ch == 0) ? lowL  : lowR;
            SmoothBiquad& m   = (ch == 0) ? midL  : midR;
            SmoothBiquad& h   = (ch == 0) ? highL : highR;
            for (int i = 0; i < ns; ++i)
                data[i] = h.processNext(m.processNext(l.processNext(data[i])));
        }
    }

private:
    // Biquad in transposed direct form II with each coefficient driven by a
    // SmoothedValue<float, Linear> so coefficient changes ramp over 15 ms
    // instead of stepping at block boundaries.
    struct SmoothBiquad
    {
        juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> b0, b1, b2, a1, a2;
        float z1 = 0.0f;
        float z2 = 0.0f;

        void prepare(double sr)
        {
            constexpr double kRampSec = 0.015;
            b0.reset(sr, kRampSec);  b0.setCurrentAndTargetValue(1.0f);
            b1.reset(sr, kRampSec);  b1.setCurrentAndTargetValue(0.0f);
            b2.reset(sr, kRampSec);  b2.setCurrentAndTargetValue(0.0f);
            a1.reset(sr, kRampSec);  a1.setCurrentAndTargetValue(0.0f);
            a2.reset(sr, kRampSec);  a2.setCurrentAndTargetValue(0.0f);
            z1 = z2 = 0.0f;
        }

        void resetState() noexcept { z1 = z2 = 0.0f; }

        // RBJ Audio EQ Cookbook — peak / bell filter.
        void setPeak(float fHz, float q, float gainDb, float sr, bool instant)
        {
            const float A     = std::pow(10.0f, gainDb / 40.0f);
            const float w0    = juce::MathConstants<float>::twoPi * fHz / sr;
            const float cw    = std::cos(w0);
            const float sw    = std::sin(w0);
            const float alpha = sw / (2.0f * q);
            applyTargets(1.0f + alpha * A, -2.0f * cw, 1.0f - alpha * A,
                         1.0f + alpha / A, -2.0f * cw, 1.0f - alpha / A, instant);
        }

        // RBJ Audio EQ Cookbook — low shelf.
        void setLowShelf(float fHz, float q, float gainDb, float sr, bool instant)
        {
            const float A     = std::pow(10.0f, gainDb / 40.0f);
            const float w0    = juce::MathConstants<float>::twoPi * fHz / sr;
            const float cw    = std::cos(w0);
            const float sw    = std::sin(w0);
            const float alpha = sw / (2.0f * q);
            const float sa    = 2.0f * std::sqrt(A) * alpha;
            applyTargets( A * ((A + 1.0f) - (A - 1.0f) * cw + sa),
                          A * 2.0f * ((A - 1.0f) - (A + 1.0f) * cw),
                          A * ((A + 1.0f) - (A - 1.0f) * cw - sa),
                          (A + 1.0f) + (A - 1.0f) * cw + sa,
                         -2.0f * ((A - 1.0f) + (A + 1.0f) * cw),
                          (A + 1.0f) + (A - 1.0f) * cw - sa, instant);
        }

        // RBJ Audio EQ Cookbook — high shelf.
        void setHighShelf(float fHz, float q, float gainDb, float sr, bool instant)
        {
            const float A     = std::pow(10.0f, gainDb / 40.0f);
            const float w0    = juce::MathConstants<float>::twoPi * fHz / sr;
            const float cw    = std::cos(w0);
            const float sw    = std::sin(w0);
            const float alpha = sw / (2.0f * q);
            const float sa    = 2.0f * std::sqrt(A) * alpha;
            applyTargets( A * ((A + 1.0f) + (A - 1.0f) * cw + sa),
                          A * -2.0f * ((A - 1.0f) + (A + 1.0f) * cw),
                          A * ((A + 1.0f) + (A - 1.0f) * cw - sa),
                          (A + 1.0f) - (A - 1.0f) * cw + sa,
                          2.0f * ((A - 1.0f) - (A + 1.0f) * cw),
                          (A + 1.0f) - (A - 1.0f) * cw - sa, instant);
        }

        float processNext(float x) noexcept
        {
            // Pull the next ramp value for every coefficient so each sample
            // sees a slightly different biquad — this is what eliminates the
            // zipper noise that a per-block coefficient swap produces.
            const float B0 = b0.getNextValue();
            const float B1 = b1.getNextValue();
            const float B2 = b2.getNextValue();
            const float A1 = a1.getNextValue();
            const float A2 = a2.getNextValue();
            const float y  = B0 * x + z1;
            z1 = B1 * x - A1 * y + z2;
            z2 = B2 * x - A2 * y;
            return y;
        }

    private:
        // Normalise raw biquad coefficients by A0 and push them as targets to
        // the five smoothers. When `instant` is true (first block after
        // prepare/reset) the smoothers snap directly so the EQ starts at the
        // user's chosen response rather than ramping up from passthrough.
        void applyTargets(float B0, float B1, float B2,
                          float A0, float A1, float A2, bool instant)
        {
            const float inv = 1.0f / A0;
            const float nb0 = B0 * inv;
            const float nb1 = B1 * inv;
            const float nb2 = B2 * inv;
            const float na1 = A1 * inv;
            const float na2 = A2 * inv;
            if (instant)
            {
                b0.setCurrentAndTargetValue(nb0);
                b1.setCurrentAndTargetValue(nb1);
                b2.setCurrentAndTargetValue(nb2);
                a1.setCurrentAndTargetValue(na1);
                a2.setCurrentAndTargetValue(na2);
            }
            else
            {
                b0.setTargetValue(nb0);
                b1.setTargetValue(nb1);
                b2.setTargetValue(nb2);
                a1.setTargetValue(na1);
                a2.setTargetValue(na2);
            }
        }
    };

    double currentSampleRate = 44100.0;
    SmoothBiquad lowL,  lowR;
    SmoothBiquad midL,  midR;
    SmoothBiquad highL, highR;
    bool firstBlock = true;
};
