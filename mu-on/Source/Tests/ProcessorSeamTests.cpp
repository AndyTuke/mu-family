// Tests on the REAL mu-On PluginProcessor, built headless (no editor, host or audio device):
// construction under a temp storage root, layer ownership after construction, and the structural
// edits (add / delete / reorder) on a processor whose layers are tracks with an engine.

#include "Plugin/PluginProcessor.h"
#include "Tests/ProcessorSeamHelpers.h"

#include <set>

// The product class names its editor in a virtual, defined in PluginEditor.cpp, which this headless
// target does not link (see "Headless product processors" in design-plugin-family.md).
juce::AudioProcessorEditor* mu_on::PluginProcessor::createEditor() { return nullptr; }

namespace
{
    using mu_on::EngineType;

    // The real processor with the layer storage exposed to the tests.
    struct ExposedPluginProcessor : mu_on::PluginProcessor
    {
        using mu_on::PluginProcessor::layers;
        using mu_on::PluginProcessor::numLayers;
        using mu_on::PluginProcessor::unsavedLayerCount;
    };

    int engineIndex(mu_on::PluginProcessor& p, int track)
    {
        return (int) mu_seam_test::getParam(p.apvts, mu_on::layerParamId(track, "eng"));
    }
    int rumbleSource(mu_on::PluginProcessor& p, int track)
    {
        return (int) mu_seam_test::getParam(p.apvts, mu_on::layerParamId(track, "src"));
    }
}

struct MuOnProcessorSeamTests : juce::UnitTest
{
    MuOnProcessorSeamTests() : juce::UnitTest("mu-On real PluginProcessor", "mu-on-processor") {}

    // The shared swap-timing parameter exists in every product, with that product's default.
    void swapTimingDefault()
    {
        beginTest("mstr_swap exists and defaults to OnBar in mu-On");
        mu_seam_test::TempStorage storage;
        ExposedPluginProcessor p;
        auto* swap = p.apvts.getParameter("mstr_swap");
        expect(swap != nullptr, "the swap timing parameter is declared");
        if (swap != nullptr)
            expectEquals((int) std::lround(swap->convertFrom0to1(swap->getDefaultValue())), 2);
    }

    void runTest() override
    {
        swapTimingDefault();
        using namespace mu_seam_test;
        beginTest("A fresh instance is one Kick track with the four-on-the-floor row; every slot's Track exists and getTrack(i) is layer i");
        TempStorage storage;
        ExposedPluginProcessor p;
        expectEquals(p.numLayers.load(), 1, "a fresh instance starts with one layer");
        expectEquals(p.getNumLayers(), 1);
        expectEquals(p.unsavedLayerCount, 5, "an older state without a layer count held five lanes");
        expect(p.getLayer(1) == nullptr && p.getLayer(-1) == nullptr, "no layer above the live count or below zero");
        std::set<const void*> seen;
        for (int i = 0; i < mu_limits::kMaxLayers; ++i)
        {
            expect(p.layers[(size_t) i] != nullptr, "slot " + juce::String(i) + " is allocated");
            expect(&p.getTrack(i) == p.layers[(size_t) i].get(), "getTrack(" + juce::String(i) + ") is layer " + juce::String(i));
            seen.insert(&p.getTrack(i));
            expectEquals(p.getTrack(i).slot, i, "the track knows its slot");
        }
        expect(p.getLayer(0) == &p.getTrack(0), "getLayer(0) is getTrack(0)");
        expectEquals((int) seen.size(), mu_limits::kMaxLayers, "distinct tracks");
        expect(p.getTrackEngine(0) == EngineType::Kick, "the fresh track is a Kick");
        expectEquals(p.getLayerName(0), juce::String("Kick"), "a track is named after its engine");
        for (int s = 0; s < 16; ++s)
            expect(p.pattern().isOn(0, s) == (s % 4 == 0), "four on the floor, step " + juce::String(s));
        expect(p.pattern().isAccent(0, 0), "the downbeat is accented");

        beginTest("Each track writes its own step row; the Rumble envelope is written only once it is drawn");
        for (int i = 0; i < mu_limits::kMaxLayers; ++i)
        {
            juce::ValueTree node("Slot");
            p.getTrack(i).writeExtras(node);
            auto row = node.getChildWithName("Track");
            expect(row.isValid(), "track " + juce::String(i) + " writes a step row (kept whatever its engine)");
            expectEquals((int) row.getProperty("i", -1), i, "track " + juce::String(i) + " writes its own row");
            expect(! node.getChildWithName("RumbleEnv").isValid(), "a default envelope is not written");
        }
        {
            auto& t = p.getTrack(3);
            t.rumbleEnv.curvePoints = { { 0.0f, 1.0f }, { 0.5f, 0.2f }, { 1.0f, 1.0f } };
            juce::ValueTree node("Slot");
            t.writeExtras(node);
            expect(node.getChildWithName("RumbleEnv").isValid(), "a drawn envelope is written");
            juce::ValueTree other("Slot");
            p.getTrack(4).applyExtras(node);
            p.getTrack(4).writeExtras(other);
            expectEquals(other.getChildWithName("RumbleEnv").getNumChildren(), 3, "and read back into another track");
            p.getTrack(4).applyExtras(juce::ValueTree("Slot"));
            expect(p.getTrack(4).rumbleEnvIsDefault(), "a node without an envelope puts the default back");
            t.resetRumbleEnv();
        }

        beginTest("prepareToPlay and a processBlock run under the temp root");
        p.prepareToPlay(48000.0, 256);
        juce::AudioBuffer<float> buf(2, 256);
        juce::MidiBuffer midi;
        p.processBlock(buf, midi);
        expect(bufferIsSane(buf), "output is finite");
        expectEquals(p.getContentDir().getFullPathName(),
                     storage.root.getChildFile("content").getChildFile("muOn").getFullPathName());

        runLayerStructureTests();
        runStressTest();
    }

