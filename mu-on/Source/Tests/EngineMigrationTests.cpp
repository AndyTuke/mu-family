// Data saved while mu-On had five fixed, instrument-named lanes loads as five layers that each run
// one engine: every lane's rows become engine-qualified ("tune" in the Kick lane -> t0_kick_tune),
// the engine and the Rumble source are written, the layer count and the lane colours are carried, and
// the step rows, Rumble envelope and modulators land on their layer. The fixtures are built the way
// the old build wrote them. Also the round trip of the current shape, and an old track preset.

#include "Plugin/PluginProcessor.h"
#include "Modulation/ModulatorSerialise.h"
#include "Tests/ProcessorSeamHelpers.h"

namespace
{
    using namespace mu_on;

    juce::ValueTree row(const char* id, double x, const char* choice = nullptr)
    {
        juce::ValueTree r(mu_pp::kRowTag);
        r.setProperty("id", id, nullptr);
        r.setProperty("x", x, nullptr);
        if (choice != nullptr) r.setProperty("c", choice, nullptr);
        return r;
    }

    juce::ValueTree stepRow(int i, const char* on, const char* accent)
    {
        juce::ValueTree r("Track");
        r.setProperty("i", i, nullptr);
        r.setProperty("on", on, nullptr);
        r.setProperty("accent", accent, nullptr);
        return r;
    }

    // The <Modulators> node of a layer with one assignment aimed at each of `dests`.
    juce::ValueTree modulatorsTo(std::initializer_list<const char*> dests)
    {
        Layer l;
        int n = 0;
        for (const char* dest : dests)
        {
            ModulationAssignment a;
            a.id = "a" + std::to_string(n++); a.sourceId = "cs0_output"; a.destinationId = dest; a.depth = 25.0f;
            l.modulationMatrix.addAssignment(a);
        }
        return mu_pp::serialiseModulators(l);
    }

    // A format-2 state as the five-lane build wrote it.
    juce::ValueTree legacyFormat2()
    {
        juce::ValueTree s("MuOnState");
        s.setProperty("format", 2, nullptr);

        juce::ValueTree globals(mu_pp::kGlobalsTag);
        globals.appendChild(row("ch1_scSrc", 1.0), nullptr);    // Bass ducks from the Kick, as that build pre-wired it
        globals.appendChild(row("ch1_scAmt", 0.4), nullptr);
        globals.appendChild(row("seq_swing", 0.25), nullptr);
        s.appendChild(globals, nullptr);

        juce::ValueTree slots(mu_pp::kLayersTag);
        auto slot = [&](int idx) { juce::ValueTree n(mu_pp::kLayerTag); n.setProperty("idx", idx, nullptr); slots.appendChild(n, nullptr); return n; };

        auto kick = slot(0);
        kick.appendChild(row("tune", 60.0), nullptr);
        kick.appendChild(row("drive", 0.7), nullptr);
        kick.appendChild(stepRow(0, "1000100010001000", "1000000000000000"), nullptr);

        auto bass = slot(1);
        bass.appendChild(row("wave", 2.0, "Square"), nullptr);
        bass.appendChild(row("cut", 1200.0), nullptr);
        bass.appendChild(row("tune", 55.0), nullptr);
        bass.appendChild(stepRow(1, "0010001000100010", "0000000000000000"), nullptr);

        auto hat = slot(2);
        hat.appendChild(row("dec", 100.0), nullptr);
        hat.appendChild(stepRow(2, "1010101010101010", "0000000000000000"), nullptr);
        hat.appendChild(modulatorsTo({ "h.dec.prop", "k.tune.prop" }), nullptr);   // the Kick destination is not a Hat one: dropped on load

        auto snare = slot(3);
        snare.appendChild(row("tune", 3.5), nullptr);
        snare.appendChild(stepRow(3, "0000100000001000", "0000000000000000"), nullptr);

        auto rumble = slot(4);
        rumble.appendChild(row("d1", 0.1), nullptr);
        rumble.appendChild(row("size", 0.9), nullptr);
        juce::ValueTree env("RumbleEnv");
        env.setProperty("loopMult", 8, nullptr);
        for (auto [x, y] : { std::pair<float, float>{ 0.0f, 1.0f }, { 0.5f, 0.1f }, { 1.0f, 1.0f } })
        {
            juce::ValueTree pt("P");
            pt.setProperty("x", x, nullptr);
            pt.setProperty("y", y, nullptr);
            env.addChild(pt, -1, nullptr);
        }
        rumble.appendChild(env, nullptr);

        s.appendChild(slots, nullptr);
        return s;
    }

