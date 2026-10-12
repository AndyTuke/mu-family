// The lane parameter-id prefixes moved from "k_ b_ h_ s_ r_" (by lane type) to "t0_".."t4_" (by slot).
// These tests load data captured from the build BEFORE the rename (the XML and JSON below are verbatim
// lines of that build's output) into the real, headless PluginProcessor and check that every value lands
// on its renamed parameter, and that the modulation tables follow the new prefixes.

#include "Plugin/PluginProcessor.h"
#include "Modulation/MuOnModDest.h"
#include "Tests/ProcessorSeamHelpers.h"

namespace
{
    // An older APVTS dump (what a pre-format-2 host session or full preset held): ids still carry the old letters.
    const char* const kLegacyDump = R"(<MuOnState>
  <PARAM id="b_cut" value="1200.0"/>
  <PARAM id="b_wave" value="2.0"/>
  <PARAM id="h_dec" value="100.0"/>
  <PARAM id="k_drive" value="0.7000000476837158"/>
  <PARAM id="k_tune" value="60.0"/>
  <PARAM id="r_d1" value="0.1000000014901161"/>
  <PARAM id="r_size" value="0.9000000357627869"/>
  <PARAM id="s_tune" value="3.5"/>
</MuOnState>)";

    // A format-2 state from the same build: rows carry the id without any prefix.
    const char* const kFormat2State = R"(<MuOnState format="2">
  <Slots>
    <Slot idx="0">
      <p id="tune" x="60.0"/>
      <p id="drive" x="0.7000000476837158"/>
      <Track i="0" on="1000100010001000" accent="1000000000000000"/>
    </Slot>
    <Slot idx="1">
      <p id="wave" x="2.0" c="Square"/>
      <p id="cut" x="1200.0"/>
    </Slot>
    <Slot idx="2">
      <p id="dec" x="100.0"/>
    </Slot>
    <Slot idx="3">
      <p id="tune" x="3.5"/>
    </Slot>
    <Slot idx="4">
      <p id="d1" x="0.1000000014901161"/>
      <p id="size" x="0.9000000357627869"/>
    </Slot>
  </Slots>
</MuOnState>)";

    // A MIDI map file as that build saved it: one mapping per lane's old prefix, plus a mixer id.
    const char* const kLegacyMidiMap = R"({"version": 1, "quantise": "bar", "mappings": [
  {"type": "cc", "channel": 1, "number": 21, "action": "parameter", "layer": 0, "param": "k_tune", "quantise": "default"},
  {"type": "cc", "channel": 1, "number": 22, "action": "parameter", "layer": 0, "param": "b_cut", "quantise": "default"},
  {"type": "cc", "channel": 1, "number": 23, "action": "parameter", "layer": 0, "param": "h_dec", "quantise": "default"},
  {"type": "cc", "channel": 1, "number": 24, "action": "parameter", "layer": 0, "param": "s_tune", "quantise": "default"},
  {"type": "cc", "channel": 1, "number": 25, "action": "parameter", "layer": 0, "param": "r_size", "quantise": "default"},
  {"type": "cc", "channel": 1, "number": 26, "action": "parameter", "layer": 0, "param": "ch1_lvl", "quantise": "default"}]})";
}

struct MuOnPrefixMigrationTests : juce::UnitTest
{
    MuOnPrefixMigrationTests() : juce::UnitTest("mu-On id prefix migration", "mu-on-processor") {}

