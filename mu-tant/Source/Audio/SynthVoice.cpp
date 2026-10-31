#include "SynthVoice.h"
#include "TantScales.h"

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

void VoiceEngine::reset()
{
    oscs.reset();
    filter1.reset();
    filter2.reset();
    filter1.resetSmoothers();   // the next tenant's drive / low-cut ramp from 0, as on a fresh engine
    filter2.resetSmoothers();
    noise.reset();
    mono.clear();               // work buffers: cleared in place, never resized
    mono2.clear();
    haveBlock = false;          // the next block starts its pitch and cutoff at their targets
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
                                   (float) cfg.osc1Semi + cfg.osc1ToneMod, (float) cfg.osc1Fine)
                        + cfg.pitchOffsetSemis + cfg.osc1SemiMod;
    const float midi2 = toneToMidi(cfg.scaleIndex, cfg.root, cfg.osc2Octave + kBaseOctave,
                                   (float) cfg.osc2Semi + cfg.osc2ToneMod, (float) cfg.osc2Fine)
                        + cfg.pitchOffsetSemis + cfg.osc2SemiMod;
    tgtMidi1 = midi1;
    tgtMidi2 = midi2;
    tgtCut1  = cfg.filterCutoff;
    tgtCut2  = cfg.filter2Cutoff;
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
    if (! haveBlock)
    {
        curMidi1 = tgtMidi1;  curMidi2 = tgtMidi2;
        curCut1  = tgtCut1;   curCut2  = tgtCut2;
        haveBlock = true;
    }
    const bool pitchMoves = curMidi1 != tgtMidi1 || curMidi2 != tgtMidi2;
    oscs.beginBlock(xm, ns);
    for (int start = 0; start < ns; start += kRampChunk)
    {
        const int end = juce::jmin(ns, start + kRampChunk);
        if (pitchMoves)
        {
            // Linear in semitones across the block: the value at the end of this chunk.
            const float f = (float) end / (float) ns;
            oscs.osc1.setFrequency(midiToFreq(curMidi1 + (tgtMidi1 - curMidi1) * f));
            oscs.osc2.setFrequency(midiToFreq(curMidi2 + (tgtMidi2 - curMidi2) * f));
        }
        for (int i = start; i < end; ++i)
        {
            const auto o = oscs.next();
            const float n = noise.render(noiseType);
            m[i] = (o.carrier * osc1Gain + o.modulator * osc2Gain + n * noiseGain) * gain;
        }
    }
    oscs.endBlock();
    curMidi1 = tgtMidi1;  curMidi2 = tgtMidi2;

    // Dual filter, series or parallel. When a cutoff moved since the last block (the filter envelope, modulation) it
    // glides geometrically (in Hz) across the block in kRampChunk steps; otherwise one pass, as before.
    const bool cutMoves = curCut1 != tgtCut1 || curCut2 != tgtCut2;
    auto runFilter = [&](MultiModeFilter& flt, juce::AudioBuffer<float>& buf, float from, float to)
    {
        if (from == to) { flt.setCutoff(to); flt.process(buf, ns, 1); return; }
        float* const chan[1] = { buf.getWritePointer(0) };
        for (int start = 0; start < ns; start += kRampChunk)
        {
            const int len = juce::jmin(kRampChunk, ns - start);
            const float f = (float) (start + len) / (float) ns;
            flt.setCutoff(from * std::pow(to / from, f));
            juce::AudioBuffer<float> view(chan, 1, start, len);   // a window onto the same samples: no allocation
            flt.process(view, len, 1);
        }
    };
    if (cfg.filterSeries)
    {
        // Series: signal passes through filter1 then filter2.
        runFilter(filter1, mono, curCut1, tgtCut1);
        runFilter(filter2, mono, curCut2, tgtCut2);
    }
    else
    {
        // Parallel: both filters see the same input; outputs are averaged.
        mono2.copyFrom(0, 0, mono, 0, 0, ns);
        runFilter(filter1, mono,  curCut1, tgtCut1);
        runFilter(filter2, mono2, curCut2, tgtCut2);
        // Mix: 0.5× each so combined level matches a single filter.
        auto* a = mono .getWritePointer(0);
        const auto* b = mono2.getReadPointer(0);
        for (int i = 0; i < ns; ++i) a[i] = (a[i] + b[i]) * 0.5f;
    }
    if (cutMoves) { curCut1 = tgtCut1; curCut2 = tgtCut2; }

    // Sum the mono voice into every output channel.
    for (int ch = 0; ch < out.getNumChannels(); ++ch)
        out.addFrom(ch, 0, mono, 0, 0, ns);
}

} // namespace mu_tant