    // Fill to eight tracks, delete / swap, and the Rumble source follows its layer.
    void runLayerStructureTests()
    {
        using namespace mu_seam_test;
        using mu_on::toIndex;

        beginTest("a new track takes the first engine no live track uses, with that engine's groove and name");
        {
            TempStorage storage;
            ExposedPluginProcessor p;
            const int expectedEngines[] = { 0, 1, 2, 3, 4, 0, 0, 0 };   // Kick, Bass, Hat, Snare, Rumble, then Kick again
            for (int i = 1; i < mu_limits::kMaxLayers; ++i)
            {
                expectEquals(p.addLayer(), i, "addLayer appends");
                expectEquals(engineIndex(p, i), expectedEngines[i], "track " + juce::String(i) + " engine");
                expectEquals(p.getLayerName(i), juce::String(mu_on::engineName(mu_on::engineFromIndex(expectedEngines[i]))));
            }
            expectEquals(p.addLayer(), -1, "no ninth track");
            expect(p.pattern().isOn(1, 2) && ! p.pattern().isOn(1, 0), "the Bass track starts off the kick");
            expect(p.pattern().isOn(2, 0) && p.pattern().isOn(2, 2), "the Hat track starts on straight eighths");
            expect(p.pattern().isOn(3, 4) && p.pattern().isOn(3, 12), "the Snare track starts on the backbeat");
            bool anyOn = false;
            for (int s = 0; s < 16; ++s) anyOn = anyOn || p.pattern().isOn(4, s);
            expect(! anyOn, "a Rumble track has no steps");
            expectEquals(rumbleSource(p, 4), 0, "a new Rumble starts on the first Kick");
            expect(p.pattern().isOn(5, 0) && p.pattern().isOn(5, 4), "a repeated Kick gets the Kick groove");
        }

        beginTest("deleting and reordering moves the step row, the engine and the colour with the track; the Rumble source follows its layer");
        {
            TempStorage storage;
            ExposedPluginProcessor p;
            for (int i = 1; i < 5; ++i) p.addLayer();                       // Kick, Bass, Hat, Snare, Rumble
            const int hatColour = p.getLayerColourIndex(2);
            p.pattern().setOn(2, 7, true);                                  // a mark on the Hat row
            setParam(p.apvts, mu_on::layerParamId(4, "src"), 2.0f);         // the Rumble processes the Hat

            expect(p.removeLayer(1), "delete the Bass");                    // Kick, Hat, Snare, Rumble
            expectEquals(p.getNumLayers(), 4);
            expectEquals(engineIndex(p, 1), toIndex(EngineType::Hat), "the Hat moved down");
            expectEquals(engineIndex(p, 3), toIndex(EngineType::Rumble), "the Rumble moved down");
            expectEquals(rumbleSource(p, 3), 1, "the Rumble still processes the Hat");
            expect(p.pattern().isOn(1, 7), "the Hat's row moved with it");
            expectEquals(p.getLayerColourIndex(1), hatColour, "and its colour");
            expectEquals(p.getTrack(1).slot, 1, "the Track knows its new slot");

            expect(p.swapLayers(0, 1), "swap the Kick and the Hat");        // Hat, Kick, Snare, Rumble
            expectEquals(engineIndex(p, 0), toIndex(EngineType::Hat));
            expectEquals(rumbleSource(p, 3), 0, "the Rumble followed the Hat");
            expect(p.pattern().isOn(0, 7), "the row followed the swap");
            expect(! p.pattern().isOn(1, 7), "and left its old slot");

            expect(p.removeLayer(0), "delete the Hat, the Rumble's source");   // Kick, Snare, Rumble
            expectEquals(engineIndex(p, 2), toIndex(EngineType::Rumble));
            expectEquals(rumbleSource(p, 2), 0, "a deleted source becomes the first Kick");
            expect(p.getTrack(2).row.isOn(0) == p.pattern().isOn(2, 0), "the pattern reads the track's own row");
        }

        beginTest("switching engine keeps every engine's settings, the row and the old engine's modulation (inert until it runs again)");
        {
            TempStorage storage;
            ExposedPluginProcessor p;
            setParam(p.apvts, "t0_kick_tune", 77.0f);
            setParam(p.apvts, "t0_bass_cut", 1500.0f);
            ModulationAssignment a;
            a.id = "a1"; a.sourceId = "cs0_output"; a.destinationId = "k.tune.prop"; a.depth = 40.0f;
            expect(p.getTrack(0).modulationMatrix.addAssignment(a));

            p.selectTrackEngine(0, EngineType::Bass);
            expect(p.getTrackEngine(0) == EngineType::Bass);
            expectEquals(p.getLayerName(0), juce::String("Bass"));
            expectWithinAbsoluteError(getParam(p.apvts, "t0_kick_tune"), 77.0f, 0.05f);
            expectWithinAbsoluteError(getParam(p.apvts, "t0_bass_cut"), 1500.0f, 0.5f);
            expect(p.pattern().isOn(0, 4), "the row stays when the engine changes");
            expectEquals((int) p.getTrack(0).modulationMatrix.getAssignments().size(), 1, "a Kick assignment waits for the Kick to return");

            p.selectTrackEngine(0, EngineType::Rumble);
            expectEquals(rumbleSource(p, 0), 0, "a Rumble never keeps itself as its source");   // no other Kick exists, falls back to 0
        }

        beginTest("a track that becomes a Rumble releases the Rumbles that processed it");
        {
            TempStorage storage;
            ExposedPluginProcessor p;
            for (int i = 1; i < 5; ++i) p.addLayer();                       // Kick, Bass, Hat, Snare, Rumble (on the Kick)
            expectEquals(rumbleSource(p, 4), 0);
            p.selectTrackEngine(0, EngineType::Rumble);                          // the Kick turns into a Rumble
            expectEquals(rumbleSource(p, 4), 1, "the dependent moves to the first non-Rumble track");
            expectEquals(rumbleSource(p, 0), 1, "and the new Rumble does not process itself");
            p.selectTrackEngine(0, EngineType::Kick);
            p.selectTrackEngine(1, EngineType::Rumble);
            expectEquals(rumbleSource(p, 4), 0, "with a Kick available the dependent takes the first Kick");
        }

        beginTest("reset keeps the engine, the source and the steps, and puts the settings back");
        {
            TempStorage storage;
            ExposedPluginProcessor p;
            p.addLayer(); p.addLayer(); p.addLayer(); p.addLayer();
            setParam(p.apvts, "t4_src", 3.0f);
            setParam(p.apvts, "t4_rumble_drive", 0.9f);
            p.resetLayer(4);
            expectEquals(engineIndex(p, 4), toIndex(EngineType::Rumble), "the engine stays");
            expectEquals(rumbleSource(p, 4), 3, "the source stays");
            expectWithinAbsoluteError(getParam(p.apvts, "t4_rumble_drive"), 0.4f, 0.002f);
        }
    }

