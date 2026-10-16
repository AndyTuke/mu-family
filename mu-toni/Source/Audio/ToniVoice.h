#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include "Audio/EnvelopeParams.h"          // mu-core: ADSR parameter updates that do not cut a release
#include "Audio/MultiModeFilter.h"          // mu-core (reused unchanged)
#include "Audio/NoiseSeed.h"               // mu-core: per-slot, per-instance noise seeding
#include "Audio/Wavetable/XModOscPair.h"   // mu-core: wavetable oscs + 2-lane X-Mod (shared with mu-Tant)
#include "Audio/ToniScales.h"                 // midiToFreq

// μ-Toni voice — a note-triggered wavetable mono synth (mu-Tant's oscillators + X-Mod).
//   Osc1 + Osc2 (cross-modulated) (+ noise) → MultiModeFilter → Amp VCA → (caller adds insert)
// Amp / Filter / Pitch ADSR (juce::ADSR), portamento glide, legato tie.
// The arpeggiator drives noteOn/noteOnLegato/noteOff; the voice renders into a
// stereo buffer. See docs/mu-toni/design-sequencer.md.
namespace mu_toni
{

// Basic Shapes' saw: its scan runs sine → triangle → saw → square, so 2/3 of the way (frame 170
// of 0..255) — the default, so a fresh patch sounds as the analogue saw did.
inline constexpr int kSawPosition = 170;

struct ToniVoiceParams
{
    // Osc 1 / Osc 2 — a wavetable from the shared bank and its scan position (frame 0..255).
    int   osc1Table = 0,                 osc2Table = 0;
    float osc1Pos   = (float) kSawPosition, osc2Pos = (float) kSawPosition;
    int   osc1Oct   = 0,                 osc2Oct   = 0;
    float osc2Semi  = 0.0f;                        // osc2 semitone detune
    float osc1Fine  = 0.0f,              osc2Fine  = 7.0f;   // cents (slight detune = fat)
    float osc1LevelDb = 0.0f,            osc2LevelDb = -3.0f;
    mu_wavetable::XModSettings xmod;               // Osc 2 → Osc 1 cross-mod + sync
    float noiseLevelDb = -60.0f;                   // -60 dB ≡ off
    int   noiseType    = 0;                        // 0 = White, 1 = Pink

    // Filter
    int   filterType = 0;                          // LP12
    float cutoff     = 4000.0f;
    float resonance  = 0.2f;
    float drive      = 0.0f;
    float lowCutHz   = 0.0f;                        // 0 = off

    // Amp ADSR (seconds) + output level
    float ampA = 0.004f, ampD = 0.20f, ampS = 0.80f, ampR = 0.30f;
    float ampLevelDb = 0.0f;

    // Filter ADSR + depth (−1..+1, octaves of cutoff sweep)
    float fA = 0.004f, fD = 0.30f, fS = 0.0f, fR = 0.30f;
    float filterEnvDepth = 0.0f;

    // Pitch ADSR + depth (semitones, ±24) — 0 by default (inert)
    float pA = 0.004f, pD = 0.10f, pS = 0.0f, pR = 0.10f;
    float pitchEnvDepth  = 0.0f;
    int   pitchEnvTarget = 1;                      // 0 = Osc 1 + 2, 1 = Osc 2 only

    // Articulation
    float portamentoMs = 0.0f;
    bool  legato       = false;

    float pan = 0.0f;                              // −1..+1
};

class ToniVoice
{
public:
    void setBank(const mu_wavetable::WavetableBank* b) noexcept { oscs.setBank(b); }

    // Restarts the noise from the seed for this slot under the instance salt: layers do not share one sequence,
    // and salt 0 (deterministic mode) makes a slot's noise bit-repeatable.
    void setSlotIndex(int slot, juce::int64 salt = 0) noexcept { rng.setSeed(mu_core::noiseSeedFor(kNoiseSeed, salt, slot)); }

    void prepare(double sampleRate, int blockSize)
    {
        sr = sampleRate > 0 ? sampleRate : 44100.0;
        oscs.prepare(sr);
        filter.prepare(sr, blockSize, 1);
        ampEnv.setSampleRate(sr); filterEnv.setSampleRate(sr); pitchEnv.setSampleRate(sr);
        mono.setSize(1, blockSize, false, false, true);
        pitchGlide.reset(sr, 0.0);
        accentLevel.reset(sr, 0.005);   // a tied (legato) note's accent change glides, no click
        applyEnvParams();
    }

