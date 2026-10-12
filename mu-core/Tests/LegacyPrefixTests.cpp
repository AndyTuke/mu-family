// Renamed parameter-id prefixes: LayerLayout::addLegacyPrefixes (older APVTS dumps still load) and
// MidiControlMap::setIdPrefixRenames (an old MIDI map file is renamed and saved once).

#include <juce_audio_processors/juce_audio_processors.h>
#include "Persistence/LayerState.h"
#include "Control/MidiControlMap.h"

namespace
{
class LegacyStubProcessor : public juce::AudioProcessor
{
public:
    const juce::String getName() const override          { return "legacy-stub"; }
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

// Two slots under their current prefixes (t0_ / t1_) plus one global.
juce::AudioProcessorValueTreeState::ParameterLayout currentLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    for (const char* pre : { "t0_", "t1_" })
    {
        const juce::String p(pre);
        layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { p + "tune", 1 }, p + "Tune",
                                                               juce::NormalisableRange<float>(20.0f, 2000.0f), 100.0f));
    }
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { "level", 1 }, "Level", -60.0f, 6.0f, 0.0f));
    return layout;
}

float actual(juce::AudioProcessorValueTreeState& s, const char* id)
{
    auto* p = s.getParameter(id);
    return p->convertFrom0to1(p->getValue());
}
} // namespace

class LegacyPrefixTest : public juce::UnitTest
{
public:
    LegacyPrefixTest() : juce::UnitTest("Legacy id prefixes", "Presets") {}

    static juce::ValueTree dumpOf(std::initializer_list<std::pair<const char*, double>> params)
    {
        juce::ValueTree dump("StubState");
        for (const auto& [id, v] : params)
        {
            juce::ValueTree p("PARAM");
            p.setProperty("id", id, nullptr);
            p.setProperty("value", v, nullptr);
            dump.appendChild(p, nullptr);
        }
        return dump;
    }

    void runTest() override
    {
        beginTest("a legacy APVTS dump loads through the old prefixes");
        {
            LegacyStubProcessor proc;
            juce::AudioProcessorValueTreeState apvts(proc, nullptr, "StubState", currentLayout());
            mu_pp::LayerLayout layout(proc, { "t0_", "t1_" });
            layout.addLegacyPrefixes({ "k_", "b_" });

            const auto composed = mu_pp::composeLegacyState(
                dumpOf({ { "k_tune", 440.0 }, { "b_tune", 55.0 }, { "level", -6.0 } }), layout);
            mu_pp::applyState(composed, layout, {});
            expectWithinAbsoluteError(actual(apvts, "t0_tune"), 440.0f, 0.5f);
            expectWithinAbsoluteError(actual(apvts, "t1_tune"), 55.0f, 0.5f);
            expectWithinAbsoluteError(actual(apvts, "level"), -6.0f, 1e-3f);
            expect(mu_pp::findLayerNode(composed, 0).getChildWithProperty("id", "tune").isValid(),
                   "the row id carries neither prefix");
        }

        beginTest("current prefixes win; live ids are never read through the legacy list");
        {
            LegacyStubProcessor proc;
            juce::AudioProcessorValueTreeState apvts(proc, nullptr, "StubState", currentLayout());
            mu_pp::LayerLayout layout(proc, { "t0_", "t1_" });
            layout.addLegacyPrefixes({ "t1_", "t0_" });   // deliberately swapped: must not matter for live ids

            expectEquals(layout.slotOf("t0_tune"), 0);
            expectEquals(layout.slotOf("t1_tune"), 1);
            expectEquals(layout.numParams(0), 1);
            expectEquals(layout.numParams(1), 1);

            const auto composed = mu_pp::composeLegacyState(dumpOf({ { "t0_tune", 300.0 } }), layout);
            mu_pp::applyState(composed, layout, {});
            expectWithinAbsoluteError(actual(apvts, "t0_tune"), 300.0f, 0.5f);   // not routed to slot 1
            expectWithinAbsoluteError(actual(apvts, "t1_tune"), 100.0f, 0.5f);
        }

        beginTest("without a legacy list an old id is a dropped global, as before");
        {
            LegacyStubProcessor proc;
            juce::AudioProcessorValueTreeState apvts(proc, nullptr, "StubState", currentLayout());
            const mu_pp::LayerLayout layout(proc, { "t0_", "t1_" });
            const auto composed = mu_pp::composeLegacyState(dumpOf({ { "k_tune", 440.0 } }), layout);
            mu_pp::applyState(composed, layout, {});
            expectWithinAbsoluteError(actual(apvts, "t0_tune"), 100.0f, 0.5f);
        }

        beginTest("MIDI map: an old file is renamed on load and saved once; live ids are not rewritten");
        {
            const auto file = juce::File::createTempFile(".json");
            file.deleteFile();
            {
                mu_core::MidiControlMap old;
                old.setStorageFile(file);
                mu_core::MidiMapping a, b, c;
                a.number = 21; a.action.type = mu_core::ControlActionType::Parameter; a.action.paramId = "k_tune";
                b.number = 22; b.action.type = mu_core::ControlActionType::Parameter; b.action.paramId = "mstr_lvl";
                c.number = 23; c.action.type = mu_core::ControlActionType::Parameter; c.action.paramId = "b_cut";
                old.add(a); old.add(b); old.add(c);
            }

            mu_core::MidiControlMap map;
            map.setStorageFile(file);
            map.setIdPrefixRenames({ { "k_", "t0_" }, { "b_", "t1_" } });
            map.load();
            expectEquals(map.size(), 3);
            expectEquals(map.get(map.find(false, 1, 21)).action.paramId, juce::String("t0_tune"));
            expectEquals(map.get(map.find(false, 1, 22)).action.paramId, juce::String("mstr_lvl"));   // not a renamed prefix
            expectEquals(map.get(map.find(false, 1, 23)).action.paramId, juce::String("t1_cut"));

            // The file itself was rewritten: a loader with no renames sees the new ids.
            mu_core::MidiControlMap again;
            again.setStorageFile(file);
            again.load();
            expectEquals(again.get(again.find(false, 1, 21)).action.paramId, juce::String("t0_tune"));

            // A map added after load is stored as given, even under an old prefix.
            mu_core::MidiMapping live;
            live.number = 30; live.action.type = mu_core::ControlActionType::Parameter; live.action.paramId = "k_live";
            map.add(live);
            expectEquals(map.get(map.find(false, 1, 30)).action.paramId, juce::String("k_live"));
            file.deleteFile();
        }
    }
};

static LegacyPrefixTest legacyPrefixTest;
