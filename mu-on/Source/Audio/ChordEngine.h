#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include "Audio/MultiModeFilter.h"   // mu-core
#include "Audio/ExpDecay.h"          // mu-core: one-multiply exponential decay
#include "Audio/ChordVoicing.h"
#include <cmath>

// ChordEngine — a chord player for dub-techno stabs and pads: up to six notes of one chord on band-limited oscillators
// (saw, square or a narrow pulse), summed into one low-pass MultiModeFilter (mu-core) with a drive on its input and its
// own decay envelope on the cutoff, then one A/D/S/R amp envelope. A step names the chord (ChordVoicing.h); the notes
// are held to the end of the step and then released. A new step replaces the chord that is sounding.
//
// Block-rate filter envelope (the cutoff is set once per block); oscillators and the amp envelope are per-sample.
namespace mu_on
{

class ChordEngine
{
public:
    enum Wave { Saw = 0, Square = 1, Pulse = 2 };

    void prepare(double sr, int maxBlock)
    {
        sampleRate = sr > 0.0 ? sr : 44100.0;
        filter.prepare(sampleRate, maxBlock, 2);
        filter.setType(0);   // LP12
        active = false;
    }

    // wave 0..2; cutHz; res 0..1; drv 0..1 (the filter's input drive); env 0..1 (filter envelope depth); edecMs the
    // filter envelope's decay; atkMs / decMs / relMs the amp times; sus 0..1; root 0..11 and scale: the master key.
    void setParams(int wave_, float cutHz, float res, float drv, float env_, float edecMs,
                   float atkMs, float decMs, float sus_, float relMs, int root, int scale) noexcept
    {
        wave     = juce::jlimit(0, 2, wave_);
        baseCut  = cutHz;
        envDepth = juce::jlimit(0.0f, 1.0f, env_);
        filterEnvInv = 1.0f / juce::jmax(1.0f, edecMs * 0.001f * (float) sampleRate);
        atkInv   = 1.0f / juce::jmax(1.0f, atkMs * 0.001f * (float) sampleRate);
        decayEnv.setTimeConstant(decMs * 0.001f * (float) sampleRate);
        sustain  = juce::jlimit(0.0f, 1.0f, sus_);
        relMul   = std::exp(-1.0f / juce::jmax(1.0f, relMs * 0.001f * (float) sampleRate));
        keyRoot  = root;
        keyScale = scale;
        filter.setResonance(juce::jlimit(0.0f, 1.0f, res));
        filter.setDrive(juce::jlimit(0.0f, 1.0f, drv));
    }

    // A chord at `onset` in the block, held `gateSamples` (0: until the next chord) and then released.
    void trigger(float velocity, int onset, const ChordHit& hit, int gateSamples = 0) noexcept
    {
        restart = true; pendingVel = velocity; pendingOnset = juce::jmax(0, onset);
        pendingGate = juce::jmax(0, gateSamples); pendingHit = hit;
    }

    void reset() noexcept { active = false; restart = false; releasing = false; gateLeft = 0; filter.reset(); }
    void resetFilterSmoothing() noexcept { filter.resetSmoothers(); }

    void render(juce::AudioBuffer<float>& buf, int n)
    {
        const int chs = buf.getNumChannels();

        // Block-rate filter envelope: the cutoff sweeps from base + depth down to base, restarting on a chord.
        const float envT   = restart ? 0.0f : t;
        const float fEnv   = (active || restart) ? std::exp(-envT * filterEnvInv) : 0.0f;
        filter.setCutoff(juce::jlimit(20.0f, (float) sampleRate * 0.45f, baseCut + fEnv * envDepth * 6000.0f));

        for (int i = 0; i < n; ++i)
        {
            // Sample-accurate onset: the chord's notes are worked out here, at the step's own sample.
            if (restart && i >= pendingOnset)
            {
                restart = false;
                const auto notes = voiceChord(keyRoot, keyScale, pendingHit);
                voices = notes.count;
                for (int v = 0; v < voices; ++v)
                {
                    inc[(size_t) v] = mu_audio::midiToFreq((float) notes.midi[(size_t) v]) / (float) sampleRate;
                    phase[(size_t) v] = 0.0f;
                }
                active = true; t = 0.0f; amp = 0.0f; vel = pendingVel; decayEnv.reset(1.0f);
                releasing = false; relGain = 1.0f; gateLeft = pendingGate;
            }

            float s = 0.0f;
            if (active)
            {
                // Amp envelope: linear attack to 1, exponential decay to the sustain floor, then the release after the gate.
                if (t < attackSamples()) amp = t * atkInv;
                else                     amp = sustain + (1.0f - sustain) * decayEnv.next();
                if (gateLeft > 0 && --gateLeft == 0) releasing = true;
                if (releasing) relGain *= relMul;

                float sum = 0.0f;
                for (int v = 0; v < voices; ++v)
                {
                    sum += osc(phase[(size_t) v], inc[(size_t) v]);
                    phase[(size_t) v] += inc[(size_t) v];
                    if (phase[(size_t) v] >= 1.0f) phase[(size_t) v] -= 1.0f;
                }
                s = sum * (0.55f / std::sqrt((float) juce::jmax(1, voices))) * amp * vel * relGain;

                t += 1.0f;
                if (releasing && relGain < 1.0e-4f) active = false;
            }
            for (int c = 0; c < chs; ++c) buf.addSample(c, i, s);
        }

        filter.process(buf, n, chs);   // the drive and the low-pass shape the chord
    }

private:
    float attackSamples() const noexcept { return 1.0f / juce::jmax(1.0e-6f, atkInv); }

    // PolyBLEP: the correction that removes a hard edge's aliasing (Valimaki and Huovilainen 2007).
    static float polyBlep(float x, float dt) noexcept
    {
        if (x < dt)        { x /= dt; return x + x - x * x - 1.0f; }
        if (x > 1.0f - dt) { x = (x - 1.0f) / dt; return x * x + x + x + 1.0f; }
        return 0.0f;
    }

    float osc(float x, float dt) const noexcept
    {
        if (wave == Saw) return 2.0f * x - 1.0f - polyBlep(x, dt);
        const float width = wave == Pulse ? 0.25f : 0.5f;
        float y = x < width ? 1.0f : -1.0f;
        y += polyBlep(x, dt);
        float x2 = x - width; if (x2 < 0.0f) x2 += 1.0f;
        return y - polyBlep(x2, dt);
    }

    MultiModeFilter filter;
    double sampleRate = 44100.0;
    int    wave = Saw, keyRoot = 0, keyScale = mu_audio::kNumScales - 1;
    float  baseCut = 900.0f, envDepth = 0.5f, filterEnvInv = 0.0005f, atkInv = 0.1f, sustain = 0.6f, relMul = 0.999f;
    mu_core::ExpDecay decayEnv;
    std::array<float, 6> phase {}, inc {};
    int    voices = 0;
    float  t = 0.0f, amp = 0.0f, vel = 1.0f, pendingVel = 1.0f, relGain = 1.0f;
    int    pendingOnset = 0, pendingGate = 0, gateLeft = 0;
    ChordHit pendingHit;
    bool   active = false, restart = false, releasing = false;
};

} // namespace mu_on
