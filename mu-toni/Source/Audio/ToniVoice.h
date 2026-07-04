#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include "Audio/MultiModeFilter.h"   // mu-core (reused unchanged)
#include "AnalogueOsc.h"
#include "Scales.h"                    // midiToFreq

// μ-Toni MVP voice — a note-triggered analogue-style mono synth.
//   Osc1 + Osc2 (+ noise) → MultiModeFilter → Amp VCA → (caller adds insert)
// Amp / Filter / Pitch ADSR (juce::ADSR), portamento glide, legato tie.
// The arpeggiator drives noteOn/noteOnLegato/noteOff; the voice renders into a
// stereo buffer. See docs/mu-toni/design-sequencer.md.
namespace mu_toni
{

struct ToniVoiceParams
{
    // Osc 1 / Osc 2
    int   osc1Shape = AnalogueOsc::Saw,  osc2Shape = AnalogueOsc::Saw;
    int   osc1Oct   = 0,                 osc2Oct   = 0;
    float osc2Semi  = 0.0f;                        // osc2 semitone detune
    float osc1Fine  = 0.0f,              osc2Fine  = 7.0f;   // cents (slight detune = fat)
    float osc1LevelDb = 0.0f,            osc2LevelDb = -3.0f;
    float pulseWidth  = 0.5f;                      // shared PW for Pulse shape
    float noiseLevelDb = -60.0f;                   // -60 dB ≡ off

    // Filter
    int   filterType = 0;                          // LP12
    float cutoff     = 4000.0f;
    float resonance  = 0.2f;
    float drive      = 0.0f;

    // Amp ADSR (seconds) + output level
    float ampA = 0.004f, ampD = 0.20f, ampS = 0.80f, ampR = 0.30f;
    float ampLevelDb = 0.0f;

    // Filter ADSR + depth (−1..+1, octaves of cutoff sweep)
    float fA = 0.004f, fD = 0.30f, fS = 0.0f, fR = 0.30f;
    float filterEnvDepth = 0.0f;

    // Pitch ADSR + depth (semitones, ±24) — 0 by default (inert)
    float pA = 0.004f, pD = 0.10f, pS = 0.0f, pR = 0.10f;
    float pitchEnvDepth = 0.0f;

    // Articulation
    float portamentoMs = 0.0f;
    bool  legato       = false;

    float pan = 0.0f;                              // −1..+1
};

class ToniVoice
{
public:
    void prepare(double sampleRate, int blockSize)
    {
        sr = sampleRate > 0 ? sampleRate : 44100.0;
        osc1.prepare(sr); osc2.prepare(sr);
        filter.prepare(sr, blockSize, 1);
        ampEnv.setSampleRate(sr); filterEnv.setSampleRate(sr); pitchEnv.setSampleRate(sr);
        mono.setSize(1, blockSize, false, false, true);
        pitchGlide.reset(sr, 0.0);
        applyEnvParams();
    }

    void setParams(const ToniVoiceParams& p)
    {
        params = p;
        osc1.setShape(p.osc1Shape); osc2.setShape(p.osc2Shape);
        osc1.setPulseWidth(p.pulseWidth); osc2.setPulseWidth(p.pulseWidth);

        osc1Semis = (float) p.osc1Oct * 12.0f + p.osc1Fine * 0.01f;
        osc2Semis = (float) p.osc2Oct * 12.0f + p.osc2Semi + p.osc2Fine * 0.01f;

        osc1Gain  = juce::Decibels::decibelsToGain(p.osc1LevelDb, -60.0f);
        osc2Gain  = juce::Decibels::decibelsToGain(p.osc2LevelDb, -60.0f);
        noiseGain = p.noiseLevelDb <= -60.0f ? 0.0f : juce::Decibels::decibelsToGain(p.noiseLevelDb, -60.0f);
        levelGain = juce::Decibels::decibelsToGain(p.ampLevelDb, -60.0f);

        const float pan01 = 0.5f * (juce::jlimit(-1.0f, 1.0f, p.pan) + 1.0f);
        panL = std::cos(pan01 * 0.5f * juce::MathConstants<float>::pi);
        panR = std::sin(pan01 * 0.5f * juce::MathConstants<float>::pi);

        filter.setType(p.filterType);
        filter.setResonance(p.resonance);
        filter.setDrive(p.drive);

        if (p.portamentoMs != lastPortaMs)
        {
            lastPortaMs = p.portamentoMs;
            const float cur = pitchGlide.getCurrentValue();
            pitchGlide.reset(sr, (double) p.portamentoMs * 0.001);
            pitchGlide.setCurrentAndTargetValue(cur);
        }
        applyEnvParams();
    }

