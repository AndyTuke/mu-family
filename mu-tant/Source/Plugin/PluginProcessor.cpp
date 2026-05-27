#include "Plugin/PluginProcessor.h"
#include "Plugin/PluginEditor.h"
#include "Audio/Scales.h"

namespace mu_tant
{

namespace
{
    juce::StringArray rootNames()
    {
        return { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    }
    juce::StringArray scaleNames()
    {
        juce::StringArray a;
        for (const auto& s : kScales) a.add(s.name);
        return a;
    }

    // Build the parameter family for a single voice. Called for v0..v7 so each
    // voice gets its own independent osc/xmod/filter/level state in the APVTS.
    void addVoiceParams(juce::AudioProcessorValueTreeState::ParameterLayout& layout,
                        int voice)
    {
        using namespace juce;
        auto f = [](float lo, float hi, float step) { return NormalisableRange<float>(lo, hi, step); };
        auto id = [voice](const char* base) {
            return PluginProcessor::voiceParamId(voice, base);
        };
        auto label = [voice](const char* base) {
            return juce::String("V") + juce::String(voice + 1) + " " + base;
        };

        // Per-oscillator pitch + wavetable position.
        layout.add(std::make_unique<AudioParameterInt>  (ParameterID{id("o1_oct"), 1},  label("Osc1 Octave"), 0, 8, 4));
        layout.add(std::make_unique<AudioParameterFloat>(ParameterID{id("o1_tone"), 1}, label("Osc1 Tone"),   f(0.0f, 14.0f, 0.01f), 0.0f));
        layout.add(std::make_unique<AudioParameterFloat>(ParameterID{id("o1_fine"), 1}, label("Osc1 Fine"),   f(-100.0f, 100.0f, 0.1f), 0.0f));
        layout.add(std::make_unique<AudioParameterFloat>(ParameterID{id("o1_pos"), 1},  label("Osc1 Position"), f(0.0f, 1.0f, 0.001f), 0.0f));
        layout.add(std::make_unique<AudioParameterInt>  (ParameterID{id("o2_oct"), 1},  label("Osc2 Octave"), 0, 8, 3));
        layout.add(std::make_unique<AudioParameterFloat>(ParameterID{id("o2_tone"), 1}, label("Osc2 Tone"),   f(0.0f, 14.0f, 0.01f), 2.0f));
        layout.add(std::make_unique<AudioParameterFloat>(ParameterID{id("o2_fine"), 1}, label("Osc2 Fine"),   f(-100.0f, 100.0f, 0.1f), 0.0f));
        layout.add(std::make_unique<AudioParameterFloat>(ParameterID{id("o2_pos"), 1},  label("Osc2 Position"), f(0.0f, 1.0f, 0.001f), 0.0f));

        // Cross-mod + balance.
        layout.add(std::make_unique<AudioParameterFloat> (ParameterID{id("xmod"), 1},  label("X-Mod"),      f(0.0f, 1.0f, 0.001f), 0.0f));
        layout.add(std::make_unique<AudioParameterChoice>(ParameterID{id("xmode"), 1}, label("X-Mod Mode"), StringArray{ "Off", "FM", "Sync" }, 0));
        layout.add(std::make_unique<AudioParameterFloat> (ParameterID{id("mix"), 1},   label("Osc Mix"),    f(0.0f, 1.0f, 0.001f), 0.5f));

        // Filter (mu-core).
        NormalisableRange<float> cutoff(20.0f, 20000.0f, 1.0f);
        cutoff.setSkewForCentre(640.0f);
        layout.add(std::make_unique<AudioParameterInt>  (ParameterID{id("flt_type"), 1}, label("Filter Type"), 0, 15, 0));
        layout.add(std::make_unique<AudioParameterFloat>(ParameterID{id("flt_cut"), 1},  label("Cutoff"), cutoff, 8000.0f));
        layout.add(std::make_unique<AudioParameterFloat>(ParameterID{id("flt_res"), 1},  label("Resonance"), f(0.0f, 0.99f, 0.001f), 0.2f));

        // Per-voice level — distinct from the mixer fader (this is engine-level
        // trim before the channel strip; the mixer adds its own per-channel level
        // / pan / mute / solo on top, matching the mu-clid signal flow).
        layout.add(std::make_unique<AudioParameterFloat>(ParameterID{id("level"), 1}, label("Level"), f(-60.0f, 6.0f, 0.1f), -6.0f));
    }
}

juce::AudioProcessorValueTreeState::ParameterLayout PluginProcessor::createParameterLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    // Shared tonal centre.
    layout.add(std::make_unique<AudioParameterChoice>(ParameterID{"root",  1}, "Root",  rootNames(),  0));
    layout.add(std::make_unique<AudioParameterChoice>(ParameterID{"scale", 1}, "Scale", scaleNames(), 0));