    // Loads `xml` as a host session, then checks the values every fixture above holds.
    void loadAndCheck(const char* xml)
    {
        using namespace mu_seam_test;
        TempStorage storage;
        mu_on::PluginProcessor p;

        juce::MemoryBlock block;
        juce::AudioProcessor::copyXmlToBinary(*juce::XmlDocument::parse(xml), block);
        p.setStateInformation(block.getData(), (int) block.getSize());

        expectWithinAbsoluteError(getParam(p.apvts, "t0_tune"), 60.0f, 0.05f);
        expectWithinAbsoluteError(getParam(p.apvts, "t0_drive"), 0.7f, 0.002f);
        expectEquals((int) getParam(p.apvts, "t1_wave"), 2);
        expectWithinAbsoluteError(getParam(p.apvts, "t1_cut"), 1200.0f, 0.5f);
        expectWithinAbsoluteError(getParam(p.apvts, "t2_dec"), 100.0f, 0.5f);
        expectWithinAbsoluteError(getParam(p.apvts, "t3_tune"), 3.5f, 0.05f);
        expectWithinAbsoluteError(getParam(p.apvts, "t4_d1"), 0.1f, 0.002f);
        expectWithinAbsoluteError(getParam(p.apvts, "t4_size"), 0.9f, 0.002f);
        expectWithinAbsoluteError(getParam(p.apvts, "t1_tune"), 41.2f, 0.05f);   // untouched params stay at their default
        for (const char* old : { "k_tune", "b_cut", "h_dec", "s_tune", "r_size" })
            expect(p.apvts.getParameter(old) == nullptr, juce::String("the old id no longer exists: ") + old);
    }

    void runTest() override
    {
        using namespace mu_seam_test;

        beginTest("an APVTS dump written before the rename (k_ b_ h_ s_ r_ ids) still loads");
        loadAndCheck(kLegacyDump);

        beginTest("a format-2 state written before the rename still loads, steps included");
        {
            loadAndCheck(kFormat2State);

            TempStorage storage;
            mu_on::PluginProcessor p;
            for (int s = 0; s < 16; ++s) p.pattern().setOn(0, s, false);   // wipe the Kick row, then load it back
            juce::MemoryBlock block;
            juce::AudioProcessor::copyXmlToBinary(*juce::XmlDocument::parse(kFormat2State), block);
            p.setStateInformation(block.getData(), (int) block.getSize());
            expect(p.pattern().isOn(0, 0) && p.pattern().isOn(0, 4) && ! p.pattern().isOn(0, 1), "Kick steps restored");
        }

        beginTest("a MIDI map written before the rename is renamed on load and saved once");
        {
            TempStorage storage;
            const auto mapFile = storage.root.getChildFile("settings").getChildFile("muOn_midiControl.json");
            mapFile.getParentDirectory().createDirectory();
            mapFile.replaceWithText(kLegacyMidiMap);

            mu_on::PluginProcessor p;
            auto& map = p.getMidiControlMap();
            expectEquals(map.size(), 6);
            const char* expected[] = { "t0_tune", "t1_cut", "t2_dec", "t3_tune", "t4_size", "ch1_lvl" };
            for (int i = 0; i < 6; ++i)
            {
                const auto id = map.get(map.find(false, 1, 21 + i)).action.paramId;
                expectEquals(id, juce::String(expected[i]));
                expect(p.apvts.getParameter(id) != nullptr, "mapped id " + id + " is a live parameter");
            }
            const auto saved = mapFile.loadFileAsString();
            expect(saved.contains("t4_size") && ! saved.contains("r_size") && ! saved.contains("\"k_tune"),
                   "the file on disk holds the new ids");
        }

        beginTest("modulation targets name live parameters under their lane's prefix; ids are unchanged");
        {
            TempStorage storage;
            mu_on::PluginProcessor p;
            for (int lane = 0; lane < mu_on::kNumChannels; ++lane)
            {
                int n = 0;
                const auto* t = mu_on::destsForLane(lane, n);
                expect(n > 0, "lane " + juce::String(lane) + " has targets");
                for (int i = 0; i < n; ++i)
                {
                    expect(p.apvts.getParameter(t[i].param) != nullptr, juce::String("live parameter: ") + t[i].param);
                    expect(juce::String(t[i].param).startsWith(mu_on::layerParamPrefix(lane)), juce::String(t[i].param));
                }
            }
            expect(mu_on::isValidLaneDest(mu_on::Kick, "k.tune.prop") && mu_on::isValidLaneDest(mu_on::Rumble, "r.size.prop"),
                   "mod-target ids kept their letters");
        }
    }
};

static MuOnPrefixMigrationTests muOnPrefixMigrationTests;
