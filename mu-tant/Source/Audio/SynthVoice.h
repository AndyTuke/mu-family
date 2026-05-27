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
    int   root        = 0;     // 0..11 (C..B)
    int   scaleIdx    = 0;     // index into kScales
    int   osc1Octave  = 4,   osc2Octave = 3;
    float osc1Tone    = 0.0f, osc2Tone   = 2.0f;   // continuous scale degree
    float osc1Fine    = 0.0f, osc2Fine   = 0.0f;   // cents
    float osc1Pos     = 0.0f, osc2Pos    = 0.0f;   // wavetable position 0..1
    float xmod        = 0.0f;  // 0..1 cross-mod amount
    int   xmodMode    = 0;     // 0=Off, 1=FM (B->A), 2=Sync (A->B)
    float mix         = 0.5f;  // 0=Osc1(A), 1=Osc2(B)
    int   filterType  = 0;
    float filterCutoff= 8000.0f;
    float filterRes   = 0.2f;
    float levelDb     = -6.0f; // slot level
};

class VoiceEngine
{
public:
    enum XMod { Off = 0, FM = 1, Sync = 2 };

    void prepare(double sampleRate, int blockSize);
    void setBank(const WavetableBank* b) noexcept;
    void setConfig(const VoiceConfig& c);   // recomputes osc frequencies + filter

    // Renders the voice and ADDS it into `out` (stereo). numSamples <= prepared block size.
    void process(juce::AudioBuffer<float>& out, int numSamples);

private:
    WavetableOscillator      osc1, osc2;
    MultiModeFilter          filter;
    VoiceConfig              cfg;
    juce::AudioBuffer<float> mono;     // 1-channel work buffer
    double                   sr   = 44100.0;
    float                    gain = 1.0f;
};

} // namespace mu_tant
