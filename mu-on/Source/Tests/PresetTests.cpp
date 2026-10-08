// Preset-file coverage: the shared mu-core helpers (full preset write / read, categories,
// preset metadata, atomic saves) and mu-On's per-track step-row round-trip. Like the other
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

            // The dropdown lister reads the wrapper's category (not mu-Clid's presetCategory key).
            const auto listed = mu_pp::listPresetsByCategory(dir, "muOn");
            expectEquals((int) listed.size(), 2);
            expectEquals(listed[0].category, juce::String("Drums"));
            expectEquals(mu_pp::readPresetMeta(f).description, juce::String("d"));

            mu_pp::readFullPreset(f, "MuOnPreset", "SomeOtherState", error);
            expect(error.isNotEmpty(), "a preset for another product's state is refused");
        }

        beginTest("atomic save: replaces an existing preset whole; a failed write reports and keeps the old file");
        {
            const auto f = dir.getChildFile("Atomic.muOn");
            expect(mu_pp::replaceFileAtomically(f, "first"), "first write");
            expect(mu_pp::replaceFileAtomically(f, "second"), "overwrite");
            expectEquals(f.loadFileAsString(), juce::String("second"));
            expect(! f.getSiblingFile("Atomic.muOn.tmp").existsAsFile(), "no temp file left behind");

            // A parent that is a file, not a folder, can never be written: the failure is reported.
            const auto blocker = dir.getChildFile("NotAFolder");
            blocker.replaceWithText("x");
            juce::String reported;
            const bool ok = mu_pp::replaceFileAtomically(blocker.getChildFile("P.muOn"), "y",
                                                         [&](const juce::String& m) { reported = m; });
            expect(! ok, "write into an impossible path fails");
            expect(reported.contains("P.muOn"), "failure names the file: " + reported);

            juce::ValueTree state("MuOnState");
            expect(mu_pp::writeFullPreset(blocker, "muOn", "MuOnPreset", "X", "", "", state, [](const juce::String&) {})
                       == juce::File(), "writeFullPreset returns an empty File on failure");
        }

        beginTest("preset metadata: both file shapes, entities, prolog; huge values skipped; a re-save is re-read");
        {
            juce::ValueTree state("MuOnState");
            const auto wrapped = mu_pp::writeFullPreset(dir, "muOn", "MuOnPreset", "Amp", "a <b> & \"c\"", "Bass & Low", state);
            auto meta = mu_pp::readPresetMeta(wrapped);
            expectEquals(meta.rootTag, juce::String("MuOnPreset"));
            expectEquals(meta.category, juce::String("Bass & Low"));
            expectEquals(meta.description, juce::String("a <b> & \"c\""));

            // mu-Clid's shape: root properties, a comment in the prolog, single quotes, and an
            // embedded sample far longer than any kept value.
            const auto clid = dir.getChildFile("Kick.muRhythm");
            const juce::String blob = juce::String::repeatedString("QUJD", 5000);
            clid.replaceWithText("<?xml version=\"1.0\"?>\n<!-- saved -> by test -->\n"
                                 "<MuClidRhythm presetName=\"Kick\" presetCategory=\"Kicks\" presetEmbedSamples=\"1\" "
                                 "sampleData=\"" + blob + "\" lane='Kick'><Modulators/></MuClidRhythm>");
            meta = mu_pp::readPresetMeta(clid);
            expectEquals(meta.rootTag, juce::String("MuClidRhythm"));
            expectEquals(meta.category, juce::String("Kicks"));
            expect(meta.embedSamples, "presetEmbedSamples read");
            expectEquals(meta.attribute("lane"), juce::String("Kick"));
            expect(meta.attributes.count("sampleData") == 0, "the embedded sample is not kept");

            // Re-saving with a new category is picked up on the next read.
            clid.replaceWithText("<MuClidRhythm presetCategory=\"Snares and more\"/>");
            expectEquals(mu_pp::readPresetMeta(clid).category, juce::String("Snares and more"));

            const auto junk = dir.getChildFile("Junk.muOn");
            junk.replaceWithText("not xml at all");
            expect(! mu_pp::readPresetMeta(junk).isValid(), "a non-XML file has no metadata");
            expect(! mu_pp::readPresetMeta(dir.getChildFile("Missing.muOn")).isValid(), "a missing file has no metadata");
            clid.deleteFile();
            junk.deleteFile();
            wrapped.deleteFile();
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
