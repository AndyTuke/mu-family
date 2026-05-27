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

void VoiceEngine::setConfig(const VoiceConfig& c)
{
    cfg = c;

    const float midi1 = toneToMidi(cfg.scaleIdx, cfg.root, cfg.osc1Octave, cfg.osc1Tone, cfg.osc1Fine);
    const float midi2 = toneToMidi(cfg.scaleIdx, cfg.root, cfg.osc2Octave, cfg.osc2Tone, cfg.osc2Fine);
    osc1.setFrequency(midiToFreq(midi1));
    osc2.setFrequency(midiToFreq(midi2));
    osc1.setPosition(cfg.osc1Pos);
    osc2.setPosition(cfg.osc2Pos);

    filter.setType(cfg.filterType);
    filter.setCutoff(cfg.filterCutoff);
    filter.setResonance(cfg.filterRes);

    gain = juce::Decibels::decibelsToGain(cfg.levelDb, -60.0f);
}

void VoiceEngine::process(juce::AudioBuffer<float>& out, int numSamples)
{
    const int ns = juce::jmin(numSamples, mono.getNumSamples());
    if (ns <= 0) return;

    float* m = mono.getWritePointer(0);
    const float mixB = cfg.mix;
    const float mixA = 1.0f - cfg.mix;

    for (int i = 0; i < ns; ++i)
    {
        float a, b;
        if (cfg.xmodMode == Sync)
        {
            // A -> B: B's phase is forced to 0 each time A wraps.
            a = osc1.render();
            if (osc1.justWrapped()) osc2.resetPhase();
            b = osc2.render();
        }
        else
        {
            // Off / FM. FM is B -> A: B modulates A's phase.
            b = osc2.render();
            const float pm = (cfg.xmodMode == FM) ? (cfg.xmod * b * kFmDepth) : 0.0f;
            a = osc1.render(pm);
        }
        m[i] = (a * mixA + b * mixB) * gain;
    }

    // Filter (mu-core) in place on the mono work buffer.
    filter.process(mono, ns, 1);

    // [gate stub] — drawable-gate pattern multiply goes here (design-sequencer.md).
    // [insert stub] — mu-core InsertProcessor goes here.

    // Sum the mono voice into every output channel.
    for (int ch = 0; ch < out.getNumChannels(); ++ch)
        out.addFrom(ch, 0, mono, 0, 0, ns);
}

} // namespace mu_tant
