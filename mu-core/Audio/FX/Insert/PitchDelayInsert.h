#pragma once

#include "InsertAlgorithmBase.h"
#include "InsertDelayLine.h"
#include <cmath>
#include <vector>

// insertAlgo = 20. Pitch-shifting delay: a delay line whose output passes through a pitch shifter before it is heard and
// before it is fed back, so every repeat is shifted again (+7 gives a rising fifth cascade, -12 falling octaves, and so on).
//   - Pitch     -12..+12 semitones.
//   - Time      20 ms..1 s between repeats. With the insert's Sync on it is 1..16 sixteenth notes of the tempo instead
//               (the knob's range in sixteen equal steps). It is glided, not jumped.
//   - Feedback  0..95 %; the fed-back signal passes through tanh so the loop cannot run away.
//   - Mix       dry/wet crossfade.
// The shifter is the classic two-head delay-line pitch shifter: two read heads half a window apart sweep through a short
// buffer at a speed that changes the playback rate, each faded in and out with a Hann window so the jump back is inaudible.
class PitchDelayInsert : public InsertAlgorithmBase
{
public:
    void prepare(double sampleRate, int) override
    {
        sr = sampleRate > 0.0 ? sampleRate : 44100.0;
        for (auto& l : line) l.prepare(sr, 4.25);   // 4 s (16 sixteenths at 60 bpm) plus the interpolator's taps
        winSamples = juce::jmax(64, (int) (0.06 * sr));
        for (auto& s : shifter) s.prepare(winSamples);
        fb.reset(sr, 0.02);
        mix.reset(sr, 0.02);
        reset();
    }

    void reset() override
    {
        for (auto& l : line) l.clear();
        for (auto& s : shifter) s.clear();
        started = false;
        fb.setCurrentAndTargetValue(0.0f);
        mix.setCurrentAndTargetValue(0.0f);
    }

    void process(juce::AudioBuffer<float>& buf, int ns, int nCh,
                 const VoiceParams& p, float& /*grOut*/) override
    {
        if (! line[0].ready()) return;
        const int nChState = juce::jmin(nCh, 2);
        // Slot 0 = Pitch -12..12 st, 1 = Time 20..1000 ms (log), 2 = Feedback 0..95 %, 3 = Mix 0..100 %.
        const float semis  = juce::jlimit(-12.0f, 12.0f, insertSlot(p, 0));
        const float timeMs = juce::jlimit(20.0f, 1000.0f, insertSlot(p, 1));
        const float fbAmt  = juce::jlimit(0.0f, 0.95f, insertSlot(p, 2) * 0.01f);
        const float mixAmt = juce::jlimit(0.0f, 1.0f, insertSlot(p, 3) * 0.01f);
        const float ratio  = std::exp2(semis / 12.0f);

        float target = timeMs * 0.001f * (float) sr;
        if (p.insertSync)
        {
            const int n = mu_ui::insertSyncStepFromNorm(p.insertParam[1]);
            target = juce::jmin((float) (n * mu_ui::insertSixteenthSeconds(tempoBpm) * sr), (float) (line[0].capacity() - 8));
        }
        if (! started)
        {
            timeSmoothed = target; started = true;
            fb.setCurrentAndTargetValue(fbAmt); mix.setCurrentAndTargetValue(mixAmt);
        }
        fb.setTargetValue(fbAmt);
        mix.setTargetValue(mixAmt);
        const float glide = 1.0f - std::exp(-1.0f / (0.08f * (float) sr));

        auto chans = buf.getArrayOfWritePointers();
        for (int i = 0; i < ns; ++i)
        {
            timeSmoothed += (target - timeSmoothed) * glide;
            const float f = fb.getNextValue();
            const float m = mix.getNextValue();
            for (int ch = 0; ch < nChState; ++ch)
            {
                const float in = chans[ch][i];
                const float shifted = shifter[ch].process(line[ch].read(timeSmoothed), ratio);
                line[ch].push(std::tanh(in + shifted * f));
                chans[ch][i] = in * (1.0f - m) + shifted * m;
            }
        }
    }

private:
    // Two-head pitch shifter on a short circular buffer.
    struct Shifter
    {
        void prepare(int window)
        {
            win = window;
            buf.assign((size_t) (win * 2 + 8), 0.0f);
            clear();
        }
        void clear() { std::fill(buf.begin(), buf.end(), 0.0f); write = 0; phase = 0.0f; }

        float process(float x, float ratio) noexcept
        {
            const int size = (int) buf.size();
            buf[(size_t) write] = x;
            // Each head's delay sweeps 0..win; its speed (1 - ratio) samples per sample is what shifts the pitch.
            const float p1 = phase, p2 = phase + 0.5f >= 1.0f ? phase - 0.5f : phase + 0.5f;
            const float y = tap(p1 * (float) win, size) * hann(p1) + tap(p2 * (float) win, size) * hann(p2);
            phase += (1.0f - ratio) / (float) win;
            if (phase >= 1.0f) phase -= 1.0f;
            if (phase < 0.0f)  phase += 1.0f;
            if (++write >= size) write = 0;
            return y;
        }

    private:
        static float hann(float ph) noexcept { const float s = std::sin(juce::MathConstants<float>::pi * ph); return s * s; }
        float tap(float delay, int size) const noexcept   // linear interpolation
        {
            float rp = (float) write - delay;
            while (rp < 0.0f) rp += (float) size;
            // A tiny negative position plus the size rounds to exactly the size in float: wrap again, and never leave the buffer
            // (a NaN position, from a NaN pitch ratio, also falls back to the start).
            while (rp >= (float) size) rp -= (float) size;
            if (! (rp >= 0.0f && rp < (float) size)) rp = 0.0f;
            const int i0 = juce::jmin((int) rp, size - 1);
            const float fr = rp - (float) i0;
            const int i1 = i0 + 1 >= size ? 0 : i0 + 1;
            return buf[(size_t) i0] * (1.0f - fr) + buf[(size_t) i1] * fr;
        }
        std::vector<float> buf;
        int   win = 64, write = 0;
        float phase = 0.0f;
    };

    double sr = 44100.0;
    int    winSamples = 2048;
    InsertDelayLine line[2];
    Shifter shifter[2];
    float  timeSmoothed = 0.0f;
    bool   started = false;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> fb, mix;
};
