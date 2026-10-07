#include "SynthVoice.h"
#include "Scales.h"

namespace mu_tant
{

void VoiceEngine::prepare(double sampleRate, int blockSize)
{
    sr = sampleRate > 0 ? sampleRate : 44100.0;
    oscs.prepare(sr);
    filter1.prepare(sr, blockSize, 1);
    filter1.reset();
    filter2.prepare(sr, blockSize, 1);
    filter2.reset();
    mono .setSize(1, blockSize, false, false, true);  mono .clear();
    mono2.setSize(1, blockSize, false, false, true);  mono2.clear();
}

void VoiceEngine::setBank(const WavetableBank* b) noexcept
{
    oscs.setBank(b);
}

void VoiceEngine::setConfig(const VoiceConfig& c)
{
    cfg = c;

    // pitchOffsetSemis transposes the whole voice (0 in Free mode; the held-note
    // offset in Note mode) — added after toneToMidi so scale intervals are preserved.
    const float midi1 = toneToMidi(cfg.scaleIndex, cfg.root, cfg.osc1Octave + kBaseOctave,
                                   (float) cfg.osc1Semi, (float) cfg.osc1Fine)
                        + cfg.pitchOffsetSemis + cfg.osc1SemiMod;
    const float midi2 = toneToMidi(cfg.scaleIndex, cfg.root, cfg.osc2Octave + kBaseOctave,
                                   (float) cfg.osc2Semi, (float) cfg.osc2Fine)
                        + cfg.pitchOffsetSemis + cfg.osc2SemiMod;
    oscs.osc1.setFrequency(midiToFreq(midi1));
    oscs.osc2.setFrequency(midiToFreq(midi2));
    // Position is a 0..255 frame index; normalise to the osc's 0..1 scan input.
    oscs.osc1.setPosition(cfg.osc1Pos / 255.0f);
    oscs.osc2.setPosition(cfg.osc2Pos / 255.0f);
    oscs.osc1.setTable(cfg.osc1Wavetable);
    oscs.osc2.setTable(cfg.osc2Wavetable);

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

    // Osc 1 + Osc 2 through the shared 2-lane X-Mod (Lane A phase / index, Lane B amplitude),
    // then the per-source levels + noise into the mono work buffer.
    mu_wavetable::XModSettings xm;
    xm.phaseMode = cfg.xmodPhaseMode;  xm.index = cfg.xmodIndex;  xm.sync    = cfg.sync;
    xm.feedback  = cfg.xmodFeedback;   xm.ampMode = cfg.xmodAmpMode; xm.depth = cfg.xmodDepth;
    xm.ssbHz     = cfg.xmodSsbHz;
    oscs.beginBlock(xm, ns);
    for (int i = 0; i < ns; ++i)
    {
        const auto o = oscs.next();
        const float n = noise.render(noiseType);
        m[i] = (o.carrier * osc1Gain + o.modulator * osc2Gain + n * noiseGain) * gain;
    }
    oscs.endBlock();

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
