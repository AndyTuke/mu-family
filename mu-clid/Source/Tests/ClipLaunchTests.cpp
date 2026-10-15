// Clip bank on the REAL mu-Clid PluginProcessor, built headless: store / launch through the layer-preset
// hooks over PresetIO (the same checks the composed products run in mu-core's ClipLaunchChecks.h, which
// needs a LayerSwapper mu-Clid does not have), the bank surviving every Rhythm install and structural
// edit, sample-path-only clips, and <Clips> in full presets and sessions. The transport state is set
// directly (sequencerPlaying), so every case is deterministic.

#include "Plugin/PluginProcessor.h"
#include "Plugin/HotSwapStager.h"
#include "Tests/ProcessorSeamHelpers.h"

namespace mu_clid
{
    // Drives the boundary scan and the commit pass of the processor's own stager, which the audio
    // thread and the swap poll timer do on a host.
    struct ClipCommitProbe
    {
        static bool scan(PluginProcessor& p) { return p.hotSwapStager.checkBoundaries(p.getNumRhythms(), true, 0xff); }
        static void commit(PluginProcessor& p) { p.hotSwapStager.processSwaps(); }
    };
}

namespace
{
    using namespace mu_seam_test;
    using Probe = mu_clid::ClipCommitProbe;

    // The real processor with the MIDI program-change entry exposed.
    struct ExposedProcessor : mu_clid::PluginProcessor
    {
        using mu_clid::PluginProcessor::applyMidiPresetLayer;
    };

    // A processor whose layer-preset stage can be made to abort without staging, as a failed load would.
    struct RefusingProcessor : mu_clid::PluginProcessor
    {
        bool refuse = false;
        void stageLayerPresetTree(int slot, juce::ValueTree tree, bool keepIdentity) override
        {
            if (! refuse) mu_clid::PluginProcessor::stageLayerPresetTree(slot, std::move(tree), keepIdentity);
        }
    };

    const char* const kParam = "r0_hitsA";

    juce::File presetsDir()
    {
        auto dir = juce::File::getSpecialLocation(juce::File::currentExecutableFile).getParentDirectory();
        for (int i = 0; i < 10 && dir.exists(); ++i, dir = dir.getParentDirectory())
            if (dir.getChildFile("tests").getChildFile("presets").isDirectory())
                return dir.getChildFile("tests").getChildFile("presets");
        return {};
    }

    // A short sine in the processor's Samples folder, so a rhythm can play a real file.
    juce::File writeSample(mu_clid::PluginProcessor& p, const juce::String& name)
    {
        const auto f = p.getSamplesDir().getChildFile(name);
        f.getParentDirectory().createDirectory();
        juce::AudioBuffer<float> buf(1, 2205);
        for (int i = 0; i < buf.getNumSamples(); ++i)
            buf.setSample(0, i, 0.3f * std::sin(0.05f * (float) i));
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> out = f.createOutputStream();
        if (out != nullptr)
            if (auto w = wav.createWriterFor(out, juce::AudioFormatWriterOptions{}.withSampleRate(44100.0)
                                                                                 .withNumChannels(1)
                                                                                 .withBitsPerSample(16)))
                w->writeFromAudioSampleBuffer(buf, 0, buf.getNumSamples());   // the writer flushes when it goes out of scope
        return f;
    }

    bool sameBank(const mu_core::ClipBank& a, const mu_core::ClipBank& b)
    {
        for (int i = 0; i < mu_core::ClipBank::kCount; ++i)
        {
            if (a.isOccupied(i) != b.isOccupied(i) || a.getName(i) != b.getName(i)) return false;
            if (a.isOccupied(i) && ! a.get(i).isEquivalentTo(b.get(i))) return false;
        }
        return true;
    }

    juce::String xmlOf(mu_clid::PluginProcessor& p, const juce::File& f)
    {
        p.saveFullPresetTo(f);
        return f.loadFileAsString();
    }
}

struct MuClidClipLaunchTests : juce::UnitTest
{
    MuClidClipLaunchTests() : juce::UnitTest("mu-Clid clip bank", "mu-clid-processor") {}

    void runTest() override
    {
        storeAndLaunch();
        clipsHoldDifferentPresets();
        dropdownThenStripGestures();
        samplePathOnly();
        launchEqualsFileLoad();
        nonClipLoadClearsActiveClip();
        rejectionsAndGuards();
        launchWhilePlaying();
        cancelledLaunch();
        liveClips();
        launchThatCannotBeBuilt();
        bankFollowsStructuralEdits();
        fullPresetAndSessionRoundTrip();
        stagedFullPresetCarriesBanks();
        oldFilesLoadEmpty();
        clipNamesFollowPresets();
    }

