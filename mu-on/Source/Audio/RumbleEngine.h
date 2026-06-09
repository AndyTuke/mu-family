#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include "Audio/MultiModeFilter.h"          // mu-core: drive valve + final filter
#include "Audio/FX/Slots/ReverbSlot.h"      // mu-core: Signalsmith dark reverb
#include <cmath>

// RumbleEngine — a classic techno "rumble": takes the Kick's audio as input and runs it,
// in order, through:
//   1. input drive   — the same valve saturation MultiModeFilter applies on its input
//   2. delay taps    — 1/16, 2/16, 3/16 of the beat-grid (tempo-synced), each with a level
//   3. dark reverb   — Signalsmith reverb (size param) into a low-pass (darkness)
//   4. bar envelope  — a per-bar volume shape from a drawable smooth curve (the shared
//                      LFOEditor), evaluated per block by GrooveVoices and pushed via
//                      setBarEnvLevel(); loops once per bar
//   5. filter        — a final multimode filter (cutoff + resonance)
// Stereo in/out; the kick feed is supplied by GrooveVoices (it stashes the kick render).
//
// The whole signal chain + params are wired and audible; the bar-volume envelope is live
// (drawable curve, persisted in state, evaluated under a try-lock). The reverb tone + the
// block-rate envelope are still basic — fine for the current framework. Allocation-free in render().
namespace mu_on
{

class RumbleEngine
{
public:
    void prepare(double sr, int maxBlock)
    {
        sampleRate   = sr > 0.0 ? sr : 44100.0;
        maxBlockSize = juce::jmax(1, maxBlock);

        // Delay line long enough for 3/16 at the slowest tempo (20 BPM) with headroom.
        const int maxDelay = (int) std::ceil(sampleRate * (60.0 / 20.0) * 1.0);   // ~1 beat @ 20 BPM
        delayBuf.setSize(2, juce::jmax(1, maxDelay));
        delayBuf.clear();
        writePos = 0;

        filter.prepare(sampleRate, maxBlockSize, 2);
        filter.setType(0);   // LP12 final filter
        revLp.prepare(sampleRate, maxBlockSize, 2);
        revLp.setType(0);    // LP12 darkening the reverb

        reverb.prepare(sampleRate, maxBlockSize);
        reverb.setEnabled(true);
        reverb.setLevel(1.0f);
        reverb.setParam("damp", 0.7f);   // dark by default

        scratch.setSize(2, maxBlockSize);
        revWet.setSize(2, maxBlockSize);
    }

    // Per-block params. `bpm` drives the tempo-synced delay taps; `revMix` is the reverb
    // dry/wet blend (0 = dry, 1 = fully wet).
    void setParams(double bpm_, float drive, float d1, float d2, float d3,
                   float revSize, float revMix_, float revLpHz, float cutHz, float res) noexcept
    {
        bpm    = bpm_ > 0.0 ? bpm_ : 120.0;
        drv    = juce::jlimit(0.0f, 1.0f, drive);
        lvl1   = juce::jlimit(0.0f, 1.0f, d1);
        lvl2   = juce::jlimit(0.0f, 1.0f, d2);
        lvl3   = juce::jlimit(0.0f, 1.0f, d3);
        revMix = juce::jlimit(0.0f, 1.0f, revMix_);
        reverb.setParam("size", juce::jlimit(0.0f, 1.0f, revSize));
        revLp.setCutoff(juce::jlimit(200.0f, 18000.0f, revLpHz));
        filter.setCutoff(juce::jlimit(20.0f, 20000.0f, cutHz));
        filter.setResonance(juce::jlimit(0.0f, 1.0f, res));
    }

    // Per-block bar-volume level (0..1), evaluated from the drawable bar envelope by the
    // caller (GrooveVoices) at the current bar phase.
    void setBarEnvLevel(float v) noexcept { envLevel = juce::jlimit(0.0f, 1.0f, v); }

    // Clear the delay line + filter state (e.g. on transport stop). The reverb tail decays.
    void reset()
    {
        delayBuf.clear();
        writePos = 0;
        filter.reset();
        revLp.reset();
    }

