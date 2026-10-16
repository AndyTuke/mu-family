#pragma once

#include "InsertAlgorithmBase.h"
#include <atomic>
#include <cmath>

// insertAlgo = 9. Ring modulator — multiplies the input by a sinusoidal
// carrier at p.insertTone Hz. Per-channel phase accumulators preserve continuity
// across blocks. Mix knob (p.insertDrive) blends dry → ring-modulated.
class RingModInsert : public InsertAlgorithmBase
{
public:
    void prepare(double sampleRate, int) override
    {
        currentSampleRate = sampleRate;
        // 15 ms ramps eliminate "step" sound on freq sweeps and crackle on mix
        // movement.
        smoothedPhInc.reset(sampleRate, 0.015);  smoothedPhInc.setCurrentAndTargetValue(0.0f);
        smoothedMix  .reset(sampleRate, 0.015);  smoothedMix  .setCurrentAndTargetValue(0.0f);
        reset();
    }
    // Snaps the carrier increment to 0 Hz and the phase to 0, so the carrier stops dead. That is only
    // right where nothing is sounding and the audio thread is excluded (a layer move under suspend and
    // the layers lock). Never call it from a transport-stop or note-retrigger hook while a voice sounds:
    // the carrier would jump and click. The debug assert catches a call that overlaps process().
    void reset() override
    {
       #if JUCE_DEBUG
        jassert(! inProcess.load(std::memory_order_relaxed));
       #endif
        phase[0] = phase[1] = 0.0f;
        // Snap the smoothers to their prepare() values: a fresh voice ramps the carrier up from 0 Hz,
        // so a reset one must too, or the two carriers keep a constant phase offset.
        smoothedPhInc.setCurrentAndTargetValue(0.0f);
        smoothedMix  .setCurrentAndTargetValue(0.0f);
    }

    void process(juce::AudioBuffer<float>& buf, int ns, int nCh,
                 const VoiceParams& p, float& /*grOut*/) override
    {
        // Slot 0 = Mix 0..100 %, Slot 3 = Freq 10..5000 Hz (log).
        const float mix   = insertSlot(p, 0) / 100.0f;
        const float freq  = juce::jlimit(10.0f, 5000.0f, insertSlot(p, 3));
        const float phInc = 2.0f * juce::MathConstants<float>::pi * freq
                            / (float)currentSampleRate;
       #if JUCE_DEBUG
        inProcess.store(true, std::memory_order_relaxed);
       #endif
        smoothedPhInc.setTargetValue(phInc);
        smoothedMix  .setTargetValue(mix);
        // Sample-major: the phase increment and mix advance once per sample and every channel uses them, so the
        // two channels' carriers stay in step (channel-major looping let the left ramp its increment up from 0
        // while the right ran at full rate, leaving the phases offset for good).
        auto** chans = buf.getArrayOfWritePointers();
        for (int i = 0; i < ns; ++i)
        {
            const float pInc = smoothedPhInc.getNextValue();
            const float m    = smoothedMix  .getNextValue();
            for (int ch = 0; ch < nCh; ++ch)
            {
                float& ph = phase[ch < 2 ? ch : 0];
                const float carrier = std::sin(ph);
                ph += pInc;
                if (ph >= juce::MathConstants<float>::twoPi)
                    ph -= juce::MathConstants<float>::twoPi;
                chans[ch][i] = chans[ch][i] * (1.0f - m + carrier * m);
            }
        }
       #if JUCE_DEBUG
        inProcess.store(false, std::memory_order_relaxed);
       #endif
    }

private:
    double currentSampleRate = 44100.0;
    float  phase[2] = {};
   #if JUCE_DEBUG
    std::atomic<bool> inProcess { false };   // debug only: true while process() runs, so reset() can assert it never overlaps
   #endif
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedPhInc;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedMix;
};
