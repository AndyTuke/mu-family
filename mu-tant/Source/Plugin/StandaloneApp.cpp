// Standalone app for mu-Tant — the window, close prompt, and mu-link bridge are the shared
// mu-core standalone shell (mu_standalone::App), so they're identical across the family.
// Activated by JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP=1 on the Standalone target.

#include "PluginProcessor.h"            // product TU context (JucePlugin_* macros, createPluginFilter)
#include "Plugin/StandaloneShell.h"      // mu-core: shared standalone window + app

class MuTantApp : public mu_standalone::App
{
public:
    MuTantApp() : mu_standalone::App ({ juce::String (juce::CharPointer_UTF8 ("\xce\xbc-Tant")), "mu-Tant" }) {}
};

juce::JUCEApplicationBase* juce_CreateApplication()
{
    return new MuTantApp();
}
