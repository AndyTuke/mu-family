#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include "Audio/MultiModeFilter.h"   // mu-core
#include <cmath>

// BassEngine — a deep monophonic bass: a main oscillator (sine / saw / square) plus a
// sub oscillator one octave down, an A/D/S amp envelope, and a low-pass MultiModeFilter
// (mu-core) with its own decay envelope on the cutoff + valve drive. That combination
// spans "deep clean" (sine/low drive/low reso) to "rumble" (sub up, drive + resonance).
//
// Pitch is a fixed root (the 909 step grid triggers on/off, not pitch) — per-step pitch
// + glide + a note-off-driven release are future enhancements (so Release/Glide are
// deliberately NOT exposed yet: they'd be inert without a note-off / pitch event).
//
// Block-rate filter-envelope (cutoff updated once per block); osc + amp env are per-sample.
namespace mu_on
{

class BassEngine
{
public:
    enum Wave { Sine = 0, Saw = 1, Square = 2 };

    void prepare(double sr, int maxBlock)
    {
        sampleRate = sr > 0.0 ? sr : 44100.0;
        filter.prepare(sampleRate, maxBlock, 2);
        filter.setType(0);   // LP12
        active = false;
    }

    // rootHz: pitch; wave: 0/1/2; sub 0..1; cut Hz; res 0..1; env 0..1 (filter env depth);
    // edecMs filter-env decay; atkMs/decMs amp attack/decay; sus 0..1; drive 0..1.
    void setParams(float rootHz, int wave_, float sub_, float cutHz, float res,
                   float env_, float edecMs, float atkMs, float decMs, float sus_, float drv)
    {
        rootFreq = juce::jmax(10.0f, rootHz);
        wave     = juce::jlimit(0, 2, wave_);
        sub      = juce::jlimit(0.0f, 1.0f, sub_);
        baseCut  = cutHz;
        envDepth = juce::jlimit(0.0f, 1.0f, env_);
        filterEnvInv = 1.0f / juce::jmax(1.0f, edecMs * 0.001f * (float) sampleRate);
        atkInv   = 1.0f / juce::jmax(1.0f, atkMs * 0.001f * (float) sampleRate);
        decInv   = 1.0f / juce::jmax(1.0f, decMs * 0.001f * (float) sampleRate);
        sustain  = juce::jlimit(0.0f, 1.0f, sus_);
        filter.setResonance(juce::jlimit(0.0f, 1.0f, res));
        filter.setDrive(juce::jlimit(0.0f, 1.0f, drv));
    }

    void trigger(float velocity, int onset = 0) noexcept { restart = true; pendingVel = velocity; pendingOnset = juce::jmax(0, onset); }

    // Silence the voice + clear the filter ring immediately (e.g. transport stop).
    void reset() noexcept { active = false; restart = false; filter.reset(); }

    void render(juce::AudioBuffer<float>& buf, int n)
    {
        const int chs = buf.getNumChannels();

        // Block-rate filter envelope: cutoff sweeps from base+depth down to base. On the
        // block a trigger lands, t resets to 0 mid-loop — use 0 here too so the filter
        // envelope opens on the onset block instead of reading the previous block's t.
        const float envT   = restart ? 0.0f : t;
        const float fEnv   = (active || restart) ? std::exp(-envT * filterEnvInv) : 0.0f;
        const float cutoff = juce::jlimit(20.0f, (float) sampleRate * 0.45f,
                                          baseCut + fEnv * envDepth * 6000.0f);
        filter.setCutoff(cutoff);

        for (int i = 0; i < n; ++i)
        {
            // Sample-accurate onset: a step landing mid-block starts the voice at its offset.
            if (restart && i >= pendingOnset) { active = true; phase = 0.0f; subPhase = 0.0f; t = 0.0f; amp = 0.0f; vel = pendingVel; restart = false; }

            float s = 0.0f;
            if (active)
            {
                // Amp env: linear attack to 1, exponential decay toward the sustain floor.
                if (t < attackSamples()) amp = t * atkInv;
                else                     amp = sustain + (1.0f - sustain) * std::exp(-(t - attackSamples()) * decInv);
                amp *= vel;

                const float main = osc(phase, wave);
                const float subS = std::sin(subPhase);
                s = (main + sub * subS) * amp;

                phase    += twoPi * rootFreq / (float) sampleRate;        if (phase    > twoPi) phase    -= twoPi;
                subPhase += twoPi * (rootFreq * 0.5f) / (float) sampleRate; if (subPhase > twoPi) subPhase -= twoPi;
                t += 1.0f;
                if (sustain < 1.0e-4f && amp < 1.0e-4f && t > attackSamples() + 16.0f) active = false;
            }
            for (int c = 0; c < chs; ++c) buf.addSample(c, i, s);
        }

        filter.process(buf, n, chs);   // LP + valve drive shapes the body / rumble
    }

private:
    static constexpr float twoPi = 6.28318530718f;

    float attackSamples() const noexcept { return 1.0f / juce::jmax(1.0e-6f, atkInv); }

    static float osc(float ph, int wave) noexcept
    {
        const float x = ph / twoPi;                 // 0..1
        switch (wave)
        {
            case Saw:    return 2.0f * x - 1.0f;
            case Square: return x < 0.5f ? 1.0f : -1.0f;
            default:     return std::sin(ph);       // Sine
        }
    }

    MultiModeFilter filter;
    double sampleRate = 44100.0;
    int    wave = Sine;
    float  rootFreq = 41.2f, sub = 0.5f, baseCut = 600.0f, envDepth = 0.4f;
    float  filterEnvInv = 0.004f, atkInv = 0.2f, decInv = 0.004f, sustain = 0.0f;
    float  phase = 0.0f, subPhase = 0.0f, t = 0.0f, amp = 0.0f, vel = 1.0f, pendingVel = 1.0f;
    int    pendingOnset = 0;
    bool   active = false, restart = false;
};

} // namespace mu_on