    void setParams(const ToniVoiceParams& p)
    {
        params = p;
        oscs.osc1.setTable(p.osc1Table);  oscs.osc2.setTable(p.osc2Table);
        oscs.osc1.setPosition(p.osc1Pos / 255.0f);
        oscs.osc2.setPosition(p.osc2Pos / 255.0f);

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
        filter.setLowCut(p.lowCutHz);

        // The glide time is read when a slide starts (noteOnLegato), never mid-slide: re-seeding the ramp
        // here (SmoothedValue::reset jumps to the target) cancelled a slide in flight whenever Glide was
        // modulated or automated.
        glideSeconds = (double) p.portamentoMs * 0.001;
        applyEnvParams();
    }

    // Hard note — retrigger envelopes, snap pitch, reset osc phase for a clean attack.
    void noteOn(int midiNote)
    {
        targetMidi = (float) midiNote;
        pitchGlide.setCurrentAndTargetValue(targetMidi);
        oscs.osc1.resetPhase(); oscs.osc2.resetPhase();
        releasing = false;
        applyEnvParams();   // settings held back during a release apply now, before the new note's envelopes start
        ampEnv.noteOn(); filterEnv.noteOn(); pitchEnv.noteOn();
    }

    // Legato/tied note — no envelope retrigger, glide pitch toward the new note.
    void noteOnLegato(int midiNote)
    {
        targetMidi = (float) midiNote;
        // Start the slide from where the pitch is now, over the glide time as it stands now.
        const float cur = pitchGlide.getCurrentValue();
        pitchGlide.reset(sr, glideSeconds);            // sets the ramp length (and jumps current to target)
        pitchGlide.setCurrentAndTargetValue(cur);      // keep the pitch where it is
        pitchGlide.setTargetValue(targetMidi);         // and slide to the new note
    }

    void noteOff() { releasing = true; ampEnv.noteOff(); filterEnv.noteOff(); pitchEnv.noteOff(); }

    // The accent of the note about to start, 0..1 (303-style): up to kAccentDb louder and its
    // filter up to kAccentOctaves brighter. `glide` (a tied note) moves to it smoothly; otherwise
    // the new note starts at it.
    void setAccent(float accent01, bool glide)
    {
        const float a = juce::jlimit(0.0f, 1.0f, accent01);
        if (glide) accentLevel.setTargetValue(a);
        else       accentLevel.setCurrentAndTargetValue(a);
    }

    void reset()
    {
        ampEnv.reset(); filterEnv.reset(); pitchEnv.reset();
        filter.reset();
    }

    bool isActive() const noexcept { return ampEnv.isActive(); }

    // Renders and ADDS the voice into `out` (stereo). numSamples ≤ prepared block.
    void process(juce::AudioBuffer<float>& out, int numSamples) { process(out, 0, numSamples); }