    // Hard note — retrigger envelopes, snap pitch, reset osc phase for a clean attack.
    void noteOn(int midiNote)
    {
        targetMidi = (float) midiNote;
        pitchGlide.setCurrentAndTargetValue(targetMidi);
        osc1.resetPhase(); osc2.resetPhase();
        ampEnv.noteOn(); filterEnv.noteOn(); pitchEnv.noteOn();
    }

    // Legato/tied note — no envelope retrigger, glide pitch toward the new note.
    void noteOnLegato(int midiNote)
    {
        targetMidi = (float) midiNote;
        pitchGlide.setTargetValue(targetMidi);
    }

    void noteOff() { ampEnv.noteOff(); filterEnv.noteOff(); pitchEnv.noteOff(); }

    void reset()
    {
        ampEnv.reset(); filterEnv.reset(); pitchEnv.reset();
        filter.reset();
    }

    bool isActive() const noexcept { return ampEnv.isActive(); }

    // Renders and ADDS the voice into `out` (stereo). numSamples ≤ prepared block.
    void process(juce::AudioBuffer<float>& out, int numSamples)
    {
        if (! ampEnv.isActive() || numSamples <= 0) return;

        // Block-rate control: sample filter/pitch envelopes (advance n to stay in sync).
        const float envF = filterEnv.getNextSample();
        const float envP = pitchEnv.getNextSample();
        for (int i = 1; i < numSamples; ++i) { filterEnv.getNextSample(); pitchEnv.getNextSample(); }

        const float baseMidi = pitchGlide.getCurrentValue();
        pitchGlide.skip(numSamples);

        const float midi = baseMidi + params.pitchEnvDepth * envP;
        osc1.setFrequency(midiToFreq(midi + osc1Semis));
        osc2.setFrequency(midiToFreq(midi + osc2Semis));

        // Filter cutoff with envelope (depth in octaves), clamped to a safe range.
        const float cut = juce::jlimit(20.0f, (float) (0.45 * sr),
                                       params.cutoff * std::pow(2.0f, params.filterEnvDepth * envF * kFilterEnvOctaves));
        filter.setCutoff(cut);

        float* m = mono.getWritePointer(0);
        for (int i = 0; i < numSamples; ++i)
        {
            float s = osc1.render() * osc1Gain + osc2.render() * osc2Gain;
            if (noiseGain > 0.0f) s += (rng.nextFloat() * 2.0f - 1.0f) * noiseGain;
            m[i] = s;
        }

        filter.process(mono, numSamples, 1);

        float* L = out.getWritePointer(0);
        float* R = out.getNumChannels() > 1 ? out.getWritePointer(1) : L;
        const float lg = panL * levelGain, rg = panR * levelGain;
        for (int i = 0; i < numSamples; ++i)
        {
            const float a = ampEnv.getNextSample();
            const float v = m[i] * a;
            L[i] += v * lg;
            R[i] += v * rg;
        }
    }

private:
    void applyEnvParams()
    {
        ampEnv.setParameters   ({ params.ampA, params.ampD, params.ampS, params.ampR });
        filterEnv.setParameters({ params.fA,   params.fD,   params.fS,   params.fR   });
        pitchEnv.setParameters ({ params.pA,   params.pD,   params.pS,   params.pR   });
    }

    static constexpr float kFilterEnvOctaves = 5.0f;

    double sr = 44100.0;
    ToniVoiceParams params;

    AnalogueOsc osc1, osc2;
    juce::Random rng;
    MultiModeFilter filter;
    juce::ADSR ampEnv, filterEnv, pitchEnv;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> pitchGlide;
    juce::AudioBuffer<float> mono;

    float targetMidi = 60.0f;
    float osc1Semis = 0.0f, osc2Semis = 0.0f;
    float osc1Gain = 1.0f, osc2Gain = 0.7f, noiseGain = 0.0f, levelGain = 1.0f;
    float panL = 0.7071f, panR = 0.7071f;
    float lastPortaMs = -1.0f;
};

} // namespace mu_toni
