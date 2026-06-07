#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <cmath>

// KickEngine — a synthesized 909-style kick: a sine body with an exponential PITCH
// envelope (start tune → base) for the thump, an exponential AMP envelope, and a tanh
// drive for click/punch. Mono; rendered (additively) into every channel of the buffer.
// Block-start onset (sample-accurate onset is a future refinement). Allocation-free.
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
        pitchInv = 1.0f / juce::jmax(1.0f, pitchDecMs * 0.001f * (float) sampleRate);
        ampInv   = 1.0f / juce::jmax(1.0f, ampDecMs   * 0.001f * (float) sampleRate);
        drive    = juce::jlimit(0.0f, 1.0f, drv);
    }

    void trigger(float velocity) noexcept { restart = true; pendingVel = velocity; }

    void render(juce::AudioBuffer<float>& buf, int n) noexcept
    {
        const int chs = buf.getNumChannels();
        for (int i = 0; i < n; ++i)
        {
            if (restart) { active = true; phase = 0.0f; t = 0.0f; vel = pendingVel; restart = false; }

            float s = 0.0f;
            if (active)
            {
                const float pEnv = std::exp(-t * pitchInv);
                const float freq = baseFreq + pitchAmt * pEnv;
                const float aEnv = std::exp(-t * ampInv) * vel;
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
    float  baseFreq = 50.0f, pitchAmt = 220.0f, pitchInv = 0.01f, ampInv = 0.002f, drive = 0.2f;
    float  phase = 0.0f, t = 0.0f, vel = 1.0f, pendingVel = 1.0f;
    bool   active = false, restart = false;
};

} // namespace mu_on
