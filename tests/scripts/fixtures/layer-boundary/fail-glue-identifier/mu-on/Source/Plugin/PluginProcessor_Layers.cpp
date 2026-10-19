// voiceEngines in a comment is fine
namespace mu_on
{
void PluginProcessor::commitDeferredWork()
{
    for (auto& e : voiceEngines) e.reset();
}
}