    // A clip is named after the preset it was stored from; the live view carries the names with the queue.
    void clipNamesFollowPresets()
    {
        beginTest("clip names: the loaded file's stem, else the rhythm's name; published to the view, kept on re-store and in full presets");
        const auto dir = presetsDir();
        const auto fileA = dir.getChildFile("T11.muRhythm");
        const auto fileB = dir.getChildFile("T13.muRhythm");
        expect(fileA.existsAsFile() && fileB.existsAsFile());

        using Type = mu_core::ControlActionType;
        const auto act = [](Type type, int index)
        {
            mu_core::ControlAction a;
            a.type = type; a.layer = 0; a.index = index;
            return a;
        };

        TempStorage storage;
        mu_clid::PluginProcessor p;
        const auto* view = p.getLiveView();
        p.getRhythm(0).name = "Kick";

        expect(p.perform(act(Type::StoreClip, 0)), "stored with no preset loaded");
        expectEquals(view->getClipName(0, 0), juce::String("Kick"), "falls back to the rhythm's name");

        p.stageRhythmPreset(0, fileA);
        expect(p.perform(act(Type::StoreClip, 1)));
        expectEquals(view->getClipName(0, 1), fileA.getFileNameWithoutExtension(), "the loaded file's stem");
        p.stageRhythmPreset(0, fileB);
        expect(p.perform(act(Type::StoreClip, 2)));
        expectEquals(view->getClipName(0, 2), fileB.getFileNameWithoutExtension());
        expectEquals(view->getClipName(0, 3), juce::String(), "an empty pad has no name");

        // Launch a clip, then re-store it after a tweak: it keeps its own name, not the layer's.
        expect(p.perform(act(Type::LaunchClip, 1)));
        expectEquals(view->getPlayingClip(0), 1);
        setParam(p.apvts, kParam, 9.0f);
        expect(p.perform(act(Type::StoreClip, 1)));
        expectEquals(view->getClipName(0, 1), fileA.getFileNameWithoutExtension(), "re-storing the playing clip keeps its name");

        // While playing, a launch is queued and its name is already published.
        p.sequencerPlaying.store(true);
        expect(p.perform(act(Type::LaunchClip, 2)));
        expectEquals(view->getQueuedClip(0), 2);
        expectEquals(view->getClipName(0, view->getQueuedClip(0)), fileB.getFileNameWithoutExtension());
        Probe::scan(p); Probe::commit(p);
        p.getLiveState().reconcile();
        expectEquals(view->getPlayingClip(0), 2);
        p.sequencerPlaying.store(false);

        // The names are part of the bank, so they survive a full-preset save and load.
        const auto full = storage.root.getChildFile("names.muClid");
        p.saveFullPresetTo(full);
        mu_clid::PluginProcessor q;
        q.loadPreset(full);
        expectEquals(q.getLiveView()->getClipName(0, 0), juce::String("Kick"));
        expectEquals(q.getLiveView()->getClipName(0, 1), fileA.getFileNameWithoutExtension());
        expectEquals(q.getLiveView()->getClipName(0, 2), fileB.getFileNameWithoutExtension());

        // A file load labels its layer; a reset forgets the label.
        p.addLayer();
        p.stageRhythmPreset(1, fileA);
        expectEquals(p.clipLabel(1), fileA.getFileNameWithoutExtension());
        p.resetLayer(1);
        expectEquals(p.clipLabel(1), p.getLayerName(1), "reset forgets the file");
    }

    void storeAndLaunch()
    {
        beginTest("storeClip, change, launchClip (stopped) restores the rhythm; bank, name and colour untouched");
        TempStorage storage;
        mu_clid::PluginProcessor p;
        p.getRhythm(0).name = "keep";
        p.getRhythm(0).colourIndex = 5;
        setParam(p.apvts, kParam, 3.0f);
        const auto before = p.captureLayerPresetTree(0);
        expect(before.isValid());
        expect(p.storeClip(0, 3, "A"));
        expectEquals((int) p.clipMask(0), 1 << 3);

        setParam(p.apvts, kParam, 7.0f);
        const auto stored = p.getLayer(0)->clips->get(3);
        expect(p.launchClip(0, 3));
        expectWithinAbsoluteError(getParam(p.apvts, kParam), 3.0f, 0.01f);
        expect(p.captureLayerPresetTree(0).isEquivalentTo(before), "capture then stage round-trips the rhythm");
        expect(p.getLayer(0)->clips->get(3).isEquivalentTo(stored), "the launch did not write the bank");
        expectEquals(p.getLayer(0)->clips->getName(3), juce::String("A"));
        expect(p.getRhythm(0).name == "keep" && p.getRhythm(0).colourIndex == 5, "identity untouched");

        // A clip is the rhythm's layer-preset tree without file metadata, identity or mixer strip.
        expect(! before.hasProperty("r0_name") && ! before.hasProperty("r0_colour") && ! before.hasProperty("presetName"));
        bool hasStrip = false;
        for (int i = 0; i < before.getNumProperties(); ++i)
            hasStrip = hasStrip || before.getPropertyName(i).toString().startsWith("ch_");
        expect(! hasStrip, "no mixer strip");

        // The bank survives every install of a fresh Rhythm (SlotBound), a direct load included.
        const auto file = storage.root.getChildFile("any.muRhythm");
        p.saveRhythmPresetToFile(0, file);
        expect(p.applyRhythmPreset(file, 0));
        expectEquals((int) p.clipMask(0), 1 << 3, "a direct layer-preset load leaves the bank");
    }