    void load(PluginProcessor& p, const juce::ValueTree& state)
    {
        juce::MemoryBlock block;
        juce::AudioProcessor::copyXmlToBinary(*state.createXml(), block);
        p.setStateInformation(block.getData(), (int) block.getSize());
    }
}

struct MuOnEngineMigrationTests : juce::UnitTest
{
    MuOnEngineMigrationTests() : juce::UnitTest("mu-On engine migration", "mu-on-processor") {}

    void checkFiveLanes(PluginProcessor& p, const juce::String& what)
    {
        using namespace mu_seam_test;
        expectEquals(p.getNumLayers(), 5, what + ": five layers");

        // Layer i runs engine i, named for it, in the colour the lane had.
        const char* names[] = { "Kick", "Bass", "Hat", "Snare", "Rumble" };
        const int colours[] = { 0, 2, 1, 3, 4 };
        for (int i = 0; i < 5; ++i)
        {
            expectEquals((int) getParam(p.apvts, layerParamId(i, "eng")), i, what + ": engine of layer " + juce::String(i));
            expectEquals(p.getLayerName(i), juce::String(names[i]), what + ": name of layer " + juce::String(i));
            expectEquals(p.getLayerColourIndex(i), colours[i], what + ": colour of layer " + juce::String(i));
        }
        expectEquals((int) getParam(p.apvts, "t4_src"), 0, what + ": the Rumble processes the first Kick");

        // Every lane's rows landed on its engine-qualified parameters.
        expectWithinAbsoluteError(getParam(p.apvts, "t0_kick_tune"), 60.0f, 0.05f);
        expectWithinAbsoluteError(getParam(p.apvts, "t0_kick_drive"), 0.7f, 0.002f);
        expectEquals((int) getParam(p.apvts, "t1_bass_wave"), 2, what + ": a choice row");
        expectWithinAbsoluteError(getParam(p.apvts, "t1_bass_cut"), 1200.0f, 0.5f);
        expectWithinAbsoluteError(getParam(p.apvts, "t1_bass_tune"), 55.0f, 0.05f);   // Bass tune is not Kick tune
        expectWithinAbsoluteError(getParam(p.apvts, "t2_hat_dec"), 100.0f, 0.5f);
        expectWithinAbsoluteError(getParam(p.apvts, "t3_snare_tune"), 3.5f, 0.05f);
        expectWithinAbsoluteError(getParam(p.apvts, "t4_rumble_d1"), 0.1f, 0.002f);
        expectWithinAbsoluteError(getParam(p.apvts, "t4_rumble_size"), 0.9f, 0.002f);
        expectWithinAbsoluteError(getParam(p.apvts, "t0_kick_ptch"), 220.0f, 0.5f);   // untouched: default

        // Layers above the five are at their defaults.
        expectEquals((int) getParam(p.apvts, "t5_eng"), 0);
        expectWithinAbsoluteError(getParam(p.apvts, "t5_kick_tune"), 50.0f, 0.05f);
    }

