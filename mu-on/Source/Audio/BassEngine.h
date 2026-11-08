#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include "Audio/MultiModeFilter.h"   // mu-core
#include "Audio/ExpDecay.h"          // mu-core: one-multiply exponential decay
#include <cmath>

// BassEngine — a deep monophonic bass: a main oscillator (sine / saw / square) plus a
// sub oscillator one octave down, an A/D/S amp envelope, and a low-pass MultiModeFilter
// (mu-core) with its own decay envelope on the cutoff + valve drive. That combination
// spans "deep clean" (sine/low drive/low reso) to "rumble" (sub up, drive + resonance).
//
// Pitch is the Tune (the master key and scale snap it). A step's gate lasts to the end of the step, or of the run of
// tied steps it starts: then the amp envelope releases. A tied step does not retrigger: the note carries on and glides
// to the new pitch over the Glide time.
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
        carryMul = std::exp(-1.0f / (0.0015f * (float) sampleRate));   // the old note fades out over about 1.5 ms at a retrigger
        carry = lastGain = 0.0f;
    }

    // rootHz: pitch; wave: 0/1/2; sub 0..1; cut Hz; res 0..1; env 0..1 (filter env depth);
    // edecMs filter-env decay; atkMs/decMs amp attack/decay; sus 0..1; drive 0..1; relMs amp release after the gate
    // (the default is so long that a note with no gate, e.g. a direct trigger, is unaffected); glideMs the slide time of a tied note.
    void setParams(float rootHz, int wave_, float sub_, float cutHz, float res,
                   float env_, float edecMs, float atkMs, float decMs, float sus_, float drv,
                   float relMs = 5000.0f, float glideMs = 0.0f, float fatkMs = 0.0f, float fsus_ = 0.0f, float frelMs = 0.0f, int filterType = 0)
    {
        // The filter's type and the rest of its envelope (attack, sustain level, release). With none set (attack 0, sustain 0,
        // release 0) the envelope is the old one: an instant open that decays on over E.Dec, whatever the gate does.
        filter.setType(filterType);
        fAtkSamples = fatkMs < 0.1f ? 0.0f : fatkMs * 0.001f * (float) sampleRate;
        fSustain    = juce::jlimit(0.0f, 1.0f, fsus_);
        fRelSamples = frelMs < 1.0f ? 0.0f : frelMs * 0.001f * (float) sampleRate;
        relMul    = std::exp(-1.0f / juce::jmax(1.0f, relMs * 0.001f * (float) sampleRate));
        glideCoef = glideMs < 1.0f ? 0.0f : 1.0f - std::exp(-3.0f / (glideMs * 0.001f * (float) sampleRate));
        rootFreq = juce::jmax(10.0f, rootHz);
        wave     = juce::jlimit(0, 2, wave_);
        sub      = juce::jlimit(0.0f, 1.0f, sub_);
        baseCut  = cutHz;
        envDepth = juce::jlimit(0.0f, 1.0f, env_);
        filterEnvInv = 1.0f / juce::jmax(1.0f, edecMs * 0.001f * (float) sampleRate);
        atkInv   = 1.0f / juce::jmax(1.0f, atkMs * 0.001f * (float) sampleRate);
        decayEnv.setTimeConstant(decMs * 0.001f * (float) sampleRate);
        sustain  = juce::jlimit(0.0f, 1.0f, sus_);
        filter.setResonance(juce::jlimit(0.0f, 1.0f, res));
        filter.setDrive(juce::jlimit(0.0f, 1.0f, drv));
    }

    // `tied`: the step continues the note before it (no retrigger, a glide to the new pitch). `gateSamples` is how long
    // the note is held before its release (0: held until the next trigger).
    // `semitones` transposes the step from the Tune (the Step panel's Octave and Note).
    void trigger(float velocity, int onset = 0, bool tied = false, int gateSamples = 0, float semitones = 0.0f) noexcept
    {
        restart = true; pendingVel = velocity; pendingOnset = juce::jmax(0, onset); pendingTied = tied; pendingGate = juce::jmax(0, gateSamples);
        pendingMul = std::exp2(semitones * (1.0f / 12.0f));
    }

    // Silence the voice + clear the filter ring immediately (e.g. transport stop).
    void reset() noexcept { feHold = feAtRelease = relT = 0.0f; carry = lastGain = 0.0f; active = false; restart = false; releasing = false; gliding = false; gateLeft = 0; filter.reset(); }

    // The filter's drive smoothing back to where prepare() leaves it (0), so the next block ramps the drive in
    // as a freshly prepared engine's does (a track arriving in another slot). Allocation-free.
    void resetFilterSmoothing() noexcept { filter.resetSmoothers(); }

    void render(juce::AudioBuffer<float>& buf, int n)
    {
        const int chs = buf.getNumChannels();

        // Block-rate filter envelope: cutoff sweeps from base+depth down to base. On the
        // block a trigger lands, t resets to 0 mid-loop — use 0 here too so the filter
        // envelope opens on the onset block instead of reading the previous block's t.
        // The filter envelope: opens over its attack, falls to its sustain level over the decay, and after the gate falls away over
        // its release (a release of 0 keeps the decay going). A tied step does not reopen it.
        float fEnv = 0.0f;
        if (active || restart)
        {
            if (releasing && ! restart && fRelSamples > 0.0f)
            {
                fEnv = feAtRelease * std::exp(-relT / fRelSamples);   // from where it was when the gate ended
                relT += (float) n;
            }
            else
            {
                const float envT = (restart && ! (pendingTied && active)) ? 0.0f : t;
                fEnv = envT < fAtkSamples ? envT / fAtkSamples
                                          : fSustain + (1.0f - fSustain) * std::exp(-(envT - fAtkSamples) * filterEnvInv);
                feHold = fEnv;
            }
        }
        const float cutoff = juce::jlimit(20.0f, (float) sampleRate * 0.45f,
                                          baseCut + fEnv * envDepth * 6000.0f);
        filter.setCutoff(cutoff);

        for (int i = 0; i < n; ++i)
        {
            // Sample-accurate onset: a step landing mid-block starts the voice at its offset.
            if (restart && i >= pendingOnset)
            {
                restart = false;
                stepMul = pendingMul;
                if (pendingTied && active && ! releasing)
                {
                    // A tied step: the note carries on (envelopes untouched; the level eases to the step's velocity) and
                    // slides to the new pitch.
                    gliding = glideCoef > 0.0f;
                    if (! gliding) curFreq = rootFreq * stepMul;
                    velTarget = pendingVel;
                }
                else
                {
                    // A new note on a voice still sounding (a step with no gate holds until the next trigger) would drop from its
                    // held level to zero and restart the phase: a step the ear hears as a click, plainest on a sine. Keep the phase
                    // and let the old level fade out (carry) under the new note's attack.
                    const bool wasSounding = active;
                    carry = wasSounding ? lastGain : 0.0f;
                    if (! wasSounding) { phase = 0.0f; subPhase = 0.0f; }
                    active = true; t = 0.0f; amp = 0.0f; vel = velTarget = pendingVel; decayEnv.reset(1.0f);
                    curFreq = rootFreq * stepMul; gliding = false; releasing = false; relGain = 1.0f; gateLeft = pendingGate;
                }
            }
            vel += (velTarget - vel) * 0.004f;   // about 5 ms: a tied step's accent or ghost changes level without a click

            // Pitch: follows the Tune, or slides toward it after a tied step (held until a pending tie's own sample).
            const float targetFreq = rootFreq * stepMul;
            if (gliding)
            {
                curFreq += (targetFreq - curFreq) * glideCoef;
                if (std::abs(targetFreq - curFreq) < 0.01f) { curFreq = targetFreq; gliding = false; }
            }
            else if (! (restart && pendingTied && active))
                curFreq = targetFreq;

            float s = 0.0f;
            if (active)
            {
                // Amp env: linear attack to 1, exponential decay toward the sustain floor.
                if (t < attackSamples()) amp = t * atkInv;
                else                     amp = sustain + (1.0f - sustain) * decayEnv.next();   // decay starts after the attack
                // Gate: when the held time is up the note releases (its level falls by the Release time).
                if (gateLeft > 0 && --gateLeft == 0) { releasing = true; feAtRelease = feHold; relT = 0.0f; }
                if (releasing) relGain *= relMul;
                amp *= vel * relGain;

                const float main = osc(phase, wave);
                const float subS = std::sin(subPhase);
                s = (main + sub * subS) * (amp + carry);
                carry *= carryMul;
                lastGain = amp;

                phase    += twoPi * curFreq / (float) sampleRate;         if (phase    > twoPi) phase    -= twoPi;
                subPhase += twoPi * (curFreq * 0.5f) / (float) sampleRate;  if (subPhase > twoPi) subPhase -= twoPi;
                t += 1.0f;
                if ((sustain < 1.0e-4f && amp < 1.0e-4f && t > attackSamples() + 16.0f) || (releasing && relGain < 1.0e-4f)) active = false;
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
    float  filterEnvInv = 0.004f, atkInv = 0.2f, sustain = 0.0f;
    mu_core::ExpDecay decayEnv;   // amp decay toward the sustain floor
    float  phase = 0.0f, subPhase = 0.0f, t = 0.0f, amp = 0.0f, vel = 1.0f, pendingVel = 1.0f;
    float  fAtkSamples = 0.0f, fSustain = 0.0f, fRelSamples = 0.0f, feHold = 0.0f, feAtRelease = 0.0f, relT = 0.0f;   // the filter envelope's attack, sustain, release and where it stood
    float  carry = 0.0f, lastGain = 0.0f, carryMul = 0.99f;   // the previous note's level at a retrigger, fading out under the new one
    int    pendingOnset = 0, pendingGate = 0, gateLeft = 0;
    float  stepMul = 1.0f, pendingMul = 1.0f, velTarget = 1.0f;   // the step's transposition (now, and for the trigger waiting); the level the velocity eases to
    float  curFreq = 41.2f, relGain = 1.0f, relMul = 0.999f, glideCoef = 0.0f;   // the sounding pitch; the release level and its per-sample step; the glide step
    bool   active = false, restart = false, pendingTied = false, gliding = false, releasing = false;
};

} // namespace mu_on