    void clipsHoldDifferentPresets()
    {
        beginTest("layer presets loaded then stored into different clips stay different, stopped and playing");
        TempStorage storage;
        mu_clid::PluginProcessor p;
        setParam(p.apvts, kParam, 3.0f);  p.saveLayerPreset(0, "presetA");
        setParam(p.apvts, kParam, 7.0f);  p.saveLayerPreset(0, "presetB");
        juce::File fileA, fileB;
        for (const auto& f : p.slotPresetFiles(0))
        {
            if (f.getFileNameWithoutExtension() == "presetA") fileA = f;
            if (f.getFileNameWithoutExtension() == "presetB") fileB = f;
        }
        expect(fileA.existsAsFile() && fileB.existsAsFile());

        for (const bool playing : { false, true })
        {
            p.clearClip(0, 0); p.clearClip(0, 1);
            p.sequencerPlaying.store(playing);

            // Each load is followed at once by a store, as the strip does; a playing load is still pending then.
            p.loadLayerPreset(0, fileA);
            expect(p.storeClip(0, 0), "store A");
            p.loadLayerPreset(0, fileB);
            expect(p.storeClip(0, 1), "store B");
            expect(! p.getLayer(0)->clips->get(0).isEquivalentTo(p.getLayer(0)->clips->get(1)), "the two clips differ");
            expectWithinAbsoluteError((float) (double) p.getLayer(0)->clips->get(0).getProperty(kParam), 3.0f, 0.01f);
            expectWithinAbsoluteError((float) (double) p.getLayer(0)->clips->get(1).getProperty(kParam), 7.0f, 0.01f);

            // The stored preset still lands, and launching each clip shows its own state.
            if (playing)
            {
                expect(p.hasPendingSwap(0), "the store left the chosen preset staged");
                Probe::scan(p); Probe::commit(p);
            }
            expectWithinAbsoluteError(getParam(p.apvts, kParam), 7.0f, 0.01f);
            for (const int clip : { 0, 1 })
            {
                expect(p.launchClip(0, clip));
                if (playing) { Probe::scan(p); Probe::commit(p); }
                expectWithinAbsoluteError(getParam(p.apvts, kParam), clip == 0 ? 3.0f : 7.0f, 0.01f,
                                          "launch clip " + juce::String(clip));
            }
        }
        p.sequencerPlaying.store(false);
    }

    // The entry points the layer header's preset dropdown and the clip strip call, with real rhythm files that
    // differ in their pattern: choose (stageRhythmPreset) -> store pad (StoreClip action) for each, then launch
    // each pad (LaunchClip action) and check the live Rhythm.
    void dropdownThenStripGestures()
    {
        beginTest("dropdown preset then strip store, per pad: banks differ and each launch shows its own rhythm, stopped and playing");
        const auto dir = presetsDir();
        const juce::File files[] = { dir.getChildFile("T11.muRhythm"), dir.getChildFile("T13.muRhythm"),
                                     dir.getChildFile("A9_hat.muRhythm") };
        struct Shape { int steps, hits, rot; juce::String sample; };
        Shape want[3];
        for (int i = 0; i < 3; ++i)
        {
            expect(files[i].existsAsFile(), files[i].getFullPathName());
            const auto t = juce::ValueTree::fromXml(files[i].loadFileAsString());
            want[i] = { (int) t.getProperty("r0_stepsA"), (int) t.getProperty("r0_hitsA"), (int) t.getProperty("r0_rotA"),
                        t.getProperty("r0_sample").toString() };
        }
        expect(want[0].hits != want[1].hits, "the fixtures differ in hits");

        using Type = mu_core::ControlActionType;
        const auto act = [](Type type, int index)
        {
            mu_core::ControlAction a;
            a.type = type; a.layer = 0; a.index = index;
            return a;
        };

        for (const bool playing : { false, true })
        {
            TempStorage storage;
            mu_clid::PluginProcessor p;
            p.sequencerPlaying.store(playing);
            const auto* view = p.getLiveView();
            std::vector<int> rebinds;   // what the editor re-binds its panel on
            p.onLayerPresetCommitted = [&rebinds](int slot) { rebinds.push_back(slot); };
            const auto settle = [&]
            {
                if (playing) { Probe::scan(p); Probe::commit(p); }
                p.getLiveState().reconcile();
            };

            for (int i = 0; i < 3; ++i)
            {
                p.stageRhythmPreset(0, files[i]);                       // the dropdown's call
                expect(p.perform(act(Type::StoreClip, i)), "store pad " + juce::String(i));
            }
            settle();

            const auto stateOf = [&](int i)
            {
                const auto t = p.getLayer(0)->clips->get(i);
                return juce::String((int) t.getProperty("r0_stepsA")) + "/" + juce::String((int) t.getProperty("r0_hitsA"))
                     + "/" + juce::String((int) t.getProperty("r0_rotA")) + "/" + t.getProperty("r0_sample").toString();
            };
            for (int i = 0; i < 3; ++i)
                for (int j = i + 1; j < 3; ++j)
                    expect(stateOf(i) != stateOf(j), "bank payloads " + juce::String(i) + " and " + juce::String(j) + " differ: "
                                                       + stateOf(i) + " vs " + stateOf(j));

            // Launch in an order that starts away from the pad the last store left playing.
            for (const int pad : { 0, 1, 0, 2, 1 })
            {
                const auto label = juce::String(playing ? "playing" : "stopped") + ", pad " + juce::String(pad);
                rebinds.clear();
                expect(p.perform(act(Type::LaunchClip, pad)), "launch pad " + juce::String(pad));
                settle();
                expect(rebinds == std::vector<int> { 0 }, label + ": the editor is told to re-bind exactly once");
                const auto& g = p.getRhythm(0).genA;
                expectEquals(g.hits, want[pad].hits, label + " hits");
                expectEquals(g.steps, want[pad].steps, label + " steps");
                expectEquals(g.rotate, want[pad].rot, label + " rotate");
                expectEquals(view->getPlayingClip(0), pad, label + " lamp");
                expectWithinAbsoluteError(getParam(p.apvts, kParam), (float) want[pad].hits, 0.01f, label + " APVTS");
                expect(juce::File(p.samples.path(0)).getFileName() == want[pad].sample, label + " sample " + p.samples.path(0));
            }
        }
    }

