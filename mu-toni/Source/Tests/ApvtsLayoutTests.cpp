// Shared global-FX APVTS layout coverage for mu-toni's mixer/FX rack.
//
// mu-toni's mixer binds to the params declared by mu_mixfx::addGlobalFxParams
// (mu-core) and synced via ProcessorBase::syncGlobalFxParam. This builds an APVTS
// from that helper (behind a minimal headless AudioProcessor) and asserts the
// exact ID set — so drift in the shared layout is caught here. Like mu-clid- and
// mu-tant-tests, it does NOT construct the full PluginProcessor (its createEditor()
// drags the editor/UI tree into a console app — see backlog #721).

#include <juce_audio_processors/juce_audio_processors.h>
#include "Plugin/MixerFxParams.h"   // mu-core: mu_mixfx::addGlobalFxParams

namespace
{
class StubProcessor : public juce::AudioProcessor
{
public:
    StubProcessor() = default;
    const juce::String getName() const override          { return "stub"; }
    void prepareToPlay (double, int) override             {}
    void releaseResources() override                      {}
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override {}
    juce::AudioProcessorEditor* createEditor() override   { return nullptr; }
    bool hasEditor() const override                       { return false; }
    bool acceptsMidi() const override                     { return false; }
    bool producesMidi() const override                    { return false; }
    double getTailLengthSeconds() const override          { return 0.0; }
    int getNumPrograms() override                         { return 1; }
    int getCurrentProgram() override                      { return 0; }
    void setCurrentProgram (int) override                 {}
    const juce::String getProgramName (int) override      { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override {}
    void setStateInformation (const void*, int) override  {}
};
} // namespace

class MuToniApvtsLayoutTest : public juce::UnitTest
{
public:
    MuToniApvtsLayoutTest() : juce::UnitTest ("Shared global-FX APVTS layout", "Mixer") {}

    void runTest() override
    {
        StubProcessor proc;
        juce::AudioProcessorValueTreeState::ParameterLayout layout;
        mu_mixfx::addGlobalFxParams (layout);
        juce::AudioProcessorValueTreeState apvts (proc, nullptr, "PARAMS", std::move (layout));

        beginTest ("Core global-FX / return / master params exist");
        {
            static const char* expected[] = {
                "eff_algo", "eff_en", "eff_p0",
                "dly_en", "dly_ms", "dly_fb",
                "rev_algo", "rev_en", "rev_lvl",
                "eff2dly", "eff2rev", "dly2rev",
                "echo_en", "echo_fb",
                "ret_eff_lvl", "ret_dly_lvl", "ret_rev_lvl",
                "mstr_lvl", "mstr_pan", "mst_insChar", "mst_ins2Char",
            };
            for (const char* id : expected)
                expect (apvts.getParameter (id) != nullptr,
                        juce::String ("missing global-FX param '") + id + "'");
        }

        beginTest ("Dead legacy sends stay removed (#724)");
        {
            expect (apvts.getParameter ("eff_send") == nullptr, "eff_send must not exist");
            expect (apvts.getParameter ("dly_send") == nullptr, "dly_send must not exist");
        }
    }
};

static MuToniApvtsLayoutTest muToniApvtsLayoutTest;
