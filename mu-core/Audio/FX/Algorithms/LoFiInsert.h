#pragma once

#include "Audio/FX/Insert/InsertAlgorithmBase.h"
#include "Audio/FX/Algorithms/FxUse.h"
#include "Audio/FX/Algorithms/InsertSupport.h"
#include "Audio/AudioFilters.h"
#include <cmath>
#include <vector>

namespace mu_audio
{
// insertAlgo = 29. Lo-Fi: the wear of an old record or tape. There is no Mix knob: each of the four knobs is an amount that is
// zero at 0 (and Tone is open at 20 kHz), so the effect is dry/wet by construction.
//   - Wobble   0..100 %: wow and flutter together, a pitch drift from a short modulated delay. 0 is untouched with no delay.
//   - Noise    0..100 %: steady hiss, up to about -34 dBFS RMS (quadratic: 0.04 * (n / 100)^2 peak, high-passed at 1 kHz).
//   - Crackle  0..100 %: random vinyl pops, up to about 40 a second (quadratic on the rate), random size and sign.
//   - Tone     500..20000 Hz: high-frequency loss, two one-pole low-passes (12 dB/oct); 20 kHz bypasses.
// Order: wobble delay, then Tone, then the added hiss and crackle, so Tone dulls the music and not the added noise, as on a
// worn record. The hiss and crackle are added, so they do not depend on the signal level. Wow is a 0.5 Hz sine plus a slow
// random wander (a new target about every 3 s, raised-cosine glide), flutter a 7 Hz sine plus a faster smooth wander, about +-0.4 % and +-0.6 % pitch at 100 %. Noise comes from one
// xorshift32 per insert (seeded from the slot, restarted by reset()), so it does not depend on the block size. At most 2 ms of
// delay at the start of a note, exactly 0 at Wobble 0.
class LoFiInsert : public InsertAlgorithmBase
{
public:
    static constexpr FxUse kUse = FxUse::Insert;

    void prepare(double sampleRate, int) override
    {
        sr = sampleRate > 0.0 ? sampleRate : 44100.0;
        ringSize = (int) std::ceil(0.004 * sr) + 8;
        for (auto& r : ring) r.assign((size_t) ringSize, 0.0f);
        wobbleSm.reset(sr, 0.015);
        hissCoeff = std::exp(-juce::MathConstants<float>::twoPi * 1000.0f / (float) sr);
        popDecay = std::exp(-1.0f / (0.0015f * (float) sr));
        reset();
    }

    void setSlotIndex(int slotIndex) override
    {
        slotSeed = slotIndex;
        rng.seed(slotSeed);
    }

    void reset() override
    {
        for (auto& r : ring) std::fill(r.begin(), r.end(), 0.0f);
        for (int ch = 0; ch < 2; ++ch)
        {
            tone1[ch].reset(); tone2[ch].reset();
            hissLp[ch] = 0.0f;
            pop[ch] = 0.0f;
        }
        writePos = 0;
        wowPhase = flutterPhase = 0.0f;
        drift = {};
        jitter = {};
        primed = false;
        lastToneHz = -1.0f;
        rng.seed(slotSeed);
    }