    // Add the rumble produced from the kick feed `in` into `out` (both stereo, n samples).
    void render(const juce::AudioBuffer<float>& in, juce::AudioBuffer<float>& out, int n)
    {
        const int ch = juce::jmin(2, juce::jmin(out.getNumChannels(), in.getNumChannels()));
        if (ch <= 0 || n <= 0) return;
        const int ns = juce::jmin(n, maxBlockSize);

        // 1. Input drive (MultiModeFilter valve: 1+drv²·5 pre-gain, asymmetric soft-clip, √makeup).
        for (int c = 0; c < ch; ++c)
        {
            const float* src = in.getReadPointer(c);
            float* s = scratch.getWritePointer(c);
            for (int i = 0; i < ns; ++i)
            {
                float x = src[i];
                if (drv > 0.0001f)
                {
                    const float pre = 1.0f + drv * drv * 5.0f;
                    const float inv = 1.0f / std::sqrt(pre);
                    const float y   = x * pre;
                    x = (y >= 0.0f ? std::tanh(y) : y / (1.0f - y)) * inv;
                }
                s[i] = x;
            }
        }

        // 2. Delay taps at 1/16, 2/16, 3/16 of a beat (tempo-synced), each with a level.
        const double spb = sampleRate * 60.0 / bpm;   // samples per beat
        const int cap = delayBuf.getNumSamples();
        const int t1 = juce::jlimit(1, cap - 1, (int) std::lround(spb * 0.25));
        const int t2 = juce::jlimit(1, cap - 1, (int) std::lround(spb * 0.50));
        const int t3 = juce::jlimit(1, cap - 1, (int) std::lround(spb * 0.75));

        int wp = writePos;
        for (int i = 0; i < ns; ++i)
        {
            for (int c = 0; c < ch; ++c)
            {
                float* dl = delayBuf.getWritePointer(c);
                float* s  = scratch.getWritePointer(c);
                dl[wp] = s[i];
                auto tap = [&](int d) { int r = wp - d; if (r < 0) r += cap; return dl[r]; };
                s[i] += tap(t1) * lvl1 + tap(t2) * lvl2 + tap(t3) * lvl3;
            }
            if (++wp >= cap) wp = 0;
        }
        writePos = wp;

        // n-sample views so the heavy stages process exactly this block.
        juce::AudioBuffer<float> work(scratch.getArrayOfWritePointers(), ch, 0, ns);

        // 3. Dark reverb with a dry/wet mix: reverb the dry into a wet buffer, darken it with
        //    the low-pass, then blend dry↔wet by revMix.
        if (revMix > 0.0001f)
        {
            for (int c = 0; c < ch; ++c)
                revWet.copyFrom(c, 0, work, c, 0, ns);
            juce::AudioBuffer<float> wet(revWet.getArrayOfWritePointers(), ch, 0, ns);
            reverb.processReturn(wet);     // wet-only
            revLp.process(wet, ns, ch);    // darken the wet tail
            for (int c = 0; c < ch; ++c)
            {
                float* d = work.getWritePointer(c);
                const float* w = wet.getReadPointer(c);
                for (int i = 0; i < ns; ++i)
                    d[i] = d[i] * (1.0f - revMix) + w[i] * revMix;
            }
        }

        // 4. Bar-volume envelope — the level evaluated from the drawable bar shape this block.
        if (envLevel != 1.0f) work.applyGain(envLevel);

        // 5. Final filter → add the rumble to the channel output.
        filter.process(work, ns, ch);
        for (int c = 0; c < ch; ++c)
            out.addFrom(c, 0, work, c, 0, ns);
    }

private:
    MultiModeFilter filter, revLp;
    ReverbSlot      reverb;
    juce::AudioBuffer<float> delayBuf, scratch, revWet;

    double sampleRate = 44100.0, bpm = 120.0;
    int    maxBlockSize = 512, writePos = 0;
    float  drv = 0.0f, lvl1 = 0.0f, lvl2 = 0.0f, lvl3 = 0.0f, revMix = 0.5f, envLevel = 1.0f;
};

} // namespace mu_on