    void samplePathOnly()
    {
        beginTest("a stored clip holds the sample path and no audio; launching it reloads that sample");
        TempStorage storage;
        mu_clid::PluginProcessor p;
        const auto kick = writeSample(p, "clip_kick.wav");
        const auto snare = writeSample(p, "clip_snare.wav");
        p.samples.load(0, kick);
        expect(p.storeClip(0, 0));
        const auto tree = p.getLayer(0)->clips->get(0);
        expectEquals(tree.getProperty("r0_sample").toString(), juce::String("clip_kick.wav"), "a path inside Samples is stored relative");
        expect(! tree.hasProperty("sampleData") && ! tree.hasProperty("sampleName"), "no embedded audio");

        p.samples.load(0, snare);
        expectEquals(p.samples.path(0), snare.getFullPathName());
        expect(p.launchClip(0, 0));
        expectEquals(p.samples.path(0), kick.getFullPathName(), "launch reloads the stored sample by path");

        // A preset embedding its samples does not put audio into the clips.
        p.savePreset("embed", {}, {}, true);
        const auto saved = p.getPresetsDir().getChildFile("embed.muClid").loadFileAsString();
        expect(saved.contains("<Clips>"));
        const auto root = juce::ValueTree::fromXml(saved);
        const auto clip = root.getChildWithName("Rhythm").getChildWithName("Clips").getChild(0).getChild(0);
        expect(clip.isValid() && ! clip.hasProperty("sampleData"), "the clip stays path-only inside an embedding preset");
        expect(root.getChildWithName("Rhythm").hasProperty("sampleData"), "while the rhythm itself embeds");
    }

    void launchEqualsFileLoad()
    {
        beginTest("a clip loaded from a .muRhythm launches exactly as loading that file, keeping name and colour");
        TempStorage storage;
        mu_clid::PluginProcessor src;
        setParam(src.apvts, kParam, 3.0f);
        src.saveLayerPreset(0, "clipsrc");
        const auto files = src.slotPresetFiles(0);
        expectEquals(files.size(), 1);

        mu_clid::PluginProcessor viaClip, viaLoad;
        viaClip.getRhythm(0).name = "mine";
        viaClip.getRhythm(0).colourIndex = 4;
        setParam(viaClip.apvts, kParam, 7.0f);
        setParam(viaLoad.apvts, kParam, 7.0f);
        expect(viaClip.loadClipFromFile(0, 6, files.getFirst()));
        expect(viaClip.launchClip(0, 6));
        viaLoad.loadLayerPreset(0, files.getFirst());
        expectWithinAbsoluteError(getParam(viaClip.apvts, kParam), 3.0f, 0.01f);
        expect(viaClip.captureLayerPresetTree(0).isEquivalentTo(viaLoad.captureLayerPresetTree(0)),
               "the launch and the direct load leave the same rhythm");
        expect(viaClip.getRhythm(0).name == "mine" && viaClip.getRhythm(0).colourIndex == 4, "a clip keeps the layer's identity");
        expect(viaLoad.getRhythm(0).name != "mine", "a file load takes the preset's name");
    }

    void nonClipLoadClearsActiveClip()
    {
        beginTest("every non-clip load path clears activeClip; a clip launch keeps it; the bank stays");
        TempStorage storage;
        ExposedProcessor p;
        p.saveLayerPreset(0, "plain");
        const auto file = p.slotPresetFiles(0).getFirst();
        p.storeClip(0, 1);
        p.storeClip(0, 6);

        p.getLayer(0)->activeClip = 6;
        expect(p.launchClip(0, 1));
        expectEquals(p.getLayer(0)->activeClip, 6, "a launch leaves activeClip to the performance state");

        p.getLayer(0)->activeClip = 6;
        p.loadLayerPreset(0, file);
        expectEquals(p.getLayer(0)->activeClip, -1, "loadLayerPreset");

        p.getLayer(0)->activeClip = 6;
        p.stageRhythmPreset(0, file);
        expectEquals(p.getLayer(0)->activeClip, -1, "stageRhythmPreset");

        p.getLayer(0)->activeClip = 6;
        p.applyRhythmPreset(file, 0);
        expectEquals(p.getLayer(0)->activeClip, -1, "applyRhythmPreset");

        p.getLayer(0)->activeClip = 6;
        p.applyMidiPresetLayer(0, file);
        expectEquals(p.getLayer(0)->activeClip, -1, "MIDI program change");

        p.getLayer(0)->activeClip = 6;
        p.sequencerPlaying.store(true);
        p.stageRhythmPreset(0, file);   // staged for the boundary: the live rhythm already shows no clip
        p.sequencerPlaying.store(false);
        expectEquals(p.getLayer(0)->activeClip, -1, "a staged load");
        expectEquals((int) p.clipMask(0), (1 << 1) | (1 << 6), "none of them touched the bank");
    }