    void runTest() override
    {
        using namespace mu_seam_test;

        beginTest("a five-lane format-2 state loads as five layers with the right engines, parameters, colours and source");
        {
            TempStorage storage;
            PluginProcessor p;
            load(p, legacyFormat2());
            checkFiveLanes(p, "format 2");

            // Saved mixer values are carried as saved; the pre-wired default is not applied on top of them.
            expectEquals((int) getParam(p.apvts, "ch1_scSrc"), 1, "the mixer sidechain source is as saved");
            expectWithinAbsoluteError(getParam(p.apvts, "ch1_scAmt"), 0.4f, 0.002f);
            expectWithinAbsoluteError(getParam(p.apvts, "seq_swing"), 0.25f, 0.002f);

            // Step rows and the Rumble envelope are on their layer.
            expect(p.pattern().isOn(0, 0) && p.pattern().isOn(0, 4) && ! p.pattern().isOn(0, 1) && p.pattern().isAccent(0, 0), "Kick row");
            expect(p.pattern().isOn(1, 2) && ! p.pattern().isOn(1, 0), "Bass row");
            expect(p.pattern().isOn(2, 0) && p.pattern().isOn(2, 2) && ! p.pattern().isOn(2, 1), "Hat row");
            expect(p.pattern().isOn(3, 4) && p.pattern().isOn(3, 12), "Snare row");
            expectEquals(p.getTrack(4).rumbleEnv.curvePoints.size(), (size_t) 3, "Rumble envelope points");
            expectEquals(p.getTrack(4).rumbleEnv.loopMultiplier, 8, "Rumble envelope length");
            expect(p.getTrack(0).rumbleEnvIsDefault(), "the other layers keep the default envelope");

            // The Hat lane's own modulator survives; layers the state did not have are empty.
            expectEquals((int) p.getTrack(2).modulationMatrix.getAssignments().size(), 1, "the Hat modulator kept");
            expect(p.getTrack(2).modulationMatrix.getAssignments().front().destinationId == "h.dec.prop");
            expect(p.getTrack(5).modulationMatrix.getAssignments().empty());
        }

        beginTest("a state saved by this build loads back unchanged (no second migration)");
        {
            TempStorage storage;
            PluginProcessor a;
            load(a, legacyFormat2());
            a.addLayer();                                       // a sixth layer: a Kick again
            setParam(a.apvts, "t5_kick_tune", 44.0f);
            juce::MemoryBlock block;
            a.getStateInformation(block);

            PluginProcessor b;
            b.setStateInformation(block.getData(), (int) block.getSize());
            expectEquals(b.getNumLayers(), 6, "six layers saved, six loaded");
            checkFiveLanesParams(b);
            expectEquals((int) getParam(b.apvts, "t5_eng"), 0);
            expectWithinAbsoluteError(getParam(b.apvts, "t5_kick_tune"), 44.0f, 0.05f);
            for (int i = 0; i < 6; ++i)
                expectEquals(b.getLayerColourIndex(i), a.getLayerColourIndex(i), "colour of layer " + juce::String(i));
        }

        beginTest("clips: a saved state carries each track's bank and playing clip, an old state loads empty, reset empties");
        {
            TempStorage storage;
            PluginProcessor a;
            load(a, legacyFormat2());
            juce::ValueTree payload("MuOnTrack");
            payload.setProperty("tag", 7, nullptr);
            a.getTrack(1).clips->store(2, payload, "fill");
            a.getTrack(1).activeClip = 2;
            juce::MemoryBlock block;
            a.getStateInformation(block);

            PluginProcessor b;
            b.getTrack(0).clips->store(6, payload);   // stale data the load must replace
            b.setStateInformation(block.getData(), (int) block.getSize());
            expect(b.getTrack(1).clips->get(2).isEquivalentTo(payload));
            expectEquals(b.getTrack(1).clips->getName(2), juce::String("fill"));
            expectEquals(b.getTrack(1).activeClip, 2);
            expect(b.getTrack(0).clips->isEmpty(), "the stale clip is gone");

            load(b, legacyFormat2());   // a state written before clips existed
            expect(b.getTrack(1).clips->isEmpty() && b.getTrack(1).activeClip == -1, "an old state has no clips");

            a.resetLayer(1);
            expect(a.getTrack(1).clips->isEmpty() && a.getTrack(1).activeClip == -1, "reset empties the bank");
        }

        beginTest("an APVTS dump with the old root <Pattern> and <RumbleEnv> (written before format 2) loads the same way");
        {
            // Old ids by lane type, the grid as one root <Pattern>, the envelope at the root.
            const char* dump = R"(<MuOnState>
  <PARAM id="k_tune" value="60.0"/>
  <PARAM id="b_wave" value="2.0"/>
  <PARAM id="b_tune" value="55.0"/>
  <PARAM id="h_dec" value="100.0"/>
  <PARAM id="s_tune" value="3.5"/>
  <PARAM id="r_d1" value="0.1000000014901161"/>
  <Pattern tracks="4" steps="16">
    <Track i="0" on="1000100010001000" accent="1000000000000000"/>
    <Track i="2" on="1010101010101010" accent="0000000000000000"/>
  </Pattern>
  <RumbleEnv loopMult="4"><P x="0.0" y="1.0"/><P x="1.0" y="0.3"/></RumbleEnv>
</MuOnState>)";
            TempStorage storage;
            PluginProcessor p;
            juce::MemoryBlock block;
            juce::AudioProcessor::copyXmlToBinary(*juce::XmlDocument::parse(dump), block);
            p.setStateInformation(block.getData(), (int) block.getSize());

            expectEquals(p.getNumLayers(), 5);
            expectWithinAbsoluteError(getParam(p.apvts, "t0_kick_tune"), 60.0f, 0.05f);
            expectEquals((int) getParam(p.apvts, "t1_bass_wave"), 2);
            expectWithinAbsoluteError(getParam(p.apvts, "t1_bass_tune"), 55.0f, 0.05f);
            expectWithinAbsoluteError(getParam(p.apvts, "t2_hat_dec"), 100.0f, 0.5f);
            expectWithinAbsoluteError(getParam(p.apvts, "t4_rumble_d1"), 0.1f, 0.002f);
            expect(p.pattern().isOn(0, 4) && p.pattern().isOn(2, 2), "the root pattern's rows moved to their layers");
            expectEquals(p.getTrack(4).rumbleEnv.curvePoints.size(), (size_t) 2, "the root envelope moved to the Rumble layer");
            expectEquals((int) getParam(p.apvts, "t4_eng"), 4);
        }

