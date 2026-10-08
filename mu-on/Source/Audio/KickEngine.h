#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include "Audio/ExpDecay.h"   // mu-core: one-multiply exponential decay
#include <cmath>

// KickEngine — a synthesized 909-style kick: a sine body with an exponential PITCH
// envelope (start tune → base) for the thump, an exponential AMP envelope, and a tanh
// drive for click/punch. Mono; rendered (additively) into every channel of the buffer.
// Sample-accurate onset: trigger() takes the step's sample offset within the block.
// Allocation-free.
namespace mu_on
{

class KickEngine
{
public:
    void prepare(double sr) noexcept { sampleRate = sr > 0.0 ? sr : 44100.0; active = false; }

    // baseFreq Hz, pitch amount Hz (added at the attack), pitch/amp decays in ms, drive 0..1.
    void setParams(float baseHz, float pitchAmtHz, float pitchDecMs, float ampDecMs, float drv) noexcept
    {
        baseFreq = baseHz;
        pitchAmt = pitchAmtHz;
        pitchEnv.setTimeConstant(pitchDecMs * 0.001f * (float) sampleRate);
        ampEnv  .setTimeConstant(ampDecMs   * 0.001f * (float) sampleRate);
        drive    = juce::jlimit(0.0f, 1.0f, drv);
    }

    void trigger(float velocity, int onset = 0) noexcept { restart = true; pendingVel = velocity; pendingOnset = juce::jmax(0, onset); }

    // Silence the voice immediately (e.g. transport stop). Allocation-free.
    void reset() noexcept { active = false; restart = false; }

    void render(juce::AudioBuffer<float>& buf, int n) noexcept
    {
        const int chs = buf.getNumChannels();
        for (int i = 0; i < n; ++i)
        {
            // Sample-accurate onset: a step landing mid-block starts the voice at its offset.
            if (restart && i >= pendingOnset)
            {
                active = true; phase = 0.0f; t = 0.0f; restart = false;
                pitchEnv.reset(1.0f);
                ampEnv.reset(pendingVel);
            }

            float s = 0.0f;
            if (active)
            {
                const float pEnv = pitchEnv.next();
                const float freq = baseFreq + pitchAmt * pEnv;
                const float aEnv = ampEnv.next();   // starts at the velocity
                s = std::sin(phase) * aEnv;
                if (drive > 0.0f) s = std::tanh(s * (1.0f + drive * 4.0f)) / (1.0f + drive * 0.5f);

                phase += twoPi * freq / (float) sampleRate;
                if (phase > twoPi) phase -= twoPi;
                t += 1.0f;
                if (aEnv < 1.0e-4f && t > 16.0f) active = false;
            }
            for (int c = 0; c < chs; ++c) buf.addSample(c, i, s);
        }
    }

private:
    static constexpr float twoPi = 6.28318530718f;

    double sampleRate = 44100.0;
    float  baseFreq = 50.0f, pitchAmt = 220.0f, drive = 0.2f;
    float  phase = 0.0f, t = 0.0f, pendingVel = 1.0f;
    mu_core::ExpDecay pitchEnv, ampEnv;   // pitch sweep (start tune → base) and amp decay
    int    pendingOnset = 0;
    bool   active = false, restart = false;
};

} // namespace mu_on
