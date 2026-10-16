namespace mu_tant
{
// TryLock mode (the default): layers[] reads under the products own try-lock stay legal until it switches.
void PluginProcessor::processBlock(juce::AudioBuffer<float>& b, juce::MidiBuffer& m)
{
    auto& p = layerAs<Pattern>(0);
    const auto* l = layers[1].get();
}
}
