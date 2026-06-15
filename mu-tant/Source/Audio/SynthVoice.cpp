#include "SynthVoice.h"
#include "Scales.h"

namespace mu_tant
{

// FM is phase-modulation: osc B's output shifts osc A's read phase.
// 0.5 cycles at full FM depth gives a strong but stable index.
static constexpr float kFmScale = 0.5f;

void VoiceEngine::prepare(double sampleRate, int blockSize)
{
    sr = sampleRate > 0 ? sampleRate : 44100.0;
    osc1.prepare(sr);
    osc2.prepare(sr);
    filter1.prepare(sr, blockSize, 1);
    filter1.reset();
    filter2.prepare(sr, blockSize, 1);
    filter2.reset();
    mono .setSize(1, blockSize, false, false, true);  mono .clear();
    mono2.setSize(1, blockSize, false, false, true);  mono2.clear();
}

void VoiceEngine::setBank(const WavetableBank* b) noexcept
{
    osc1.setBank(b);
    osc2.setBank(b);
}

void VoiceEngine::setConfig(const VoiceConfig& c)
{
    cfg = c;

    // pitchOffsetSemis transposes the whole voice (0 in Free mode; the held-note
    // offset in Note mode) — added after toneToMidi so scale intervals are preserved.
    const float midi1 = toneToMidi(cfg.scaleIdx, cfg.root, cfg.osc1Octave + kBaseOctave,
                                   (float) cfg.osc1Semi, (float) cfg.osc1Fine) + cfg.pitchOffsetSemis;
    const float midi2 = toneToMidi(cfg.scaleIdx, cfg.root, cfg.osc2Octave + kBaseOctave,
                                   (float) cfg.osc2Semi, (float) cfg.osc2Fine) + cfg.pitchOffsetSemis;
    osc1.setFrequency(midiToFreq(midi1));
    osc2.setFrequency(midiToFreq(midi2));
    // Position is a 0..255 frame index; normalise to the osc's 0..1 scan input.
    osc1.setPosition(cfg.osc1Pos / 255.0f);
    osc2.setPosition(cfg.osc2Pos / 255.0f);
    osc1.setTable(cfg.osc1Wavetable);
    osc2.setTable(cfg.osc2Wavetable);

    filter1.setType(cfg.filterType);
    filter1.setCutoff(cfg.filterCutoff);
    filter1.setResonance(cfg.filterRes);
    filter1.setDrive(cfg.filterDrive);
    filter1.setLowCut(cfg.filterLowCutHz);

    filter2.setType(cfg.filter2Type);
    filter2.setCutoff(cfg.filter2Cutoff);
    filter2.setResonance(cfg.filter2Res);
    filter2.setDrive(cfg.filter2Drive);
    filter2.setLowCut(cfg.filter2LowCutHz);

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
    const auto noiseType = static_cast<NoiseGen::Type>(cfg.noiseType);
    // All three cross-mod types are active simultaneously at their individual depths.
    const float fmAmt   = juce::jlimit(0.0f, 1.0f, cfg.xmodFm);
    const float amAmt   = juce::jlimit(0.0f, 1.0f, cfg.xmodAm);
    const float ringAmt = juce::jlimit(0.0f, 1.0f, cfg.xmodRing);

    for (int i = 0; i < ns; ++i)
    {
        // Osc2 renders first; osc1 reads osc2's current output for all cross-mod types.
        const float b = osc2.render();

        // FM (phase modulation): osc2 displaces osc1's read phase.
        const float phaseMod = fmAmt * b * kFmScale;
        float a = osc1.render(phaseMod);

        // AM: osc2 modulates osc1's amplitude (standard AM: carrier × (1 + idx × mod)).
        if (amAmt > 0.0f)   a *= (1.0f + amAmt * b);

        // Ring: crossfade from dry osc1 to osc1 × osc2.
        if (ringAmt > 0.0f) a = a * (1.0f - ringAmt) + (a * b) * ringAmt;

        // Hard sync: osc1 wrap resets osc2 phase (takes effect next sample).
        if (cfg.sync && osc1.justWrapped())
            osc2.resetPhase();

        const float n = noise.render(noiseType);
        m[i] = (a * osc1Gain + b * osc2Gain + n * noiseGain) * gain;
    }

    // Dual filter — series or parallel.
    if (cfg.filterSeries)
    {
        // Series: signal passes through filter1 then filter2.
        filter1.process(mono, ns, 1);
        filter2.process(mono, ns, 1);
    }
    else
    {
        // Parallel: both filters see the same input; outputs are averaged.
        mono2.copyFrom(0, 0, mono, 0, 0, ns);
        filter1.process(mono,  ns, 1);
        filter2.process(mono2, ns, 1);
        // Mix: 0.5× each so combined level matches a single filter.
        auto* a = mono .getWritePointer(0);
        const auto* b = mono2.getReadPointer(0);
        for (int i = 0; i < ns; ++i) a[i] = (a[i] + b[i]) * 0.5f;
    }

    // Sum the mono voice into every output channel.
    for (int ch = 0; ch < out.getNumChannels(); ++ch)
        out.addFrom(ch, 0, mono, 0, 0, ns);
}

} // namespace mu_tant