    // The same into out[startSample, startSample + numSamples) — a span of a block, so a step or
    // a note-off can land on its exact sample.
    void process(juce::AudioBuffer<float>& out, int startSample, int numSamples)
    {
        if (! ampEnv.isActive() || numSamples <= 0) return;

        // Block-rate control: sample filter/pitch envelopes (advance n to stay in sync).
        const float envF = filterEnv.getNextSample();
        const float envP = pitchEnv.getNextSample();
        for (int i = 1; i < numSamples; ++i) { filterEnv.getNextSample(); pitchEnv.getNextSample(); }

        const float baseMidi = pitchGlide.getCurrentValue();
        pitchGlide.skip(numSamples);

        // Pitch envelope → osc 2 only, or both oscillators (per pitchEnvTarget).
        const float envPitch = params.pitchEnvDepth * envP;
        const float m1 = baseMidi + osc1Semis + (params.pitchEnvTarget == 0 ? envPitch : 0.0f);
        const float m2 = baseMidi + osc2Semis + envPitch;
        oscs.osc1.setFrequency(midiToFreq(m1));
        oscs.osc2.setFrequency(midiToFreq(m2));

        // Filter cutoff with envelope (depth in octaves), clamped to a safe range.
        const float accentNow = accentLevel.getCurrentValue();
        const float cut = juce::jlimit(20.0f, (float) (0.45 * sr),
                                       params.cutoff * std::pow(2.0f, params.filterEnvDepth * envF * kFilterEnvOctaves
                                                                      + accentNow * kAccentOctaves));
        filter.setCutoff(cut);

        // Osc 1 (carrier) + Osc 2 (modulator) through the 2-lane X-Mod, plus noise.
        float* m = mono.getWritePointer(0);
        oscs.beginBlock(params.xmod, numSamples);
        for (int i = 0; i < numSamples; ++i)
        {
            const auto o = oscs.next();
            float s = o.carrier * osc1Gain + o.modulator * osc2Gain;
            if (noiseGain > 0.0f) s += renderNoise() * noiseGain;
            m[i] = s;
        }
        oscs.endBlock();

        filter.process(mono, numSamples, 1);

        float* L = out.getWritePointer(0) + startSample;
        float* R = (out.getNumChannels() > 1 ? out.getWritePointer(1) : out.getWritePointer(0)) + startSample;
        const float lg = panL * levelGain, rg = panR * levelGain;
        for (int i = 0; i < numSamples; ++i)
        {
            const float a = ampEnv.getNextSample() * (1.0f + (kAccentGainMax - 1.0f) * accentLevel.getNextValue());
            const float v = m[i] * a;
            L[i] += v * lg;
            R[i] += v * rg;
        }
    }

private:
    void applyEnvParams()
    {
        // Only when changed and not mid-release (see mu_env): ADSR::setParameters would otherwise cut a release.
        mu_env::applyParameters(ampEnv,    { params.ampA, params.ampD, params.ampS, params.ampR }, releasing);
        mu_env::applyParameters(filterEnv, { params.fA,   params.fD,   params.fS,   params.fR   }, releasing);
        mu_env::applyParameters(pitchEnv,  { params.pA,   params.pD,   params.pS,   params.pR   }, releasing);
    }

    // White or pink noise (Paul Kellet economy pink filter — cheap, stable).
    float renderNoise() noexcept
    {
        const float white = rng.nextFloat() * 2.0f - 1.0f;
        if (params.noiseType == 0) return white;
        pb0 = 0.99765f * pb0 + white * 0.0990460f;
        pb1 = 0.96300f * pb1 + white * 0.2965164f;
        pb2 = 0.57000f * pb2 + white * 1.0526913f;
        return (pb0 + pb1 + pb2 + white * 0.1848f) * 0.2f;
    }

    static constexpr float kFilterEnvOctaves = 5.0f;
    static constexpr float kAccentDb         = 6.0f;                 // full accent: +6 dB …
    static constexpr float kAccentGainMax    = 1.9952623f;           //   … as a gain (10^(6/20))
    static constexpr float kAccentOctaves    = 1.0f;                 // … and the filter an octave up

    double sr = 44100.0;
    ToniVoiceParams params;

    mu_wavetable::XModOscPair oscs;   // Osc 1 (carrier) + Osc 2 (modulator)
    static constexpr juce::int64 kNoiseSeed = 0x6D75546F;   // "muTo"
    juce::Random rng { mu_core::noiseSeedFor(kNoiseSeed, 0, 0) };
    MultiModeFilter filter;
    juce::ADSR ampEnv, filterEnv, pitchEnv;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> pitchGlide;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> accentLevel;   // 0..1, the playing note's accent
    juce::AudioBuffer<float> mono;

    float targetMidi = 60.0f;
    float osc1Semis = 0.0f, osc2Semis = 0.0f;
    float pb0 = 0.0f, pb1 = 0.0f, pb2 = 0.0f;   // pink-noise filter state
    float osc1Gain = 1.0f, osc2Gain = 0.7f, noiseGain = 0.0f, levelGain = 1.0f;
    float panL = 0.7071f, panR = 0.7071f;
    double glideSeconds = 0.0;   // portamento time, taken when a tied note starts a slide
    bool   releasing    = false; // set at noteOff, cleared at noteOn: envelope parameters wait while it is set
};

} // namespace mu_toni
