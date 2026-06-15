// Processor-level APVTS layout coverage - the real mu-tant createParameterLayout.
//
// Builds an APVTS from mu_tant::PluginProcessor::createParameterLayout() (the
// actual factory, compiled via the editor-free PluginProcessor_APVTS.cpp TU)
// behind a minimal headless AudioProcessor, then asserts the full ID set:
//   - per-voice  v{0..7}_*   (osc / xmod / levels / filter / gate / insert)
//   - per-channel ch{0..7}_* (mixer strip: level/pan/mute/solo/sends/sidechain/outBus)
//   - global FX / return / master (mu_mixfx::addGlobalFxParams)
// plus representative ranges/defaults and the removal of the dead eff_send/dly_send.
//
// It does NOT construct the full PluginProcessor - its createEditor() drags the
// editor/UI tree into the console app (the same constraint mu-clid-tests works
// around). The live syncGlobalFxParam->engine path + .muTant file round-trip stay
// out for that reason (the modulator/gate serialise round-trip is covered by
// MuTantPersistTests; the sync is exercised at runtime in both products).

#include <juce_audio_processors/juce_audio_processors.h>
#include "Plugin/PluginProcessor.h"   // mu_tant::PluginProcessor::createParameterLayout

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

class MuTantApvtsLayoutTest : public juce::UnitTest
{
public:
    MuTantApvtsLayoutTest() : juce::UnitTest ("APVTS layout (real createParameterLayout)", "Mixer") {}

    void runTest() override
    {
        using mu_tant::PluginProcessor;

        StubProcessor proc;
        juce::AudioProcessorValueTreeState apvts (proc, nullptr, "PARAMS",
                                                  PluginProcessor::createParameterLayout());

        auto has = [&](const juce::String& id) { return apvts.getParameter(id) != nullptr; };

        beginTest ("Per-voice v{N}_ params exist for all 8 voices");
        {
            static const char* base[] = {
                "o1_oct","o1_semi","o1_fine","o1_pos","o2_oct","o2_semi","o2_fine","o2_pos",
                "xmod_phaseMode","xmod_index","sync","xmod_fdbk","xmod_ampMode","xmod_depth","xmod_ssb",
                "o1_lvl","o2_lvl","noise_lvl","noise_type",
                "flt_type","flt_cut","flt_res","level","gate_gap","gate_bypass",
                "drvChar","insP1","insP2","insP3","insP4",
            };
            for (int v = 0; v < PluginProcessor::kMaxVoices; ++v)
                for (const char* b : base)
                {
                    const juce::String id = PluginProcessor::voiceParamId(v, b);
                    expect (has(id), "missing per-voice param '" + id + "'");
                }
        }

        beginTest ("Shared tonal centre params exist");
        {
            expect (has("root"),  "missing 'root'");
            expect (has("scale"), "missing 'scale'");
        }

        beginTest ("Mixer ch{N}_ strips exist for all 8 channels");
        {
            static const char* base[] = {
                "lvl","pan","mute","solo","sendEff","sendDly","sendRev",
                "scSrc","scAmt","scAtk","scRel","outBus",
            };
            for (int i = 0; i < PluginProcessor::kMaxVoices; ++i)
                for (const char* b : base)
                {
                    const juce::String id = "ch" + juce::String(i) + "_" + b;
                    expect (has(id), "missing channel-strip param '" + id + "'");
                }
        }

        beginTest ("Global FX / return / master params exist");
        {
            static const char* global[] = {
                "eff_algo","eff_en","eff_p0","eff_p4",
                "dly_en","dly_ms","dly_fb","dly_dirt",
                "rev_algo","rev_en","rev_lvl","rev_dirt",
                "eff2dly","eff2rev","dly2rev",
                "echo_en","echo_fb","echo_dirt",
                "ret_eff_lvl","ret_dly_lvl","ret_rev_lvl","ret_eff_scSrc",
                "mstr_lvl","mstr_pan","mst_insChar","mst_ins2Char","mst_ins2P4",
            };
            for (const char* id : global)
                expect (has(id), juce::String("missing global-FX param '") + id + "'");
        }

        beginTest ("Dead legacy sends removed");
        {
            expect (! has("eff_send"), "eff_send must not exist");
            expect (! has("dly_send"), "dly_send must not exist");
        }

        beginTest ("Representative ranges + defaults match the layout spec");
        {
            auto def = [&](const char* id, float expected)
            {
                auto* p = apvts.getParameter(id);
                if (p == nullptr) { expect(false, juce::String("missing '") + id + "'"); return; }
                expectWithinAbsoluteError (p->convertFrom0to1(p->getDefaultValue()), expected, 1.0e-3f,
                                           juce::String("default mismatch for '") + id + "'");
            };
            def ("rev_algo",  1.0f);     // Hall
            def ("mstr_lvl",  1.0f);
            def ("rev_lvl",   1.0f);
            def ("dly_fb",    0.30f);
            def ("v0_o2_semi", 2.0f);    // osc2 defaults +2 semitones
            def ("v0_o1_lvl",  0.0f);    // osc1 0 dB
            def ("v0_o2_lvl", -6.0f);

            // v0_o1_oct is an Int param -3..3 (7 discrete steps).
            if (auto* p = apvts.getParameter("v0_o1_oct"))
                expectEquals (p->getNumSteps(), 7, "v0_o1_oct should expose 7 steps (-3..3)");
            if (auto* p = apvts.getParameter("rev_algo"))
                expectEquals (p->getNumSteps(), 4, "rev_algo should expose 4 steps (Room/Hall/Plate/Spring)");
        }
    }
};

static MuTantApvtsLayoutTest muTantApvtsLayoutTest;