    void rejectionsAndGuards()
    {
        beginTest("loadClipFromFile rejects a wrong tag and an unreadable file; bad index, empty clip and a staged full preset fail");
        TempStorage storage;
        mu_clid::PluginProcessor p;
        p.storeClip(0, 1, "kept");
        juce::StringArray errors;
        p.onLoadError = [&errors](const juce::String& e) { errors.add(e); };

        auto dir = storage.root.getChildFile("clipfiles");
        dir.createDirectory();
        const auto wrong = dir.getChildFile("wrong.xml");
        wrong.replaceWithText("<NotThisProduct/>");
        const auto junk = dir.getChildFile("junk.xml");
        junk.replaceWithText("not xml");
        expect(! p.loadClipFromFile(0, 1, wrong));
        expect(! p.loadClipFromFile(0, 1, junk));
        expect(! p.loadClipFromFile(0, 1, dir.getChildFile("missing.xml")));
        expectEquals(errors.size(), 3);
        expectEquals((int) p.clipMask(0), 1 << 1);
        expectEquals(p.getLayer(0)->clips->getName(1), juce::String("kept"));

        expect(! p.launchClip(0, 5), "an empty clip");
        expect(! p.launchClip(0, 8) && ! p.launchClip(0, -1) && ! p.storeClip(0, 8));
        expect(! p.launchClip(3, 1) && ! p.storeClip(3, 1), "a layer that does not exist");

        const auto full = storage.root.getChildFile("guard.muClid");
        p.saveFullPresetTo(full);
        setParam(p.apvts, kParam, 7.0f);
        p.sequencerPlaying.store(true);
        p.loadPreset(full);
        expect(p.hasPendingFullPreset());
        expect(! p.launchClip(0, 1), "a full preset is staged");
        expect(! p.hasPendingSwap(0));
        p.sequencerPlaying.store(false);
        expect(p.clearClip(0, 1));
        expect(! p.clearClip(0, 8), "a bad index");
    }

    void launchWhilePlaying()
    {
        beginTest("launch while playing is pending and commits at the boundary, firing onLayerPresetCommitted; the bank and activeClip survive");
        TempStorage storage;
        mu_clid::PluginProcessor p;
        setParam(p.apvts, kParam, 3.0f);
        p.storeClip(0, 2, "two");
        setParam(p.apvts, kParam, 7.0f);
        p.getLayer(0)->activeClip = 2;
        const auto bank = *p.getLayer(0)->clips;
        int committed = -1;
        p.onLayerPresetCommitted = [&committed](int s) { committed = s; };

        p.sequencerPlaying.store(true);
        expect(p.launchClip(0, 2));
        expect(p.hasPendingSwap(0));
        expectWithinAbsoluteError(getParam(p.apvts, kParam), 7.0f, 0.01f);
        expect(Probe::scan(p), "the boundary flags it");
        Probe::commit(p);
        expect(! p.hasPendingSwap(0));
        expectWithinAbsoluteError(getParam(p.apvts, kParam), 3.0f, 0.01f);
        expectEquals(committed, 0);
        expect(sameBank(*p.getLayer(0)->clips, bank), "the commit left the bank");
        expectEquals(p.getLayer(0)->activeClip, 2, "and the slot's activeClip");
        p.sequencerPlaying.store(false);
    }

