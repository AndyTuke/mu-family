// Standalone app for mu-On — the window, close prompt, and mu-link bridge are the shared
// mu-core standalone shell (mu_standalone::App), identical across the family. Passing the
// mu-link name ("mu-On") wires the bridge so an attached mu-link master clock drives the
// sequencer. Activated by JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP=1.

#include "PluginProcessor.h"            // product TU context (JucePlugin_* macros, createPluginFilter)
#include "Plugin/StandaloneShell.h"      // mu-core: shared standalone window + app

class MuOnApp : public mu_standalone::App
{
public:
    MuOnApp() : mu_standalone::App ({ juce::String (juce::CharPointer_UTF8 ("\xce\xbc-On")), "mu-On" }) {}
};

juce::JUCEApplicationBase* juce_CreateApplication()
{
    return new MuOnApp();
}