    // Add / remove / swap on this thread while a worker renders, at every block size.
    void runStressTest()
    {
        using namespace mu_seam_test;
        // A running host transport makes the grid sound (it is silent while stopped). It moves on by one
        // sixteenth per block whatever the block size, so every block starts exactly on a step edge and the
        // grid fires there; a transport that advanced by the block's real length would show a short block
        // an edge only now and then.
        struct StepPlayHead : juce::AudioPlayHead
        {
            juce::Optional<PositionInfo> getPosition() const override
            {
                PositionInfo info;
                info.setIsPlaying(true);
                info.setBpm(120.0);
                info.setPpqPosition(ppq);
                info.setTimeSignature(TimeSignature { 4, 4 });
                ppq += 0.25;
                return info;
            }
            mutable double ppq = 0.0;
        } playHead;
        TempStorage storage;
        ExposedPluginProcessor stress;
        stress.setPlayHead(&playHead);
        runStructuralEditStress(*this, stress,
            // Every track but a Rumble sounds at its engine's groove once the transport runs; a Rumble alone
            // has nothing to process (and no steps), so it is made a Kick with the Kick groove.
            [&](int layer)
            {
                if (stress.getTrackEngine(layer) != EngineType::Rumble) return;
                stress.selectTrackEngine(layer, EngineType::Kick);
                stress.pattern().loadDefaultGroove(layer, EngineType::Kick);
            },
            [&](const juce::String& where)
            {
                const int n = stress.numLayers.load();
                expect(n >= 1 && n <= mu_limits::kMaxLayers, where + ": count in range");
                std::set<const void*> distinct;
                for (int i = 0; i < mu_limits::kMaxLayers; ++i)
                {
                    expect(stress.layers[(size_t) i] != nullptr, where + ": slot " + juce::String(i) + " is filled");
                    distinct.insert(stress.layers[(size_t) i].get());
                    expectEquals(stress.getTrack(i).slot, i, where + ": track " + juce::String(i) + " knows its slot");

                    // The pattern reads each slot's Track row.
                    auto& row = stress.getTrack(i).row;
                    const bool before = row.isOn(15);
                    row.setOn(15, ! before);
                    expect(stress.pattern().isOn(i, 15) == ! before, where + ": slot " + juce::String(i) + " pattern row is the track's");
                    row.setOn(15, before);

                    const int src = rumbleSource(stress, i);
                    expect(src >= 0 && src < mu_limits::kMaxLayers, where + ": the Rumble source is a layer index");
                }
                expectEquals((int) distinct.size(), mu_limits::kMaxLayers, where + ": no layer appears twice");
            });
    }
};

