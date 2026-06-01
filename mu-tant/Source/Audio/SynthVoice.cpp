#include "SynthVoice.h"
#include "Scales.h"

namespace mu_tant
{

// FM here is phase-modulation (the common "FM" in samplers/synths): osc B's
// output is added to osc A's read phase, scaled by the cross-mod amount. Half a
// cycle at full amount is a strong, still-stable index for a first stab.
static constexpr float kFmDepth = 0.5f;

void VoiceEngine::prepare(double sampleRate, int blockSize)
{
    sr = sampleRate > 0 ? sampleRate : 44100.0;
    osc1.prepare(sr);
    osc2.prepare(sr);
    filter.prepare(sr, blockSize, 1);
    filter.reset();
    mono.setSize(1, blockSize, false, false, true);
    mono.clear();
}

void VoiceEngine::setBank(const WavetableBank* b) noexcept
{
    osc1.setBank(b);
    osc2.setBank(b);
}

// Base octave the per-osc octave offset (-3..+3) sits on, so the playable
// range lands in an audible drone register (octave 0 → MIDI ~48 = C3).
static constexpr int kBaseOctave = 4;

void VoiceEngine::setConfig(const VoiceConfig& c)
{
    cfg = c;

    const float midi1 = toneToMidi(cfg.scaleIdx, cfg.root, cfg.osc1Octave + kBaseOctave,
                                   (float) cfg.osc1Semi, (float) cfg.osc1Fine);
    const float midi2 = toneToMidi(cfg.scaleIdx, cfg.root, cfg.osc2Octave + kBaseOctave,
                                   (float) cfg.osc2Semi, (float) cfg.osc2Fine);
    osc1.setFrequency(midiToFreq(midi1));
    osc2.setFrequency(midiToFreq(midi2));
    // Position is a 0..255 frame index; normalise to the osc's 0..1 scan input.
    osc1.setPosition(cfg.osc1Pos / 255.0f);
    osc2.setPosition(cfg.osc2Pos / 255.0f);

    filter.setType(cfg.filterType);
    filter.setCutoff(cfg.filterCutoff);
    filter.setResonance(cfg.filterRes);
    filter.setDrive(cfg.filterDrive);
    filter.setLowCut(cfg.filterLowCutHz);

    gain      = juce::Decibels::decibelsToGain(cfg.levelDb,      -60.0f);
    osc1Gain  = juce::Decibels::decibelsToGain(cfg.osc1LevelDb,  -60.0f);
    osc2Gain  = juce::Decibels::decibelsToGain(cfg.osc2LevelDb,  -60.0f);
    noiseGain = juce::Decibels::decibelsToGain(cfg.noiseLevelDb, -60.0f);
}

void VoiceEngine::process(juce::AudioBuffer<float>& out, int numSamples)
{
    const int ns = juce::jmin(numSamples, mono.getNumSamples());
    if (ns <= 0) return;

    float* m = mono.getWritePointer(0);
    // xmod is 0..127 in the UI; normalise to the 0..1 FM index.
    const float xmodNorm = (float) cfg.xmod / 127.0f;
    const auto  noiseType = static_cast<NoiseGen::Type>(cfg.noiseType);

    for (int i = 0; i < ns; ++i)
    {
        // Osc2 renders first; osc1 can FM-modulate from osc2's current output.
        const float b = osc2.render();

        // Cross-mod: determine the phase-mod input for osc1.
        const float phaseMod = (cfg.xmodMode == FM) ? (xmodNorm * b * kFmDepth) : 0.0f;
        float a = osc1.render(phaseMod);

        // AM (amplitude modulation): osc2 modulates osc1's amplitude.
        if (cfg.xmodMode == AM)
            a *= (1.0f + xmodNorm * b);   // standard AM: carrier × (1 + idx × modulator)

        // Ring Mod: crossfade from dry osc1 to osc1×osc2.
        if (cfg.xmodMode == RingMod)
            a = a * (1.0f - xmodNorm) + (a * b) * xmodNorm;

        // Hard sync: osc1 wrap resets osc2 phase (takes effect next sample).
        if (cfg.sync && osc1.justWrapped())
            osc2.resetPhase();

        const float n = noise.render(noiseType);
        m[i] = (a * osc1Gain + b * osc2Gain + n * noiseGain) * gain;
    }

    // Filter (mu-core) in place on the mono work buffer — drive + main filter + lo-cut all inside.
    filter.process(mono, ns, 1);

    // Sum the mono voice into every output channel.
    for (int ch = 0; ch < out.getNumChannels(); ++ch)
        out.addFrom(ch, 0, mono, 0, 0, ns);
}

} // namespace mu_tant
