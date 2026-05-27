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
}

juce::AudioProcessorValueTreeState::ParameterLayout PluginProcessor::createParameterLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;
    auto f = [](float lo, float hi, float step) { return NormalisableRange<float>(lo, hi, step); };

    // Shared tonal centre.
    layout.add(std::make_unique<AudioParameterChoice>(ParameterID{"root", 1},  "Root",  rootNames(),  0));
    layout.add(std::make_unique<AudioParameterChoice>(ParameterID{"scale", 1}, "Scale", scaleNames(), 0));

    // Per-oscillator pitch + wavetable position.
    layout.add(std::make_unique<AudioParameterInt>  (ParameterID{"o1_oct", 1},  "Osc1 Octave", 0, 8, 4));
    layout.add(std::make_unique<AudioParameterFloat>(ParameterID{"o1_tone", 1}, "Osc1 Tone",   f(0.0f, 14.0f, 0.01f), 0.0f));
    layout.add(std::make_unique<AudioParameterFloat>(ParameterID{"o1_fine", 1}, "Osc1 Fine",   f(-100.0f, 100.0f, 0.1f), 0.0f));
    layout.add(std::make_unique<AudioParameterFloat>(ParameterID{"o1_pos", 1},  "Osc1 Position", f(0.0f, 1.0f, 0.001f), 0.0f));
    layout.add(std::make_unique<AudioParameterInt>  (ParameterID{"o2_oct", 1},  "Osc2 Octave", 0, 8, 3));
    layout.add(std::make_unique<AudioParameterFloat>(ParameterID{"o2_tone", 1}, "Osc2 Tone",   f(0.0f, 14.0f, 0.01f), 2.0f));
    layout.add(std::make_unique<AudioParameterFloat>(ParameterID{"o2_fine", 1}, "Osc2 Fine",   f(-100.0f, 100.0f, 0.1f), 0.0f));
    layout.add(std::make_unique<AudioParameterFloat>(ParameterID{"o2_pos", 1},  "Osc2 Position", f(0.0f, 1.0f, 0.001f), 0.0f));

    // Cross-mod + balance.
    layout.add(std::make_unique<AudioParameterFloat> (ParameterID{"xmod", 1},  "X-Mod",      f(0.0f, 1.0f, 0.001f), 0.0f));
    layout.add(std::make_unique<AudioParameterChoice>(ParameterID{"xmode", 1}, "X-Mod Mode", StringArray{ "Off", "FM", "Sync" }, 0));
    layout.add(std::make_unique<AudioParameterFloat> (ParameterID{"mix", 1},   "Osc Mix",    f(0.0f, 1.0f, 0.001f), 0.5f));

    // Filter (mu-core).
    NormalisableRange<float> cutoff(20.0f, 20000.0f, 1.0f);
    cutoff.setSkewForCentre(640.0f);
    layout.add(std::make_unique<AudioParameterInt>  (ParameterID{"flt_type", 1}, "Filter Type", 0, 15, 0));
    layout.add(std::make_unique<AudioParameterFloat>(ParameterID{"flt_cut", 1},  "Cutoff", cutoff, 8000.0f));
    layout.add(std::make_unique<AudioParameterFloat>(ParameterID{"flt_res", 1},  "Resonance", f(0.0f, 0.99f, 0.001f), 0.2f));

    // Slot level.
    layout.add(std::make_unique<AudioParameterFloat>(ParameterID{"level", 1}, "Level", f(-60.0f, 6.0f, 0.1f), -6.0f));

    return layout;
}

PluginProcessor::PluginProcessor()
    : ProcessorBase(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true),
                    createParameterLayout(),
                    juce::Identifier("MuTantState"))
{
    bank.generateBuiltIn();      // procedural sine->saw morph table (first stab)
    voice.setBank(&bank);
}

void PluginProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    voice.prepare(sampleRate, samplesPerBlock);
}

VoiceConfig PluginProcessor::readConfig() const
{
    auto raw = [this](const char* id) { return apvts.getRawParameterValue(id)->load(); };
    VoiceConfig c;
    c.root         = (int) raw("root");
    c.scaleIdx     = (int) raw("scale");
    c.osc1Octave   = (int) raw("o1_oct");
    c.osc1Tone     = raw("o1_tone");
    c.osc1Fine     = raw("o1_fine");
    c.osc1Pos      = raw("o1_pos");
    c.osc2Octave   = (int) raw("o2_oct");
    c.osc2Tone     = raw("o2_tone");
    c.osc2Fine     = raw("o2_fine");
    c.osc2Pos      = raw("o2_pos");
    c.xmod         = raw("xmod");
    c.xmodMode     = (int) raw("xmode");
    c.mix          = raw("mix");
    c.filterType   = (int) raw("flt_type");
    c.filterCutoff = raw("flt_cut");
    c.filterRes    = raw("flt_res");
    c.levelDb      = raw("level");
    return c;
}

void PluginProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();

    voice.setConfig(readConfig());
    voice.process(buffer, buffer.getNumSamples());
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