    void process(juce::AudioBuffer<float>& buf, int ns, int nCh,
                 const VoiceParams& p, float& /*grOut*/) override
    {
        using namespace insert_support;
        const int nChState = juce::jmin(nCh, 2);
        if (nChState <= 0 || ns <= 0 || ring[0].empty()) return;
        // Slot 0 = Wobble 0..100 %, 1 = Noise 0..100 %, 2 = Crackle 0..100 %, 3 = Tone 500..20000 Hz.
        const float wobble  = juce::jlimit(0.0f, 1.0f, finiteOr(insertSlot(p, 0), 25.0f) * 0.01f);
        const float noise   = juce::jlimit(0.0f, 1.0f, finiteOr(insertSlot(p, 1), 0.0f) * 0.01f);
        const float crackle = juce::jlimit(0.0f, 1.0f, finiteOr(insertSlot(p, 2), 0.0f) * 0.01f);
        const float toneHz  = juce::jlimit(500.0f, 20000.0f, finiteOr(insertSlot(p, 3), 6000.0f));
        if (! primed) { wobbleSm.setCurrentAndTargetValue(wobble); primed = true; }
        wobbleSm.setTargetValue(wobble);

        const bool toneOpen = toneHz >= 19900.0f;
        if (! toneOpen && toneHz != lastToneHz)
        {
            for (int ch = 0; ch < 2; ++ch) { tone1[ch].prepare(toneHz, (float) sr); tone2[ch].prepare(toneHz, (float) sr); }
            lastToneHz = toneHz;
        }
        const float hissAmp = 0.04f * noise * noise;
        const float popProb = 40.0f * crackle * crackle / (float) sr;   // pops per sample

        auto chans = buf.getArrayOfWritePointers();
        // Per sample: record, read the wobbled delay, apply Tone, and add the hiss and the crackle pops.
        for (int i = 0; i < ns; ++i)
        {
            const float w = wobbleSm.getNextValue();
            // The modulators run every sample (even at Wobble 0), so the noise sequence never depends on the knob.
            wowPhase += 0.5f / (float) sr;      if (wowPhase >= 1.0f) wowPhase -= 1.0f;
            flutterPhase += 7.0f / (float) sr;  if (flutterPhase >= 1.0f) flutterPhase -= 1.0f;
            const float wow = 0.8f * std::sin(juce::MathConstants<float>::twoPi * wowPhase) + 0.2f * drift.next(0.3f / (float) sr, rng);
            const float flutter = std::sin(juce::MathConstants<float>::twoPi * flutterPhase) + 0.2f * jitter.next(25.0f / (float) sr, rng);
            const float delayMs = w * (2.0f + 1.2f * juce::jlimit(-1.3f, 1.3f, wow) + 0.15f * juce::jlimit(-1.5f, 1.5f, flutter));
            const float delaySamples = juce::jlimit(1.0f, (float) (ringSize - 4), delayMs * 0.001f * (float) sr);
            const bool delayed = w > 0.0f;

            // The pop trigger and its stereo decision are drawn every sample so the sequence is fixed by the sample count.
            const float uPop = rng.uniform();
            const float uSize = rng.uniform();
            const float uSign = rng.uniform();
            const float uSide = rng.uniform();
            const float white[2] = { rng.bipolar(), rng.bipolar() };   // the hiss of each channel
            if (uPop < popProb)
            {
                const float size = 0.05f + 0.45f * uSize * uSize * uSize;
                const float amp = uSign < 0.5f ? -size : size;
                if (uSide < 0.7f)       { pop[0] += amp; pop[1] += amp; }
                else if (uSide < 0.85f) pop[0] += amp;
                else                    pop[1] += amp;
            }

            for (int ch = 0; ch < nChState; ++ch)
            {
                const float x = sane(chans[ch][i]);
                ring[ch][(size_t) writePos] = x;
                float y = x;
                if (delayed)
                {
                    // Linear interpolation between the two samples around the (fractional) delay.
                    const int   di = (int) delaySamples;
                    const float fr = delaySamples - (float) di;
                    const float a = ring[ch][(size_t) wrap(writePos - di)];
                    const float b = ring[ch][(size_t) wrap(writePos - di - 1)];
                    y = a + (b - a) * fr;
                }
                if (toneOpen) { tone1[ch].setState(y); tone2[ch].setState(y); }
                else          y = tone2[ch].process(tone1[ch].process(y));
                // Hiss: white noise high-passed at 1 kHz (the input minus a one-pole low-pass), independent per channel.
                hissLp[ch] = hissCoeff * hissLp[ch] + (1.0f - hissCoeff) * white[ch];
                y += hissAmp * (white[ch] - hissLp[ch]);
                // Crackle: the current pop, decaying over about 1.5 ms.
                y += pop[ch];
                pop[ch] = flush(pop[ch] * popDecay);
                chans[ch][i] = y;
            }
            writePos = writePos + 1 >= ringSize ? 0 : writePos + 1;
        }
    }

private:
    // Slow random wander in -1..1 that is smooth (no steps in its rate of change): a new random target is drawn at `rate`
    // times a second and the value glides to it along a raised cosine. A white-noise-driven filter would give a rough pitch.
    struct SmoothNoise
    {
        float next(float inc, insert_support::Xorshift32& rng) noexcept
        {
            phase += inc;
            if (phase >= 1.0f)
            {
                phase -= 1.0f;
                from = to;
                to = rng.bipolar();
            }
            return from + (to - from) * (0.5f - 0.5f * std::cos(juce::MathConstants<float>::pi * phase));
        }
        float from = 0.0f, to = 0.0f, phase = 0.0f;
    };

    int wrap(int idx) const noexcept
    {
        while (idx < 0) idx += ringSize;
        return idx;
    }

    double sr = 44100.0;
    int    ringSize = 0;
    std::vector<float> ring[2];
    int    writePos = 0;
    float  wowPhase = 0.0f, flutterPhase = 0.0f;
    SmoothNoise drift, jitter;
    float  hissCoeff = 0.0f, popDecay = 0.0f;
    float  hissLp[2] = {};
    float  pop[2] = {};
    OnePoleLP tone1[2], tone2[2];
    float  lastToneHz = -1.0f;
    int    slotSeed = 0;
    bool   primed = false;
    insert_support::Xorshift32 rng;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> wobbleSm;
};
}  // namespace mu_audio
