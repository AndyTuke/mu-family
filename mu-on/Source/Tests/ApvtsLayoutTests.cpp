// Shared global-FX APVTS layout coverage for mu-on's mixer/FX rack.
//
// mu-on's mixer binds to the params declared by mu_mixfx::addGlobalFxParams (mu-core)
// and synced via ProcessorBase::syncGlobalFxParam. This builds an APVTS from that helper
// behind a minimal headless AudioProcessor and asserts the ID set - so drift in the
// shared layout is caught here. Like the rest of the family it does NOT construct the
// full PluginProcessor (its createEditor() drags the editor/UI tree in - see the backlog).

#include <juce_audio_processors/juce_audio_processors.h>
#include "Plugin/MixerFxParams.h"   // mu-core: mu_mixfx::addGlobalFxParams
#include "Persistence/TrackParams.h"   // the Track type's per-slot table

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

class MuOnApvtsLayoutTest : public juce::UnitTest
{
public:
    MuOnApvtsLayoutTest() : juce::UnitTest ("Shared global-FX APVTS layout", "Mixer") {}

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
    }
};

static MuOnApvtsLayoutTest muOnApvtsLayoutTest;

// The Track type's table for one slot holds exactly the engine row, the Rumble source row, every engine's
// parameters and the track's own groove rows (swing, accent) under the slot's prefix: a row added to one engine's name list and not the table (or the other
// way round) would leave a knob with no parameter, or a parameter nothing shows.
class MuOnTrackParamTableTest : public juce::UnitTest
{
public:
    MuOnTrackParamTableTest() : juce::UnitTest ("Track layer parameter table", "Mixer") {}

    void runTest() override
    {
        beginTest ("addLayerParams adds exactly eng, src, swing, accent and every engine's parameters of one slot, under its prefix");
        StubProcessor proc;
        juce::AudioProcessorValueTreeState::ParameterLayout layout;
        mu_on::addLayerParams (layout, 3);
        juce::AudioProcessorValueTreeState apvts (proc, nullptr, "PARAMS", std::move (layout));

        int expected = 4 + 1 + 1 + 5;   // eng, src, swing, accent, prob, level, and the insert (algorithm + four knobs)
        expect (apvts.getParameter ("t3_eng") != nullptr && apvts.getParameter ("t3_src") != nullptr, "the engine and source rows");
        expect (apvts.getParameter ("t3_swing") != nullptr && apvts.getParameter ("t3_accent") != nullptr, "the groove rows");
        for (int e = 0; e < mu_on::kNumEngineTypes; ++e)
            for (const char* x : mu_on::engineParamNames (mu_on::engineFromIndex (e)))
            {
                ++expected;
                const auto id = mu_on::engineParamId (3, mu_on::engineFromIndex (e), x);
                expect (apvts.getParameter (id) != nullptr, "no row for " + id);
            }
        expectEquals (proc.getParameters().size(), expected);

        beginTest ("the groove rows are per track: swing 0..1 default 0, accent 0..1 default 1, and no global row is left");
        {
            auto swing  = apvts.getParameter ("t3_swing");
            auto accent = apvts.getParameter ("t3_accent");
            expectWithinAbsoluteError (swing->convertFrom0to1 (swing->getDefaultValue()), 0.0f, 1.0e-6f);
            expectWithinAbsoluteError (accent->convertFrom0to1 (accent->getDefaultValue()), 1.0f, 1.0e-6f);
            expectWithinAbsoluteError (swing->convertFrom0to1 (1.0f), 1.0f, 1.0e-6f);
            expectWithinAbsoluteError (accent->convertFrom0to1 (1.0f), 1.0f, 1.0e-6f);
            expect (apvts.getParameter ("seq_swing") == nullptr && apvts.getParameter ("seq_accent") == nullptr, "no global groove row");
        }
    }
};

static MuOnTrackParamTableTest muOnTrackParamTableTest;