        beginTest("a track preset saved while the lanes were fixed loads into any track, engine included");
        {
            TempStorage storage;
            PluginProcessor p;
            p.addLayer();
            const char* preset = R"(<MuOnTrack lane="Bass">
  <p id="wave" x="1.0" c="Saw"/>
  <p id="cut" x="900.0"/>
  <Track i="1" on="0010001000100010" accent="0000000000000000"/>
</MuOnTrack>)";
            auto dir = p.getLayerPresetDir();
            dir.createDirectory();
            const auto file = dir.getChildFile("Old Bass.muTrack");
            file.replaceWithText(preset);

            expect(p.slotPresetFiles(0).contains(file), "every track lists the preset");
            p.loadLayerPreset(0, file);                           // track 0 is a Kick; the preset is a Bass
            expectEquals((int) getParam(p.apvts, "t0_eng"), 1, "the preset sets the engine");
            expectEquals(p.getLayerName(0), juce::String("Bass"));
            expectEquals((int) getParam(p.apvts, "t0_bass_wave"), 1);
            expectWithinAbsoluteError(getParam(p.apvts, "t0_bass_cut"), 900.0f, 0.5f);
            expect(p.pattern().isOn(0, 2) && ! p.pattern().isOn(0, 0), "the preset's row replaces the track's");
            expectWithinAbsoluteError(getParam(p.apvts, "t1_bass_cut"), 600.0f, 0.5f);   // the other track is untouched
        }

        beginTest("a track saved by this build round-trips through a preset file");
        {
            TempStorage storage;
            PluginProcessor p;
            p.addLayer();
            p.selectEngine(1, EngineType::Snare);
            setParam(p.apvts, "t1_snare_dec", 333.0f);
            p.saveLayerPreset(1, "Round Trip");
            const auto files = p.slotPresetFiles(0);
            expectEquals(files.size(), 1);

            p.selectEngine(0, EngineType::Hat);
            p.loadLayerPreset(0, files.getFirst());
            expectEquals((int) getParam(p.apvts, "t0_eng"), 3, "the Snare engine came back");
            expectWithinAbsoluteError(getParam(p.apvts, "t0_snare_dec"), 333.0f, 0.5f);
        }
    }

    // The migrated five layers' sound parameters (the same values the fixture holds).
    void checkFiveLanesParams(PluginProcessor& p)
    {
        using namespace mu_seam_test;
        expectWithinAbsoluteError(getParam(p.apvts, "t0_kick_tune"), 60.0f, 0.05f);
        expectEquals((int) getParam(p.apvts, "t1_bass_wave"), 2);
        expectWithinAbsoluteError(getParam(p.apvts, "t1_bass_tune"), 55.0f, 0.05f);
        expectWithinAbsoluteError(getParam(p.apvts, "t2_hat_dec"), 100.0f, 0.5f);
        expectWithinAbsoluteError(getParam(p.apvts, "t4_rumble_size"), 0.9f, 0.002f);
        for (int i = 0; i < 5; ++i) expectEquals((int) getParam(p.apvts, layerParamId(i, "eng")), i);
        expect(p.pattern().isOn(1, 2), "steps survive the round trip");
    }
};

static MuOnEngineMigrationTests muOnEngineMigrationTests;
