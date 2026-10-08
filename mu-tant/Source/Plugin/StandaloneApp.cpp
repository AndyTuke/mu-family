// Standalone app for mu-Tant — the window, close prompt, mu-link bridge and headless `--render`
// mode are the shared mu-core standalone shell (mu_standalone::App), identical across the family.
// Activated by JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP=1 on the Standalone target.

#include "PluginProcessor.h"            // product TU context (JucePlugin_* macros, createPluginFilter)
#include "Plugin/StandaloneShell.h"      // mu-core: shared standalone window + app

class MuTantApp : public mu_standalone::App
{
public:
    MuTantApp() : mu_standalone::App ({ juce::String (juce::CharPointer_UTF8 ("\xce\xbc-Tant")), "mu-Tant" }) {}

protected:
    // With no --preset, the render is the family's guaranteed-audio smoke: bypass voice 0's gate so
    // the raw oscillator drone passes with the transport stopped (no preset / sample dependency).
    void prepareRender (ProcessorBase& proc, const mu_core::render_mode::ProductArgs& args) override
    {
        if (args.presetFile == juce::File{})
            if (auto* p = proc.apvts.getParameter ("v0_gate_bypass"))
                p->setValueNotifyingHost (1.0f);
    }

    // A preset render plays its gate patterns; the drone smoke stays stopped.
    bool renderPlaysByDefault (const mu_core::render_mode::ProductArgs& args) const override
    {
        return args.presetFile != juce::File{};
    }
};

juce::JUCEApplicationBase* juce_CreateApplication()
{
    return new MuTantApp();
}
