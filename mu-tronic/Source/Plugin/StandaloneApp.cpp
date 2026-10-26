// Standalone app for mu-Tronic — the window, close prompt, and mu-link bridge are the shared mu-core standalone
// shell (mu_standalone::App), identical across the family. Passing the mu-link name ("mu-Tronic") wires the
// bridge, so an attached mu-link master clock drives the layers and the audio goes to its bus; mu-Tronic is a
// Standalone-only product, so the bridge (a standalone-only header) is the only place it can attach.
// Activated by JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP=1.

#include "PluginProcessor.h"            // product TU context (JucePlugin_* macros, createPluginFilter)
#include "Plugin/StandaloneShell.h"      // mu-core: shared standalone window + app

class MuTronicApp : public mu_standalone::App
{
public:
    MuTronicApp() : mu_standalone::App ({ juce::String (juce::CharPointer_UTF8 ("\xce\xbc-Tronic")), "mu-Tronic" }) {}
};

juce::JUCEApplicationBase* juce_CreateApplication()
{
    return new MuTronicApp();
}
