// Preset-file coverage: the shared mu-core helpers (full preset write / read, categories,
// a layer's prefixed params) and mu-On's per-track step-row round-trip. Like the other
// tests it uses a minimal headless AudioProcessor, not the full PluginProcessor.

#include <juce_audio_processors/juce_audio_processors.h>
#include "Persistence/PresetFiles.h"   // mu-core: shared preset-file handling
#include "Sequencer/StepPattern.h"

using namespace mu_on;

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

// Two lanes' worth of params: k_* and b_*, so a layer write must keep to its own prefix.
juce::AudioProcessorValueTreeState::ParameterLayout stubLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    using P = juce::AudioParameterFloat;
    layout.add(std::make_unique<P>(juce::ParameterID { "k_tune", 1 }, "Kick Tune",  0.0f, 1.0f, 0.5f));
    layout.add(std::make_unique<P>(juce::ParameterID { "k_dec",  1 }, "Kick Decay", 0.0f, 1.0f, 0.2f));
    layout.add(std::make_unique<P>(juce::ParameterID { "b_tune", 1 }, "Bass Tune",  0.0f, 1.0f, 0.5f));
    return layout;
}
} // namespace

class PresetTest : public juce::UnitTest
{
public:
    PresetTest() : juce::UnitTest("Preset files", "Presets") {}

    void runTest() override
    {
        const auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory)
                             .getChildFile("mu-on-preset-tests-" + juce::String(juce::Time::currentTimeMillis()));

        beginTest("safe preset file name: each unsafe character becomes '_', dots are kept, empty → fallback");
        {
            expectEquals(mu_pp::safePresetFileName("a\\b/c:d|e*f?g<h>i\"j", "X"), juce::String("a_b_c_d_e_f_g_h_i_j"));
            expectEquals(mu_pp::safePresetFileName("Kick 1.5", "X"), juce::String("Kick 1.5"));
            expectEquals(mu_pp::safePresetFileName("", "Rhythm"), juce::String("Rhythm"));
        }

        beginTest("full preset round-trips its state and metadata; categories are listed once");
        {
            juce::ValueTree state("MuOnState");
            state.setProperty("marker", 42, nullptr);
            const auto f = mu_pp::writeFullPreset(dir, "muOn", "MuOnPreset", "Big: Room", "d", "Drums", state);
            mu_pp::writeFullPreset(dir, "muOn", "MuOnPreset", "Other", "", "Drums", state);
            expectEquals(f.getFileName(), juce::String("Big_ Room.muOn"));   // unsafe ':' replaced

            juce::String error;
            const auto back = mu_pp::readFullPreset(f, "MuOnPreset", "MuOnState", error);
            expect(back.isValid(), error);
            expectEquals((int) back.getProperty("marker"), 42);
            expectEquals(mu_pp::readPresetCategories(dir, "muOn", "MuOnPreset").joinIntoString(","), juce::String("Drums"));
            expectEquals(mu_pp::listPresetFiles(dir, "muOn").size(), 2);

            mu_pp::readFullPreset(f, "MuOnPreset", "SomeOtherState", error);
            expect(error.isNotEmpty(), "a preset for another product's state is refused");
        }

        beginTest("layer params: written prefix-free, applied to another prefix, others reset");
        {
            StubProcessor proc;
            juce::AudioProcessorValueTreeState apvts(proc, nullptr, "S", stubLayout());
            apvts.getParameter("k_tune")->setValueNotifyingHost(0.9f);
            apvts.getParameter("b_tune")->setValueNotifyingHost(0.1f);

            juce::XmlElement root("MuOnTrack");
            mu_pp::writeLayerParams(root, proc, "k_");
            expectEquals(root.getNumChildElements(), 2);   // k_tune + k_dec only
            expectEquals(root.getChildElement(0)->getStringAttribute("id"), juce::String("tune"));

            apvts.getParameter("k_dec")->setValueNotifyingHost(0.7f);
            mu_pp::applyLayerParams(juce::ValueTree::fromXml(root), apvts, "b_");
            expectWithinAbsoluteError(apvts.getParameter("b_tune")->getValue(), 0.9f, 1e-4f);

            mu_pp::applyLayerParams({}, apvts, "k_");   // empty tree = reset to defaults
            expectWithinAbsoluteError(apvts.getParameter("k_dec")->getValue(), 0.2f, 1e-4f);
            expectWithinAbsoluteError(apvts.getParameter("b_tune")->getValue(), 0.9f, 1e-4f);   // other lane untouched
        }

        beginTest("a track row moves between tracks and clears what it doesn't cover");
        {
            StepPattern a;
            a.clear();
            a.setOn(0, 0, true); a.setOn(0, 5, true); a.setAccent(0, 5, true);
            StepPattern b;
            b.loadDefaultGroove();
            b.deserialiseTrack(2, a.serialiseTrack(0));
            for (int s = 0; s < StepPattern::kNumSteps; ++s)
            {
                expect(b.isOn(2, s)     == a.isOn(0, s),     "on mismatch");
                expect(b.isAccent(2, s) == a.isAccent(0, s), "accent mismatch");
            }
            expect(b.isOn(0, 4), "other tracks keep their steps");
        }

        dir.deleteRecursively();
    }
};

static PresetTest presetTest;
