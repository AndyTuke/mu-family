// Standalone app for mu-On — the window + close prompt are the shared mu-core standalone
// shell (mu_standalone::App), identical across the family. mu-On isn't wired to mu-link yet,
// so the mu-link name is empty (no bridge). Activated by JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP=1.

#include "PluginProcessor.h"            // product TU context (JucePlugin_* macros, createPluginFilter)
#include "Plugin/StandaloneShell.h"      // mu-core: shared standalone window + app

class MuOnApp : public mu_standalone::App
{
public:
    MuOnApp() : mu_standalone::App ({ juce::String (juce::CharPointer_UTF8 ("\xce\xbc-On")), {} }) {}
};

juce::JUCEApplicationBase* juce_CreateApplication()
{
    return new MuOnApp();
}
