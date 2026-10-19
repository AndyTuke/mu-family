#pragma once

#include "FilterFamilies.h"

// type 6 — 24 dB/oct bandpass via JUCE's LadderFilter (Moog-style).
// The raw ladder band-pass has a centre gain that climbs with resonance (about -9 dB at res 0, 0 dB near
// res 0.45, +9 dB at res 1). This wrapper multiplies by the inverse of that measured gain so the centre
// stays near 0 dB at every resonance, matching Bp12; the largest boost is about +9 dB, at res 0.
class Bp24Filter : public LadderFilter24<juce::dsp::LadderFilterMode::BPF24>
{
public:
    void reset() override { LadderFilter24::reset(); gainInitialised = false; }

    void process(juce::AudioBuffer<float>& buf, int numSamples, int numChannels,
                 float cutoffHz, float resonance) override
    {
        LadderFilter24::process(buf, numSamples, numChannels, cutoffHz, resonance);

        // Ramp the compensation across the block so a resonance change does not step the level.
        const float target = centreCompensation(resonance);
        const float start  = gainInitialised ? gain : target;
        const float step   = numSamples > 0 ? (target - start) / static_cast<float>(numSamples) : 0.0f;
        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto* data = buf.getWritePointer(ch);
            float g = start;
            for (int i = 0; i < numSamples; ++i) { g += step; data[i] *= g; }
        }
        gain = target;
        gainInitialised = true;
    }

private:
    // Linear gain that undoes the ladder band-pass centre gain. The table is the raw centre gain in dB
    // (mean of 200 Hz, 1 kHz and 4 kHz at 48 kHz) at resonance 0, 0.05 ... 1; the cutoff spread around
    // it is under +-1.7 dB.
    static float centreCompensation(float resonance) noexcept
    {
        static constexpr float rawDb[21] = {
            -9.34f, -8.49f, -7.45f, -6.43f, -5.42f, -4.42f, -3.41f, -2.41f, -1.40f, -0.40f, 0.60f,
             1.59f,  2.55f,  3.49f,  4.39f,  5.23f,  6.04f,  6.78f,  7.47f,  8.11f,  8.71f };
        if (! (resonance == resonance)) resonance = 0.0f;   // NaN guard: the table index must stay in range
        const float pos = juce::jlimit(0.01f, 1.0f, resonance) * 20.0f;   // same floor the ladder is given
        const int   i   = juce::jmin(19, static_cast<int>(pos));
        const float db  = rawDb[i] + (rawDb[i + 1] - rawDb[i]) * (pos - static_cast<float>(i));
        return std::pow(10.0f, -db / 20.0f);
    }

    float gain = 1.0f;
    bool  gainInitialised = false;
};
