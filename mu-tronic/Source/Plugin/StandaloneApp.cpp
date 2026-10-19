// Standalone app for mu-Tronic — the window, close prompt, and mu-link bridge are the shared mu-core standalone
// shell (mu_standalone::App), identical across the family. Activated by JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP=1.

#include "PluginProcessor.h"            // product TU context (JucePlugin_* macros, createPluginFilter)
#include "Plugin/StandaloneShell.h"      // mu-core: shared standalone window + app

class MuTronicApp : public mu_standalone::App
{
public:
    MuTronicApp() : mu_standalone::App ({ juce::String (juce::CharPointer_UTF8 ("\xce\xbc-Tronic")), {} }) {}   // no mu-link bridge yet (the bridge is a later stage)
};

juce::JUCEApplicationBase* juce_CreateApplication()
{
    return new MuTronicApp();
}
