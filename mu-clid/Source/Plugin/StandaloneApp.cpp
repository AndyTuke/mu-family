// Standalone app for mu-Clid — the window, close prompt, mu-link bridge and headless `--render`
// mode are the shared mu-core standalone shell (mu_standalone::App). mu-Clid adds only the Lite
// display name.
// Activated by JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP=1 on the Standalone target. PluginProcessor.h
// is included first so the JUCE module headers are in scope before the standalone shell.

#include "PluginProcessor.h"
#include "Plugin/StandaloneShell.h"   // mu-core: shared standalone window + app

class MuClidApp : public mu_standalone::App
{
public:
    MuClidApp() : mu_standalone::App ({ displayName(), "mu-Clid" }) {}

private:
    // The μ (U+03BC) is hard-coded as UTF-8 bytes for the title bar — the JucePlugin_Name
    // macro mangles it in the CMake `-D` round-trip.
    static juce::String displayName()
    {
       #if MUCLID_LITE_BUILD
        return juce::String (juce::CharPointer_UTF8 ("\xce\xbc-Clid Lite"));
       #else
        return juce::String (juce::CharPointer_UTF8 ("\xce\xbc-Clid"));
       #endif
    }
};

//==============================================================================
juce::JUCEApplicationBase* juce_CreateApplication()
{
    return new MuClidApp();
}
