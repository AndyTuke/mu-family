namespace mu_tronic
{
PluginProcessor::PluginProcessor() { setLayerAccess(mu_core::LayerAccess::Published); }

void PluginProcessor::processBlock(juce::AudioBuffer<float>& b, juce::MidiBuffer& m)
{
    renderAndMix(b, m, t);
    const double loop = layers[0]->loopBeats();
}

void PluginProcessor::loadLayer(int slot)
{
    layerAs<Empty>(slot).reset();   // layer-ok: message thread, under the writers lock
}
}