    for (int v = 0; v < kMaxVoices; ++v)
        addVoiceParams(layout, v);

    return layout;
}

PluginProcessor::PluginProcessor()
    : ProcessorBase(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true),
                    createParameterLayout(),
                    juce::Identifier("MuTantState"))
{
    bank.generateBuiltIn();      // procedural sine->saw morph table (first stab)
    for (int v = 0; v < kMaxVoices; ++v)
    {
        voices[(size_t) v] = std::make_unique<VoiceEngine>();
        voices[(size_t) v]->setBank(&bank);
    }
}

void PluginProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    for (auto& v : voices)
        if (v) v->prepare(sampleRate, samplesPerBlock);

    // Pre-allocate per-voice render scratch so processBlock never allocates.
    for (auto& buf : voiceBuffers)
        buf.setSize(2, samplesPerBlock, false, true, true);

    mixerEngine.prepare(sampleRate, samplesPerBlock);
}

VoiceConfig PluginProcessor::readConfig(int voiceIdx) const
{
    auto raw = [this](const juce::String& id) {
        return apvts.getRawParameterValue(id)->load();
    };
    auto vid = [voiceIdx](const char* base) {
        return voiceParamId(voiceIdx, base);
    };

    VoiceConfig c;
    // Tonal centre is global.
    c.root         = (int) raw("root");
    c.scaleIdx     = (int) raw("scale");
    // Per-voice osc + cross-mod + filter + level.
    c.osc1Octave   = (int) raw(vid("o1_oct"));
    c.osc1Tone     = raw(vid("o1_tone"));
    c.osc1Fine     = raw(vid("o1_fine"));
    c.osc1Pos      = raw(vid("o1_pos"));
    c.osc2Octave   = (int) raw(vid("o2_oct"));
    c.osc2Tone     = raw(vid("o2_tone"));
    c.osc2Fine     = raw(vid("o2_fine"));
    c.osc2Pos      = raw(vid("o2_pos"));
    c.xmod         = raw(vid("xmod"));
    c.xmodMode     = (int) raw(vid("xmode"));
    c.mix          = raw(vid("mix"));
    c.filterType   = (int) raw(vid("flt_type"));
    c.filterCutoff = raw(vid("flt_cut"));
    c.filterRes    = raw(vid("flt_res"));
    c.levelDb      = raw(vid("level"));
    return c;
}

void PluginProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();
    buffer.clear();

    // Pre-resolve mixer state — soloing wins over muting, matching the
    // mu-clid mixer convention.
    bool anySolo = false;
    for (int v = 0; v < kMaxVoices; ++v)
        if (mixerEngine.channels[(size_t) v].solo) { anySolo = true; break; }

    for (int v = 0; v < kMaxVoices; ++v)
    {
        const auto& ch = mixerEngine.channels[(size_t) v];
        const bool audible = anySolo ? ch.solo : !ch.mute;
        if (!audible) continue;

        auto& voiceBuf = voiceBuffers[(size_t) v];
        voiceBuf.clear();
        voices[(size_t) v]->setConfig(readConfig(v));
        voices[(size_t) v]->process(voiceBuf, numSamples);

        // Equal-power constant-power pan, matching MixerEngine::applyPanGain.
        const float pan   = juce::jlimit(-1.0f, 1.0f, ch.pan);
        const float angle = (pan + 1.0f) * 0.25f * juce::MathConstants<float>::pi;
        const float gainL = ch.level * std::cos(angle);
        const float gainR = ch.level * std::sin(angle);

        buffer.addFrom(0, 0, voiceBuf, 0, 0, numSamples, gainL);
        buffer.addFrom(1, 0, voiceBuf, 1, 0, numSamples, gainR);
    }

    // Master fader — apply unconditionally (no master mute concept).
    buffer.applyGain(mixerEngine.masterLevel);
}

juce::AudioProcessorEditor* PluginProcessor::createEditor()
{
    return new PluginEditor(*this);
}

void PluginProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary(*xml, destData);
}

void PluginProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
        apvts.replaceState(juce::ValueTree::fromXml(*xml));
}

} // namespace mu_tant

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new mu_tant::PluginProcessor();
}