    // The live state over mu-Clid's own stager: pad actions launch, the commit lands them, a load or a
    // full preset settles the lamps. Performance mode is standalone-only, so the processor is built as one.
    void liveClips()
    {
        beginTest("live clips: a pad launch plays at once when stopped, queues and lands through mu-Clid's stager, and a load or full preset settles it");
        TempStorage storage;
        juce::AudioProcessor::setTypeOfNextNewPlugin(juce::AudioProcessor::wrapperType_Standalone);
        mu_clid::PluginProcessor p;
        juce::AudioProcessor::setTypeOfNextNewPlugin(juce::AudioProcessor::wrapperType_Undefined);

        using Type = mu_core::ControlActionType;
        const auto act = [](Type type, int layer = 0, int index = -1)
        {
            mu_core::ControlAction a;
            a.type = type; a.layer = layer; a.index = index;
            return a;
        };
        mu_pp::PerformanceFile list;
        list.setItem(0, p.getProductStateId(), storage.root.getChildFile("p.preset"), storage.root, "P");
        p.loadPerformance(list, storage.root.getChildFile("live.muPerformance"));
        expect(p.perform(act(Type::PerformanceMode, 0, 1)), "Performance mode is on");
        const auto* view = p.getLiveView();

        setParam(p.apvts, kParam, 3.0f); p.storeClip(0, 1, "A");
        setParam(p.apvts, kParam, 7.0f); p.storeClip(0, 2, "B");
        expectEquals((int) view->getClipMask(0), 0b110, "storing publishes the mask");

        expect(! p.perform(act(Type::LaunchClip, 0, 5)), "an empty pad does nothing");
        expect(p.perform(act(Type::LaunchClip, 0, 1)));
        expectEquals(view->getPlayingClip(0), 1, "stopped: playing at once");
        expectEquals(p.getLayer(0)->activeClip, 1, "recorded with the rhythm for the session");
        expectWithinAbsoluteError(getParam(p.apvts, kParam), 3.0f, 0.01f);

        p.sequencerPlaying.store(true);
        expect(p.perform(act(Type::LaunchClip, 0, 2)));
        expectEquals(view->getQueuedClip(0), 2);
        expectEquals(view->getPlayingClip(0), 1, "playing moves only at the commit");
        expect(p.hasPendingSwap(0));
        expect(Probe::scan(p), "the boundary flags it");
        Probe::commit(p);
        p.getLiveState().reconcile();
        expectEquals(view->getPlayingClip(0), 2);
        expectEquals(view->getQueuedClip(0), -1);
        expectEquals(p.getLayer(0)->activeClip, 2);
        expectWithinAbsoluteError(getParam(p.apvts, kParam), 7.0f, 0.01f);

        // The playing pad mutes and unmutes; with another clip queued the first press cancels the queue.
        expect(p.perform(act(Type::LaunchClip, 0, 2)));
        expect(view->isLayerMuted(0));
        expect(p.perform(act(Type::LaunchClip, 0, 2)));
        expect(! view->isLayerMuted(0));
        p.perform(act(Type::LaunchClip, 0, 1));
        expectEquals(view->getQueuedClip(0), 1);
        expect(p.perform(act(Type::LaunchClip, 0, 2)));
        expectEquals(view->getQueuedClip(0), -1, "cancelled");
        expect(! p.hasPendingSwap(0));
        expect(! view->isLayerMuted(0), "and not muted");

        // A rhythm preset loaded over a queued clip supersedes it and leaves no clip playing.
        setParam(p.apvts, kParam, 9.0f);
        p.saveLayerPreset(0, "later");
        const auto file = p.slotPresetFiles(0).getFirst();
        p.perform(act(Type::LaunchClip, 0, 1));
        expectEquals(view->getQueuedClip(0), 1);
        p.loadLayerPreset(0, file);
        expectEquals(view->getQueuedClip(0), -1);
        expectEquals(view->getPlayingClip(0), -1);
        expectEquals(p.getLayer(0)->activeClip, -1);
        Probe::scan(p); Probe::commit(p); p.getLiveState().reconcile();
        expectEquals(view->getPlayingClip(0), -1, "the load that landed is not a clip");

        // A cancel clears the lamp; clearing the queued clip cancels its launch.
        p.perform(act(Type::LaunchClip, 0, 2));
        p.cancelPendingSwap(0);
        p.getLiveState().reconcile();
        expectEquals(view->getQueuedClip(0), -1);
        p.perform(act(Type::LaunchClip, 0, 1));
        expect(p.clearClip(0, 1));
        expectEquals(view->getQueuedClip(0), -1);
        expect(! p.hasPendingSwap(0));
        expectEquals((int) view->getClipMask(0), 0b100);
        p.sequencerPlaying.store(false);

        // A full preset applied while stopped replaces the layers: no pad plays afterwards, the bank is the preset's.
        p.storeClip(0, 1, "A");
        expect(p.perform(act(Type::LaunchClip, 0, 1)));
        expectEquals(view->getPlayingClip(0), 1);
        const auto full = storage.root.getChildFile("live.muClid");
        p.saveFullPresetTo(full);                   // saved with clip 1 playing
        expect(p.perform(act(Type::LaunchClip, 0, 2)));
        expectEquals(view->getPlayingClip(0), 2);
        p.loadPreset(full);
        p.getLiveState().reconcile();
        expectEquals(view->getPlayingClip(0), 1, "a full preset replaces the playing clip with the one it saved");
        expectEquals(p.getLayer(0)->activeClip, 1);
        expectEquals((int) view->getClipMask(0), 0b110, "and publishes its own bank");

        // A staged full preset that is cancelled from outside the live state never replaced the layers.
        expect(p.perform(act(Type::LaunchClip, 0, 2)));
        p.sequencerPlaying.store(true);
        p.loadPreset(full);
        expect(p.hasPendingFullPreset());
        p.getLiveState().reconcile();
        p.cancelPendingFullPreset();
        p.getLiveState().reconcile();
        expectEquals(view->getPlayingClip(0), 2, "a cancel leaves the playing clip alone");

        // Staged again, it lands at the boundary: mu-Clid's own commit path moves the count.
        p.loadPreset(full);
        expect(Probe::scan(p));
        Probe::commit(p);
        p.sequencerPlaying.store(false);
        expectEquals(view->getPlayingClip(0), 1, "a full preset landing drops the clip state and shows the saved clip");

        // A strip mute made outside the pad reaches the lamps with no other event.
        struct Counter : mu_core::LiveView::Listener
        {
            int calls = 0;
            void liveStateChanged() override { ++calls; }
        } counter;
        view->addListener(&counter);
        p.apvts.getParameter("ch0_mute")->setValueNotifyingHost(1.0f);
        p.flushPendingAsyncUpdates();
        expect(counter.calls > 0, "the lamps follow a mute made on the strip");
        expect(view->isLayerMuted(0));
        view->removeListener(&counter);
    }

