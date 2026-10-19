#include "PluginProcessor.h"

namespace mu_toni
{
// renderVoice used to live here; the call below is fine, only a definition is glue.
void PluginProcessor::processBlock(juce::AudioBuffer<float>& b, juce::MidiBuffer& m)
{
    renderAndMix(b, m, t);
    if (renderLayersReady(b)) { flag = true; }
}

void PluginProcessor::renderVoice(int v, juce::AudioBuffer<float>& buf,
                                  int numSamples)
{
}
}
