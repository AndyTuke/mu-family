#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include "Audio/SamplePlayer.h"   // mu-core one-shot voice
#include <cmath>

// SampleChannel — a sample-based drum lane (Hat / Snare). Built on the shared mu-core
// SamplePlayer: it owns the sample buffer and plays it one-shot per trigger with a tune
// (playback-ratio) control and a decay gate (shortens the hit). The default buffer is
// generated procedurally (bright noise burst for the hat; noise + body tone for the
// snare) so the product makes sound with no shipped assets — replacing it with a loaded
// .wav ("Load .wav…") is the intended next step (the buffer is the only seam to change).
namespace mu_on
{

class SampleChannel
{
public:
    enum Kind { HiHat = 0, Snare = 1 };

    void prepare(double sr, int maxBlock, Kind k)
    {
        sampleRate = sr > 0.0 ? sr : 44100.0;
        generate(k);
        scratch.setSize(1, juce::jmax(1, maxBlock));
        ratios.assign((size_t) juce::jmax(1, maxBlock), 1.0);
        active = false;
    }

    // tuneSemitones: ± playback transpose; decayMs: gate length.
    void setParams(float tuneSemitones, float decayMs) noexcept
    {
        ratio    = std::pow(2.0, (double) tuneSemitones / 12.0);
        decayInv = 1.0f / juce::jmax(1.0f, decayMs * 0.001f * (float) sampleRate);
    }

    void trigger(float velocity) noexcept { player.trigger(); restart = true; pendingVel = velocity; }

    void render(juce::AudioBuffer<float>& buf, int n)
    {
        if (restart) { t = 0.0f; vel = pendingVel; active = true; restart = false; }

        // Pull the one-shot into the mono scratch (SamplePlayer ADDS, so clear first).
        scratch.clear();
        for (int i = 0; i < n; ++i) ratios[(size_t) i] = ratio;
        player.process(sampleBuf, ratios.data(), scratch, n);

        const int chs = buf.getNumChannels();
        const float* src = scratch.getReadPointer(0);
        for (int i = 0; i < n; ++i)
        {
            float g = 0.0f;
            if (active)
            {
                g = std::exp(-t * decayInv) * vel;
                t += 1.0f;
                if (g < 1.0e-4f && ! player.isActive()) active = false;
            }
            const float s = src[i] * g;
            for (int c = 0; c < chs; ++c) buf.addSample(c, i, s);
        }
    }

private:
    void generate(Kind k)
    {
        const int len = (int) (0.3 * sampleRate);   // 300 ms one-shot
        sampleBuf.setSize(1, len);
        auto* d = sampleBuf.getWritePointer(0);
        juce::Random rng(k == HiHat ? 0x4a7 : 0x5b2);

        if (k == HiHat)
        {
            // Bright noise: high-passed (first difference) white noise with a fast decay.
            float prev = 0.0f;
            const float tau = 1.0f / (0.04f * (float) sampleRate);   // ~40 ms
            for (int i = 0; i < len; ++i)
            {
                const float wn = rng.nextFloat() * 2.0f - 1.0f;
                const float hp = wn - prev; prev = wn;
                d[i] = hp * std::exp(-(float) i * tau) * 0.8f;
            }
        }
        else
        {
            // Snare: noise body + a 180 Hz tone, medium decay.
            const float tauN = 1.0f / (0.12f * (float) sampleRate);
            const float tauT = 1.0f / (0.10f * (float) sampleRate);
            for (int i = 0; i < len; ++i)
            {
                const float wn   = rng.nextFloat() * 2.0f - 1.0f;
                const float tone = std::sin(6.28318530718f * 180.0f * (float) i / (float) sampleRate);
                d[i] = (wn * std::exp(-(float) i * tauN) * 0.7f
                        + tone * std::exp(-(float) i * tauT) * 0.5f);
            }
        }
    }

    SamplePlayer            player;
    juce::AudioBuffer<float> sampleBuf, scratch;
    std::vector<double>      ratios;
    double sampleRate = 44100.0, ratio = 1.0;
    float  decayInv = 0.01f, t = 0.0f, vel = 1.0f, pendingVel = 1.0f;
    bool   active = false, restart = false;
};

} // namespace mu_on