    void launchThatCannotBeBuilt()
    {
        beginTest("a launch whose stage hook staged nothing is refused: no queue, no playing clip, nothing staged");
        TempStorage storage;
        juce::AudioProcessor::setTypeOfNextNewPlugin(juce::AudioProcessor::wrapperType_Standalone);
        RefusingProcessor p;
        juce::AudioProcessor::setTypeOfNextNewPlugin(juce::AudioProcessor::wrapperType_Undefined);

        using Type = mu_core::ControlActionType;
        const auto act = [](Type type, int layer = 0, int index = -1)
        {
            mu_core::ControlAction a;
            a.type = type; a.layer = layer; a.index = index;
            return a;
        };
        mu_pp::PerformanceFile list;
        list.setItem(0, p.getProductStateId(), storage.root.getChildFile("p.preset"), storage.root, "P");
        p.loadPerformance(list, storage.root.getChildFile("live.muPerformance"));
        expect(p.perform(act(Type::PerformanceMode, 0, 1)));
        const auto* view = p.getLiveView();
        p.storeClip(0, 1); p.storeClip(0, 2);

        p.refuse = true;
        expect(! p.launchClip(0, 1), "stopped: nothing was applied");
        expect(! p.perform(act(Type::LaunchClip, 0, 1)));
        expectEquals(view->getPlayingClip(0), -1);
        expectEquals(view->getQueuedClip(0), -1);
        expectEquals(p.getLayer(0)->activeClip, -1);

        p.sequencerPlaying.store(true);
        expect(! p.perform(act(Type::LaunchClip, 0, 2)), "playing: nothing was staged");
        expectEquals(view->getQueuedClip(0), -1);
        expect(! p.hasPendingSwap(0));

        // A real launch queued first, then an aborted non-clip stage: the earlier launch must not land unlit.
        p.refuse = false;
        expect(p.perform(act(Type::LaunchClip, 0, 1)));
        expectEquals(view->getQueuedClip(0), 1);
        expect(p.hasPendingSwap(0));
        p.saveLayerPreset(0, "plain");
        const auto file = p.slotPresetFiles(0).getFirst();
        p.refuse = true;
        p.loadLayerPreset(0, file);
        expect(! p.hasPendingSwap(0), "the aborted stage cancelled the earlier queued clip");
        p.getLiveState().reconcile();
        expectEquals(view->getQueuedClip(0), -1);
        expectEquals(view->getPlayingClip(0), -1);
        p.sequencerPlaying.store(false);
    }

    void cancelledLaunch()
    {
        beginTest("a cancelled launch never commits; a layer-preset load after a launch supersedes it");
        TempStorage storage;
        mu_clid::PluginProcessor p;
        setParam(p.apvts, kParam, 3.0f);
        p.storeClip(0, 0);
        setParam(p.apvts, kParam, 9.0f);
        p.saveLayerPreset(0, "later");
        const auto file = p.slotPresetFiles(0).getFirst();
        setParam(p.apvts, kParam, 7.0f);
        int committed = -1;
        p.onLayerPresetCommitted = [&committed](int s) { committed = s; };

        p.sequencerPlaying.store(true);
        expect(p.launchClip(0, 0));
        p.cancelPendingSwap(0);
        expect(! p.hasPendingSwap(0));
        Probe::scan(p);
        Probe::commit(p);
        expectEquals(committed, -1);
        expectWithinAbsoluteError(getParam(p.apvts, kParam), 7.0f, 0.01f);

        // The file holds 9; the clip holds 3. Launch, then load the file: the file lands, the clip does not.
        expect(p.launchClip(0, 0));
        p.loadLayerPreset(0, file);
        expect(Probe::scan(p));
        Probe::commit(p);
        expectWithinAbsoluteError(getParam(p.apvts, kParam), 9.0f, 0.01f);
        p.sequencerPlaying.store(false);
    }

    void bankFollowsStructuralEdits()
    {
        beginTest("banks follow their rhythm through swap and remove, and a new or reset rhythm starts empty");
        TempStorage storage;
        mu_clid::PluginProcessor p;
        p.addLayer(); p.addLayer();
        expectEquals(p.getNumRhythms(), 3);
        for (int i = 0; i < 3; ++i)
            p.storeClip(i, i, "L" + juce::String(i));
        p.getLayer(2)->activeClip = 2;

        expect(p.swapLayers(0, 2));
        expectEquals(p.getLayer(0)->clips->getName(2), juce::String("L2"));
        expectEquals(p.getLayer(2)->clips->getName(0), juce::String("L0"));
        expectEquals(p.getLayer(0)->activeClip, 2, "activeClip travels with its rhythm");

        expect(p.removeLayer(0));   // L2's bank leaves; L1 moves to 0, L0 to 1
        expectEquals(p.getNumRhythms(), 2);
        expectEquals(p.getLayer(0)->clips->getName(1), juce::String("L1"));
        expectEquals(p.getLayer(1)->clips->getName(0), juce::String("L0"));

        p.addLayer();
        expectEquals((int) p.clipMask(2), 0, "a new rhythm has an empty bank");
        expectEquals(p.getLayer(2)->activeClip, -1);

        p.resetLayer(1);
        expectEquals((int) p.clipMask(1), 0, "reset empties the bank");
        expectEquals((int) p.clipMask(0), 1 << 1);
    }

