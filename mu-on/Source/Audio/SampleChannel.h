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
    enum class Kind { HiHat, Snare };

    void prepare(double sr, int maxBlock, Kind k)
    {
        sampleRate = sr > 0.0 ? sr : 44100.0;
        kind = k;
        generate(k);
        scratch.setSize(1, juce::jmax(1, maxBlock));
        ratios.assign((size_t) juce::jmax(1, maxBlock), 1.0);
        filledRatio = -1.0;   // force the first render to fill ratios to the live tune
        active = false;
    }

    // tuneSemitones: ± playback transpose; decayMs: gate length. The Hat has a second pair for its open sound
    // (a Snare has one sound, so it never reads the second pair).
    void setParams(float tuneSemitones, float decayMs, float openTuneSemitones = 0.0f, float openDecayMs = 0.0f) noexcept
    {
        closedRatio = std::pow(2.0, (double) tuneSemitones / 12.0);
        closedDecay = decayMs;
        openRatio   = std::pow(2.0, (double) openTuneSemitones / 12.0);
        openDecay   = openDecayMs;
        applySound();
    }

    // `open`: play the open sound (Hat only). A new hit restarts the one voice, so a closed hat chokes a ringing open one.
    void trigger(float velocity, int onset = 0, bool open = false) noexcept
    {
        playingOpen = open && kind == Kind::HiHat;
        applySound();
        player.trigger(); restart = true; pendingVel = velocity; pendingOnset = juce::jmax(0, onset);
    }

    // Silence the lane immediately (e.g. transport stop). The next render adds nothing.
    void reset() noexcept { active = false; restart = false; }

    void render(juce::AudioBuffer<float>& buf, int n)
    {
        // A step landing mid-block starts the sample + gate at its sample offset; 0 on
        // continuation blocks (the player just keeps streaming from sample 0 of the block).
        int onset = 0;
        if (restart) { gateEnv.reset(pendingVel); active = true; restart = false; onset = juce::jmin(pendingOnset, n - 1); }

        // Pull the one-shot into the mono scratch (SamplePlayer ADDS, so clear first).
        scratch.clear();
        // Refill the per-sample ratio span only when the tune (ratio) changed — it's
        // block-constant, so the old per-sample rewrite was pure busywork.
        if (ratio != filledRatio) { std::fill(ratios.begin(), ratios.end(), ratio); filledRatio = ratio; }
        const int playN = n - onset;
        if (playN > 0)
        {
            juce::AudioBuffer<float> sub(scratch.getArrayOfWritePointers(), 1, onset, playN);
            player.process(playingOpen ? openBuf : sampleBuf, ratios.data(), sub, playN);
        }

        const int chs = buf.getNumChannels();
        const float* src = scratch.getReadPointer(0);
        for (int i = onset; i < n; ++i)
        {
            float g = 0.0f;
            if (active)
            {
                g = gateEnv.next();   // starts at the velocity
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
        juce::Random rng(k == Kind::HiHat ? 0x4a7 : 0x5b2);

        if (k == Kind::HiHat)
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

            // The open hat: the same bright noise with a long ring (~250 ms time constant, 1.2 s buffer).
            const int openLen = (int) (1.2 * sampleRate);
            openBuf.setSize(1, openLen);
            auto* o = openBuf.getWritePointer(0);
            juce::Random openRng(0x6c9);
            const float openTau = 1.0f / (0.25f * (float) sampleRate);
            prev = 0.0f;
            for (int i = 0; i < openLen; ++i)
            {
                const float wn = openRng.nextFloat() * 2.0f - 1.0f;
                const float hp = wn - prev; prev = wn;
                o[i] = hp * std::exp(-(float) i * openTau) * 0.8f;
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

    // The sound the next hit uses: the closed or open pair's tune and gate.
    void applySound() noexcept
    {
        ratio = playingOpen ? openRatio : closedRatio;
        gateEnv.setTimeConstant((playingOpen ? openDecay : closedDecay) * 0.001f * (float) sampleRate);
    }

    SamplePlayer            player;
    juce::AudioBuffer<float> sampleBuf, openBuf, scratch;
    std::vector<double>      ratios;
    Kind   kind = Kind::HiHat;
    bool   playingOpen = false;
    double closedRatio = 1.0, openRatio = 1.0;
    float  closedDecay = 60.0f, openDecay = 300.0f;
    double sampleRate = 44100.0, ratio = 1.0, filledRatio = -1.0;
    float  pendingVel = 1.0f;
    mu_core::ExpDecay gateEnv;   // the gate's exponential decay
    int    pendingOnset = 0;
    bool   active = false, restart = false;
};

} // namespace mu_on
