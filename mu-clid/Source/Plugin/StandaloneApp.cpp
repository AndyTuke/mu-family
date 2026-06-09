// Standalone app for mu-Clid — the window, close prompt, and mu-link bridge are the shared
// mu-core standalone shell (mu_standalone::App). mu-Clid adds two product-specific bits: the
// `--render` headless mode (listening-test pipeline) and the Lite display name.
// Activated by JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP=1 on the Standalone target. PluginProcessor.h
// is included first so the JUCE module headers are in scope before the standalone shell.

#include "PluginProcessor.h"
#include "Plugin/RenderMode.h"
#include "Plugin/StandaloneShell.h"   // mu-core: shared standalone window + app

class MuClidApp : public mu_standalone::App
{
public:
    MuClidApp() : mu_standalone::App ({ displayName(), "mu-Clid" }) {}

    void initialise (const juce::String& commandLine) override
    {
        // Headless render mode for the listening-test pipeline. `--render <out.wav> --seconds N`
        // skips GUI startup, runs the offline render, then quits. Anything else falls through
        // to the shared standalone path.
        const auto renderArgs = mu_clid::render_mode::parse (commandLine);
        if (renderArgs.valid)
        {
            setApplicationReturnValue (mu_clid::render_mode::execute (renderArgs));
            quit();
            return;
        }
        mu_standalone::App::initialise (commandLine);   // shared window + mu-link bridge
    }

private:
    // The μ (U+03BC) is hard-coded as UTF-8 bytes for the title bar — the JucePlugin_Name
    // macro mangles it in the CMake `-D` round-trip (see #421).
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
