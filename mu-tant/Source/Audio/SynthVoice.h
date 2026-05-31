#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include "WavetableOscillator.h"
#include "WavetableBank.h"
#include "Audio/MultiModeFilter.h"   // mu-core (reused unchanged)

// mu-tant per-layer voice (design-voice.md "Per-slot voice chain").
//   Osc1 + Osc2 --(FM / Sync)--> mix --> Filter --> [gate stub] --> [insert stub] --> out
// Free-running drone: no note-on/off, no amp/pitch/filter envelopes. The gate
// stage and the mu-core insert are stubbed in this first stab (the gate is a
// pass-through; insert is not yet wired) and slot in once the rest is proven.
namespace mu_tant
{

struct VoiceConfig
{
    int   root        = 0;     // 0..11 (C..B) — tonal centre, shared by both oscs
    int   scaleIdx    = 0;     // index into kScales

    // Per-oscillator pitch — all integer-stepped.
    //   octave : -3..+3 offset from the base octave
    //   semi   : -12..+12 scale-degree offset (scale-quantised — see Scales.h)
    //   fine   : -100..+100 cents off-grid detune
    int   osc1Octave  = 0,   osc2Octave = 0;
    int   osc1Semi    = 0,   osc2Semi   = 2;
    int   osc1Fine    = 0,   osc2Fine   = 0;

    // Wavetable scan position — frame index 0..255 (256-frame Serum/Vital tables).
    float osc1Pos     = 0.0f, osc2Pos    = 0.0f;

    int   xmod        = 0;     // 0..127 cross-mod amount
    int   xmodMode    = 0;     // 0=Off, 1=FM (B→A phase-mod), 2=AM (B→A amplitude), 3=Ring
    bool  sync        = false; // hard sync: osc1 wraps → reset osc2 phase

    // Per-source levels (replace the old single osc-balance "mix"). dB.
    float osc1LevelDb = 0.0f, osc2LevelDb = -6.0f;
    float noiseLevelDb = -60.0f;   // -60 dB ≡ off
    int   noiseType    = 0;        // 0=White, 1=Pink

    int   filterType      = 0;
    float filterCutoff    = 8000.0f;
    float filterRes       = 0.2f;
    float filterEnvDepth  = 1.0f;  // 0..1 how far the filter envelope sweeps the cutoff
    float levelDb         = -6.0f; // slot output level (feeds the mixer channel)
};

// White + Pink noise generator. Pink uses Paul Kellet's economy filter
// (3-pole approximation) — cheap, no allocation, stable.
class NoiseGen
{
public:
    enum Type { White = 0, Pink = 1 };

    float render(Type type) noexcept
    {
        const float white = rng.nextFloat() * 2.0f - 1.0f;
        if (type == White) return white;

        // Paul Kellet's economy pink filter.
        b0 = 0.99765f * b0 + white * 0.0990460f;
        b1 = 0.96300f * b1 + white * 0.2965164f;
        b2 = 0.57000f * b2 + white * 1.0526913f;
        const float pink = b0 + b1 + b2 + white * 0.1848f;
        return pink * 0.2f;   // scale to roughly ±1
    }

private:
    juce::Random rng;
    float b0 = 0.0f, b1 = 0.0f, b2 = 0.0f;
};

class VoiceEngine
{
public:
    enum XMod { Off = 0, FM = 1, AM = 2, RingMod = 3 };

    void prepare(double sampleRate, int blockSize);
    void setBank(const WavetableBank* b) noexcept;
    void setConfig(const VoiceConfig& c);   // recomputes osc frequencies + filter

    // Renders the voice and ADDS it into `out` (stereo). numSamples <= prepared block size.
    void process(juce::AudioBuffer<float>& out, int numSamples);

private:
    WavetableOscillator      osc1, osc2;
    NoiseGen                 noise;
    MultiModeFilter          filter;
    VoiceConfig              cfg;
    juce::AudioBuffer<float> mono;     // 1-channel work buffer
    double                   sr   = 44100.0;
    float                    gain = 1.0f;       // slot output gain
    float                    osc1Gain = 1.0f;
    float                    osc2Gain = 0.5f;
    float                    noiseGain = 0.0f;
};

} // namespace mu_tant
