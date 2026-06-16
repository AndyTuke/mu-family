#include "SynthVoice.h"
#include "Scales.h"

namespace mu_tant
{

// X-Mod index scaling (mu-tant-xmod-design.md):
//   PM: index 1.0 → up to 2 cycles (~4π rad) of phase displacement — "DX-style" depth.
//   FM/TZFM: index 1.0 → up to an 8× frequency-modulation ratio.
//   Feedback: a conservative fixed phase depth (feedback FM turns chaotic fast).
static constexpr float kPmScale = 2.0f;
static constexpr float kFmRatio = 8.0f;
static constexpr float kFbScale = 0.3f;
// One-pole smoothing time for the continuous X-Mod controls (index / depth / SSB shift),
// so sweeping a knob — or a per-block mode change — ramps instead of zippering/clicking.
static constexpr float kXModSmoothMs = 5.0f;

void VoiceEngine::prepare(double sampleRate, int blockSize)
{
    sr = sampleRate > 0 ? sampleRate : 44100.0;
    osc1.prepare(sr);
    osc2.prepare(sr);
    filter1.prepare(sr, blockSize, 1);
    filter1.reset();
    filter2.prepare(sr, blockSize, 1);
    filter2.reset();
    hilbert.reset();
    lastA    = 0.0f;
    ssbPhase = 0.0;
    indexSm  = depthSm = ssbHzSm = 0.0f;
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
                                   (float) cfg.osc1Semi, (float) cfg.osc1Fine)
                        + cfg.pitchOffsetSemis + cfg.osc1SemiMod;
    const float midi2 = toneToMidi(cfg.scaleIdx, cfg.root, cfg.osc2Octave + kBaseOctave,
                                   (float) cfg.osc2Semi, (float) cfg.osc2Fine)
                        + cfg.pitchOffsetSemis + cfg.osc2SemiMod;
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

    // 2-lane X-Mod (mu-tant-xmod-design.md). Lane A (phase/index) and Lane B
    // (amplitude/multiply) run in parallel; the mode within each lane is mutually
    // exclusive. Continuous controls smooth per-sample toward their target.
    const int   phaseMode = cfg.xmodPhaseMode;        // 0 FM, 1 PM, 2 TZFM
    const bool  fdbk      = cfg.xmodFeedback;
    const int   ampMode   = cfg.xmodAmpMode;          // 0 Mult, 1 SSB
    const float idxTgt    = juce::jlimit(0.0f, 1.0f, cfg.xmodIndex);
    const float depTgt    = juce::jlimit(-1.0f, 1.0f, cfg.xmodDepth);
    const float ssbTgt    = cfg.xmodSsbHz;
    const float smCoef    = 1.0f - std::exp(-1.0f / (kXModSmoothMs * 0.001f * (float) sr));

    for (int i = 0; i < ns; ++i)
    {
        indexSm += (idxTgt - indexSm) * smCoef;
        depthSm += (depTgt - depthSm) * smCoef;
        ssbHzSm += (ssbTgt - ssbHzSm) * smCoef;

        // Modulator (osc2) renders first; feedback FM phase-mods it with osc1's prev output.
        const float b = osc2.render(fdbk ? kFbScale * lastA : 0.0f);

        // Lane A — carrier (osc1) phase/index bus.
        float a;
        if (phaseMode == 1)                          // PM: displace osc1's read phase
        {
            a = osc1.render(indexSm * b * kPmScale);
        }
        else                                         // FM / TZFM: scale osc1's increment
        {
            double incMul = 1.0 + (double) (indexSm * kFmRatio) * b;
            if (phaseMode == 0) incMul = juce::jmax(0.0, incMul);   // FM clamps ≥ 0 (no through-zero)
            a = osc1.render(0.0f, incMul);
        }

        // Hard sync: osc1 wrap resets osc2 phase (takes effect next sample).
        if (cfg.sync && osc1.justWrapped())
            osc2.resetPhase();

        lastA = a;                                   // feedback tap = raw carrier output

        // Lane B — amplitude/multiply bus (mode: 0 AM, 1 RM, 2 SSB).
        if (ampMode == 2)
        {
            // SSB / frequency shift: shift every partial of the carrier by ssbHz. Take the
            // analytic signal (Hilbert) and multiply by a complex exponential, keep the real
            // part → one sideband. Sign of the shift sets up/down.
            const auto  q   = hilbert.process(a);
            const float ang = (float) (ssbPhase * juce::MathConstants<double>::twoPi);
            a = q.re * std::cos(ang) - q.im * std::sin(ang);
            ssbPhase += (double) ssbHzSm / sr;
            ssbPhase -= std::floor(ssbPhase);
        }
        else if (depthSm != 0.0f)
        {
            // AM keeps the carrier (classic amplitude mod); RM crossfades dry → ring so the
            // carrier is suppressed at full depth. Depth is bipolar (centre = off; sign flips
            // the modulator phase).
            if (ampMode == 0)                       // AM
            {
                a *= 1.0f + depthSm * b;
            }
            else                                    // RM
            {
                const float k   = std::abs(depthSm);
                const float sgn = depthSm < 0.0f ? -1.0f : 1.0f;
                a = a * (1.0f - k) + sgn * (a * b) * k;
            }
        }

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