    void fullPresetAndSessionRoundTrip()
    {
        beginTest("<Clips> and activeClip round-trip through a full preset and a host session");
        TempStorage storage;
        mu_clid::PluginProcessor a;
        a.addLayer();
        setParam(a.apvts, kParam, 3.0f);
        a.storeClip(0, 0, "intro");
        setParam(a.apvts, kParam, 6.0f);
        a.storeClip(0, 7);
        a.getLayer(0)->activeClip = 7;
        a.storeClip(1, 4, "second");
        a.getLayer(1)->activeClip = 4;   // names a stored clip
        setParam(a.apvts, kParam, 5.0f);

        const auto file = storage.root.getChildFile("clips.muClid");
        const auto text = xmlOf(a, file);
        expect(text.contains("<Clips>") && text.contains("activeClip=\"7\""));

        mu_clid::PluginProcessor viaFile;
        viaFile.loadPreset(file);
        expectEquals(viaFile.getNumRhythms(), 2);
        for (int i = 0; i < 2; ++i)
        {
            expect(sameBank(*a.getLayer(i)->clips, *viaFile.getLayer(i)->clips), "preset bank " + juce::String(i));
            expectEquals(viaFile.getLayer(i)->activeClip, a.getLayer(i)->activeClip);
        }

        juce::MemoryBlock session;
        a.getStateInformation(session);
        mu_clid::PluginProcessor viaSession;
        viaSession.setStateInformation(session.getData(), (int) session.getSize());
        for (int i = 0; i < 2; ++i)
        {
            expect(sameBank(*a.getLayer(i)->clips, *viaSession.getLayer(i)->clips), "session bank " + juce::String(i));
            expectEquals(viaSession.getLayer(i)->activeClip, a.getLayer(i)->activeClip);
        }

        // A launch from the restored bank behaves as on the original.
        viaSession.launchClip(0, 0);
        expectWithinAbsoluteError(getParam(viaSession.apvts, kParam), 3.0f, 0.01f);

        // An activeClip that names an empty clip loads as none.
        auto root = juce::ValueTree::fromXml(text);
        root.getChildWithName("Rhythm").setProperty("activeClip", 3, nullptr);
        mu_clid::PluginProcessor bad;
        const auto edited = storage.root.getChildFile("edited.muClid");
        edited.replaceWithText(root.toXmlString());
        bad.loadPreset(edited);
        expectEquals(bad.getLayer(0)->activeClip, -1);
        expectEquals((int) bad.clipMask(0), (1 << 0) | (1 << 7));

        // A preset (not a session) never writes a path into the embedded-decode temp folder.
        const auto tempPath = juce::File::getSpecialLocation(juce::File::tempDirectory)
                                  .getChildFile("muClid_samples").getChildFile("t.wav").getFullPathName();
        auto clip = a.getLayer(0)->clips->get(0);
        clip.setProperty("r0_sample", tempPath, nullptr);
        a.getLayer(0)->clips->store(0, clip, "intro");
        const auto presetText = xmlOf(a, storage.root.getChildFile("temp.muClid"));
        expect(! presetText.contains("muClid_samples"), "a preset blanks a temp-folder clip sample");
        juce::MemoryBlock session2;
        a.getStateInformation(session2);
        expect(session2.toString().contains("muClid_samples"), "a session keeps it");
    }

    void stagedFullPresetCarriesBanks()
    {
        beginTest("a staged full preset carries its banks and sets them at the commit; a smaller one empties the dropped rhythms");
        TempStorage storage;
        mu_clid::PluginProcessor src;
        src.storeClip(0, 5, "new");
        const auto file = storage.root.getChildFile("one.muClid");
        src.saveFullPresetTo(file);

        mu_clid::PluginProcessor p;
        p.addLayer(); p.addLayer();
        p.storeClip(0, 1, "old0");
        p.storeClip(2, 2, "old2");
        const auto oldBank = *p.getLayer(0)->clips;

        p.sequencerPlaying.store(true);
        p.loadPreset(file);
        expect(p.hasPendingFullPreset());
        expect(sameBank(*p.getLayer(0)->clips, oldBank), "nothing changes before the commit");
        expect(Probe::scan(p));
        Probe::commit(p);
        p.sequencerPlaying.store(false);

        expectEquals(p.getNumRhythms(), 1);
        expectEquals((int) p.clipMask(0), 1 << 5);
        expectEquals(p.getLayer(0)->clips->getName(5), juce::String("new"));
        expect(p.getRhythm(1).clips->isEmpty() && p.getRhythm(2).clips->isEmpty(), "dropped rhythms' banks are empty");
        p.addLayer();
        expectEquals((int) p.clipMask(1), 0);
    }

    void oldFilesLoadEmpty()
    {
        beginTest("a file without <Clips> and a legacy session load with empty banks; mu-Clid MIDI-shaped files stay clip-free");
        TempStorage storage;
        mu_clid::PluginProcessor p;
        p.storeClip(0, 2);
        const auto presets = presetsDir();
        expect(presets.isDirectory(), "tests/presets found");
        if (presets.isDirectory())
        {
            const auto old = presets.getChildFile("A3_filter.muClid");
            expect(! old.loadFileAsString().contains("<Clips"), "the fixture predates clips");
            p.loadPreset(old);
            expectEquals((int) p.clipMask(0), 0);
            expectEquals(p.getLayer(0)->activeClip, -1);
        }

        p.storeClip(0, 2);
        juce::ValueTree legacy("LegacyHostState");
        legacy.setProperty("numRhythms", 1, nullptr);
        const auto text = legacy.createXml()->toString();
        p.setStateInformation(text.toRawUTF8(), (int) text.getNumBytesAsUTF8());
        expectEquals((int) p.clipMask(0), 0, "the legacy reader empties the banks");

        // A rhythm preset file never carries a bank, and a full preset with empty banks writes none.
        p.storeClip(0, 2);
        p.saveLayerPreset(0, "nobank");
        expect(! p.slotPresetFiles(0).getFirst().loadFileAsString().contains("Clips"));
        p.clearClip(0, 2);
        expect(! xmlOf(p, storage.root.getChildFile("empty.muClid")).contains("Clips"));
    }
};

static MuClidClipLaunchTests muClidClipLaunchTests;
