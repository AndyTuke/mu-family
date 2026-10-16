namespace mu_on
{
// numLayers.store(1) in a comment is fine; the call below is not.
PluginProcessor::PluginProcessor() { numLayers.store(1, std::memory_order_relaxed); }
}