static MuOnProcessorSeamTests muOnProcessorSeamTests;

// The layout word the shared layer driver reads follows every count store of the real processor (E1).
struct MuOnLayoutWordTests : juce::UnitTest
{
    MuOnLayoutWordTests() : juce::UnitTest("mu-On layout word follows the live count", "mu-on-processor") {}
    void runTest() override { mu_seam_test::checkLayoutWordFollowsCount<mu_on::PluginProcessor>(*this); }
};
static MuOnLayoutWordTests sMuOnLayoutWordTests;

// Per-layer step rows behind the global grid (extraction-plan E3.3): every row carries its own swing and
// accent, but the saved data stays the two global parameters, which seed every row when a state loads.
struct MuOnRowGrooveTests : juce::UnitTest
{
    MuOnRowGrooveTests() : juce::UnitTest("mu-On rows take the global groove", "mu-on-processor") {}
    void runTest() override
    {
        using namespace mu_seam_test;
        beginTest("seq_swing and seq_accent stay the saved values and seed every track's row on load, spares included");
        TempStorage storage;
        juce::MemoryBlock saved;
        {
            mu_on::PluginProcessor a;
            setParam(a.apvts, "seq_swing", 0.35f);
            setParam(a.apvts, "seq_accent", 0.55f);
            a.getStateInformation(saved);
            const auto xml = juce::AudioProcessor::getXmlFromBinary(saved.getData(), (int) saved.getSize());
            expect(xml != nullptr && ! xml->toString().contains("swing=\""), "a row saves no groove of its own");
        }
        mu_on::PluginProcessor b;
        for (int i = 0; i < mu_limits::kMaxLayers; ++i)
        {
            expectEquals(b.getTrack(i).row.swingAmount(), 0.0f, "a fresh row has the default swing");
            expectEquals(b.getTrack(i).row.accentVelocity(), 1.0f, "and the default accent");
        }
        b.setStateInformation(saved.getData(), (int) saved.getSize());
        for (int i = 0; i < mu_limits::kMaxLayers; ++i)
        {
            expectEquals(b.getTrack(i).row.swingAmount(), b.apvts.getRawParameterValue("seq_swing")->load(), "track " + juce::String(i) + " swing");
            expectEquals(b.getTrack(i).row.accentVelocity(), b.apvts.getRawParameterValue("seq_accent")->load(), "track " + juce::String(i) + " accent");
        }
        expectWithinAbsoluteError(b.getTrack(3).row.swingAmount(), 0.35f, 1.0e-6f);
        expectWithinAbsoluteError(b.getTrack(3).row.accentVelocity(), 0.55f, 1.0e-6f);
    }
};
static MuOnRowGrooveTests sMuOnRowGrooveTests;
