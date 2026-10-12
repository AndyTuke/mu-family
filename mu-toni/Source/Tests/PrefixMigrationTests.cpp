// The layer parameter-id prefix moved from "v{N}_" to "a{N}_". These tests load data captured from the
// build BEFORE the rename (the XML and JSON below are verbatim lines of that build's output) into the
// real, headless PluginProcessor and check that every value lands on its renamed parameter.

#include "Plugin/PluginProcessor.h"
#include "Tests/ProcessorSeamHelpers.h"

namespace
{
    // An older APVTS dump (what a pre-format-2 host session or full preset held): ids still carry "v{N}_".
    const char* const kLegacyDump = R"(<MuToniState>
  <PARAM id="ch0_lvl" value="0.5"/>
  <PARAM id="v0_chord" value="10.0"/>
  <PARAM id="v0_cut" value="777.0"/>
  <PARAM id="v0_xmod_phaseMode" value="2.0"/>
  <PARAM id="v1_aeA" value="0.5"/>
  <PARAM id="v2_cut" value="1500.0"/>
  <PARAM id="v2_scale" value="5.0"/>
  <PARAM id="v3_leg" value="1.0"/>
</MuToniState>)";

    // A format-2 state from the same build: rows carry the id without any prefix.
    const char* const kFormat2State = R"(<MuToniState format="2">
  <Globals>
    <p id="ch0_lvl" x="0.5"/>
  </Globals>
  <Slots>
    <Slot idx="0">
      <p id="chord" x="10.0"/>
      <p id="xmod_phaseMode" x="2.0" c="TZFM"/>
      <p id="cut" x="777.0"/>
    </Slot>
    <Slot idx="1">
      <p id="aeA" x="0.5"/>
    </Slot>
    <Slot idx="2">
      <p id="scale" x="5.0"/>
      <p id="cut" x="1500.0"/>
    </Slot>
    <Slot idx="3">
      <p id="leg" x="1.0"/>
    </Slot>
  </Slots>
</MuToniState>)";

    // A MIDI map file as that build saved it (one mapping per renamed layer id, plus a mixer id).
    const char* const kLegacyMidiMap = R"({"version": 1, "quantise": "bar", "mappings": [
  {"type": "cc", "channel": 1, "number": 21, "action": "parameter", "layer": 0, "param": "v0_cut", "quantise": "default"},
  {"type": "cc", "channel": 1, "number": 22, "action": "parameter", "layer": 0, "param": "v3_aeA", "quantise": "default"},
  {"type": "cc", "channel": 1, "number": 23, "action": "parameter", "layer": 0, "param": "ch0_lvl", "quantise": "default"}]})";
}

struct MuToniPrefixMigrationTests : juce::UnitTest
{
    MuToniPrefixMigrationTests() : juce::UnitTest("mu-Toni id prefix migration", "mu-toni-processor") {}

    // Loads `xml` as a host session, then checks the values every fixture above holds.
    void loadAndCheck(const char* xml)
    {
        using namespace mu_seam_test;
        TempStorage storage;
        mu_toni::PluginProcessor p;

        juce::MemoryBlock block;
        juce::AudioProcessor::copyXmlToBinary(*juce::XmlDocument::parse(xml), block);
        p.setStateInformation(block.getData(), (int) block.getSize());

        expectWithinAbsoluteError(getParam(p.apvts, "a0_cut"), 777.0f, 0.5f);
        expectEquals((int) getParam(p.apvts, "a0_chord"), 10);
        expectEquals((int) getParam(p.apvts, "a0_xmod_phaseMode"), 2);
        expectWithinAbsoluteError(getParam(p.apvts, "a1_aeA"), 0.5f, 0.002f);
        expectWithinAbsoluteError(getParam(p.apvts, "a2_cut"), 1500.0f, 0.5f);
        expectEquals((int) getParam(p.apvts, "a2_scale"), 5);
        expectEquals((int) getParam(p.apvts, "a3_leg"), 1);
        expectWithinAbsoluteError(getParam(p.apvts, "ch0_lvl"), 0.5f, 1e-3f);
        expectWithinAbsoluteError(getParam(p.apvts, "a1_cut"), 3000.0f, 0.5f);   // untouched layers stay at their default
        expect(p.apvts.getParameter("v0_cut") == nullptr, "the old id no longer exists");
    }

    void runTest() override
    {
        using namespace mu_seam_test;

        beginTest("an APVTS dump written before the rename (v{N}_ ids) still loads");
        loadAndCheck(kLegacyDump);

        beginTest("a format-2 state written before the rename still loads");
        loadAndCheck(kFormat2State);

        beginTest("a MIDI map written before the rename is renamed on load and saved once");
        {
            TempStorage storage;
            const auto mapFile = storage.root.getChildFile("settings").getChildFile("muToni_midiControl.json");
            mapFile.getParentDirectory().createDirectory();
            mapFile.replaceWithText(kLegacyMidiMap);

            mu_toni::PluginProcessor p;
            auto& map = p.getMidiControlMap();
            expectEquals(map.size(), 3);
            expectEquals(map.get(map.find(false, 1, 21)).action.paramId, juce::String("a0_cut"));
            expectEquals(map.get(map.find(false, 1, 22)).action.paramId, juce::String("a3_aeA"));
            expectEquals(map.get(map.find(false, 1, 23)).action.paramId, juce::String("ch0_lvl"));
            for (int i = 0; i < map.size(); ++i)
                expect(p.apvts.getParameter(map.get(i).action.paramId) != nullptr,
                       "mapped id " + map.get(i).action.paramId + " is a live parameter");

            const auto saved = mapFile.loadFileAsString();
            expect(saved.contains("a0_cut") && saved.contains("a3_aeA") && ! saved.contains("v0_cut"),
                   "the file on disk holds the new ids");
        }
    }
};

static MuToniPrefixMigrationTests muToniPrefixMigrationTests;
