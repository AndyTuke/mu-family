#pragma once

#include "InsertAlgorithmBase.h"
#include "Audio/FX/TapeSpringEcho.h"

#include <cmath>

// insertAlgo = 16. Tape Spring: a three-head tape echo with a spring reverb (Audio/FX/TapeSpringEcho.h), a twelve-position mode
// choosing the heads and the spring, on the four generic insert knobs.
//   - Mode       1..12: heads 1, 2, 3 singly, heads 2+3, the heads singly with spring, pairs with spring, all three with spring, spring alone.
//   - Rate       the tape speed: 0 % slow (long repeats) .. 100 % fast (short repeats); turning it bends the repeats' pitch.
//   - Intensity  how much of the repeats is fed back.
//   - Mix        the echo and spring level added to the dry (the dry is never touched, as on the unit).
// The echo takes the sum of the channels; its wet pair is added to the left and right. Bass, Treble, Drive and Hiss are part of the
// engine and wait for the wide panel; here they sit at their neutral settings.
class TapeSpringInsert : public InsertAlgorithmBase
{
public:
    void prepare(double sampleRate, int) override { engine.prepare(sampleRate); started = false; }
    void reset() override { engine.reset(); started = false; }

    void process(juce::AudioBuffer<float>& buf, int ns, int nCh, const VoiceParams& p, float& /*grOut*/) override
    {
        if (nCh <= 0) return;
        // Slot 0 = Mode 1..12, 1 = Rate 0..100 %, 2 = Intensity 0..100 %, 3 = Mix 0..100 %.
        mu_core::TapeSpringEcho::Params e;
        e.mode      = juce::jlimit(1, 12, (int) std::lround(insertSlot(p, 0)));
        e.rate      = juce::jlimit(0.0f, 1.0f, insertSlot(p, 1) * 0.01f);
        e.intensity = juce::jlimit(0.0f, 1.0f, insertSlot(p, 2) * 0.01f);
        const float mix = juce::jlimit(0.0f, 1.0f, insertSlot(p, 3) * 0.01f);
        e.echo = 1.0f; e.reverb = 0.7f;
        engine.setParameters(e);
        if (! started) { engine.jumpToTarget(); started = true; }   // the first block starts at this tape speed, with no glide

        auto chans = buf.getArrayOfWritePointers();
        const float inGain = nCh > 1 ? 0.5f : 1.0f;
        for (int i = 0; i < ns; ++i)
        {
            float mono = chans[0][i];
            if (nCh > 1) mono += chans[1][i];
            float wl, wr;
            engine.process(mono * inGain, wl, wr);
            chans[0][i] += wl * mix;
            if (nCh > 1) chans[1][i] += wr * mix;
        }
    }

private:
    mu_core::TapeSpringEcho engine;
    bool started = false;
};
