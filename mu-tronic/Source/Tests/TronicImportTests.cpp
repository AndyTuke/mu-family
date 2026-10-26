// "Import product preset" (docs/mu-tronic/design.md 3.4, Stage T8b): a full preset of mu-Clid, mu-Tant, mu-Toni or mu-On is
// laid into the Empty slots of the real mu-Tronic processor. Real fixtures written by each product are read from
// tests/presets/mu-tronic-import; the mu-On Rumble case is a real mu-On file with extra tracks added to its state.

#include "Plugin/PluginProcessor.h"
#include "Persistence/ProductImport.h"
#include "Sequencer/TrackTypes.h"          // mu-On's layer library: the engine indices written into a track
#include "Audio/MixerEngine.h"
#include "Tests/ProcessorSeamHelpers.h"

#include <cmath>

namespace
{
    using mu_core::LayerTypeId;

    const juce::File fixtureDir(MU_TRONIC_IMPORT_FIXTURE_DIR);

    float plain(juce::AudioProcessorValueTreeState& apvts, const juce::String& id) { return mu_seam_test::getParam(apvts, id); }

    // The value (x) of the row `id` among a node's rows; NaN when absent.
    double rowX(const juce::ValueTree& node, const juce::String& id)
    {
        for (int i = 0; i < node.getNumChildren(); ++i)
            if (const auto r = node.getChild(i); r.hasType("p") && r.getProperty("id").toString() == id)
                return (double) r.getProperty("x");
        return std::nan("");
    }

    juce::ValueTree readXml(const juce::File& f)
    {
        const auto xml = juce::XmlDocument::parse(f);
        return xml != nullptr ? juce::ValueTree::fromXml(*xml) : juce::ValueTree();
    }

    // A mu-On state's Slot node `idx`.
    juce::ValueTree slotOf(const juce::ValueTree& preset, int idx)
    {
        const auto slots = preset.getChildWithName("MuOnState").getChildWithName("Slots");
        for (int i = 0; i < slots.getNumChildren(); ++i)
            if ((int) slots.getChild(i).getProperty("idx", -1) == idx) return slots.getChild(i);
        return {};
    }

    void setRow(juce::ValueTree node, const juce::String& id, double x)
    {
        for (int i = 0; i < node.getNumChildren(); ++i)
            if (auto r = node.getChild(i); r.hasType("p") && r.getProperty("id").toString() == id)
            {
                r.setProperty("x", x, nullptr);
                r.removeProperty("c", nullptr);
                return;
            }
        juce::ValueTree r("p");
        r.setProperty("id", id, nullptr);
        r.setProperty("x", x, nullptr);
        node.appendChild(r, nullptr);
    }

    struct MuTronicImportTests : public juce::UnitTest
    {
        MuTronicImportTests() : juce::UnitTest("mu-Tronic product-preset import", "mu-Tronic") {}

        void runTest() override
        {
            sidechainRemap();
            fixturesExist();
            toniPreset();
            tantPresetAndDump();
            clidPreset();
            onPresetWithRumble();
            filledSlotsAreNeverTouched();
            tooFewSlotsRefuses();
            unreadableFiles();
            savedStateAfterImport();
        }

        void fixturesExist()
        {
            beginTest("the product fixtures are in the repository");
            for (const char* fixture : { "REAL_clid.muClid", "REAL_tant.muTant", "REAL_tant_dump.muTant", "REAL_toni.muToni",
                                         "REAL_on.muOn", "REAL_on_lanes.muOn" })
                expect(fixtureDir.getChildFile(fixture).existsAsFile(), fixture);
        }

        void sidechainRemap()
        {
            beginTest("an imported sidechain source follows its layer; the external input becomes this binary's");
            const std::vector<int> slotOf { 4, 5, 6 };
            const int ext = MixerEngine::kExtSidechainSrc + 1;
            expectEquals(mu_tronic::remapImportedSidechain(0, 9, slotOf), 0, "off stays off");
            expectEquals(mu_tronic::remapImportedSidechain(2, 9, slotOf), 6, "product channel 1 sits in slot 5, parameter 6");
            expectEquals(mu_tronic::remapImportedSidechain(mu_tronic::kProductLayers + 1, 9, slotOf), ext, "the external input (engine value 8)");
            expectEquals(mu_tronic::remapImportedSidechain(2, 5, slotOf), 0, "a layer cannot duck from itself");
            expectEquals(mu_tronic::remapImportedSidechain(5, 9, slotOf), 0, "a source that was not imported is off");
            expectEquals(mu_tronic::remapImportedSidechain(mu_tronic::kProductLayers + 2, 9, slotOf), 0, "the products have no buses");
        }

        void toniPreset()
        {
            beginTest("a mu-Toni preset becomes one Arp with its own values and strip");
            mu_seam_test::TempStorage storage;
            mu_tronic::PluginProcessor p;
            const auto file = fixtureDir.getChildFile("REAL_toni.muToni");
            const auto tree = readXml(file).getChildWithName("MuToniState");
            const auto slot0 = tree.getChildWithName("Slots").getChild(0);
            const auto globals = tree.getChildWithName("Globals");

            const auto r = p.importProductPreset(file);
            expect(r.ok, r.message);
            expectEquals(r.imported, 1);
            expectEquals(r.message, juce::String("Imported 1 layer from REAL_toni"));
            expect(p.getLayerTypeId(0) == LayerTypeId::Arp);
            expect(p.getLayerTypeId(1) == LayerTypeId::Empty);
            expectWithinAbsoluteError(plain(p.apvts, "a0_cut"), (float) rowX(slot0, "cut"), 0.5f, "the preset's filter cutoff, not the default");
            expectWithinAbsoluteError(plain(p.apvts, "ch0_lvl"), (float) rowX(globals, "ch0_lvl"), 0.01f);
            expectEquals(plain(p.apvts, "ch0_midiIn"), (float) rowX(globals, "midiInCh"), "Notes In maps to the layer's MIDI in");
            p.flushPendingAsyncUpdates();
        }

        void tantPresetAndDump()
        {
            beginTest("a mu-Tant preset (composed) and an older APVTS dump with four voices become Pattern layers");
            {
                mu_seam_test::TempStorage storage;
                mu_tronic::PluginProcessor p;
                const auto r = p.importProductPreset(fixtureDir.getChildFile("REAL_tant.muTant"));
                expect(r.ok, r.message);
                expect(p.getLayerTypeId(0) == LayerTypeId::Pattern);
                const auto tree = readXml(fixtureDir.getChildFile("REAL_tant.muTant")).getChildWithName("MuTantState");
                expectWithinAbsoluteError(plain(p.apvts, "v0_flt_cut"), (float) rowX(tree.getChildWithName("Slots").getChild(0), "flt_cut"), 1.0f);
                p.flushPendingAsyncUpdates();
            }
            mu_seam_test::TempStorage storage;
            mu_tronic::PluginProcessor p;
            const auto r = p.importProductPreset(fixtureDir.getChildFile("REAL_tant_dump.muTant"));
            expect(r.ok, r.message);
            expectEquals(r.imported, 4);
            for (int s = 0; s < 4; ++s) expect(p.getLayerTypeId(s) == LayerTypeId::Pattern, "voice " + juce::String(s));
            expect(p.getLayerTypeId(4) == LayerTypeId::Empty);
            expectWithinAbsoluteError(plain(p.apvts, "v0_flt_cut"), 2600.0f, 1.0f, "the dump's v0_flt_cut");
            expectEquals(plain(p.apvts, "root"), 2.0f, "the tonal centre comes with the first Pattern");
            p.flushPendingAsyncUpdates();
        }

        void clidPreset()
        {
            beginTest("a mu-Clid preset's rhythms become one Rhythm layer each, names and strips with them");
            mu_seam_test::TempStorage storage;
            mu_tronic::PluginProcessor p;
            const auto file = fixtureDir.getChildFile("REAL_clid.muClid");
            const auto root = readXml(file);
            const auto r = p.importProductPreset(file);
            expect(r.ok, r.message);
            expectEquals(r.imported, 3);
            for (int s = 0; s < 3; ++s) expect(p.getLayerTypeId(s) == LayerTypeId::Rhythm);
            expectEquals(juce::String(p.getLayer(0)->name), juce::String("Kick"));
            expectEquals(juce::String(p.getLayer(1)->name), juce::String("Hat"));
            expectEquals(plain(p.apvts, "r0_stepsA"), (float) (int) root.getChild(0).getProperty("stepsA"), "the rhythm's own steps");
            expectWithinAbsoluteError(p.apvts.getParameter("ch1_lvl")->getValue(), (float) (double) root.getChild(1).getProperty("ch_lvl"), 0.001f,
                                      "the strip level is normalised in the file and arrives unchanged");
            // The Hat ducked from the file's first channel: here from slot 0, parameter 1.
            expectEquals(plain(p.apvts, "ch1_scSrc"), 1.0f);
            p.flushPendingAsyncUpdates();
        }

        // A real mu-On file with a Rumble and a Hat added to its one Kick: the Rumble listens to the Kick, the Hat ducks from it,
        // and a second Hat takes the external input.
        void onPresetWithRumble()
        {
            beginTest("a mu-On preset's tracks take their engine as type; a Rumble's source and the sidechains follow the slots");
            mu_seam_test::TempStorage storage;
            auto preset = readXml(fixtureDir.getChildFile("REAL_on.muOn"));
            auto state = preset.getChildWithName("MuOnState");
            state.setProperty("numVoices", 4, nullptr);
            auto globals = state.getChildWithName("Globals");
            const mu_on::EngineType engines[] = { mu_on::EngineType::Kick, mu_on::EngineType::Rumble, mu_on::EngineType::Hat, mu_on::EngineType::Hat };
            for (int i = 0; i < 4; ++i)
            {
                auto slot = slotOf(preset, i);
                if (! slot.isValid())
                {
                    slot = slotOf(preset, 0).createCopy();
                    slot.setProperty("idx", i, nullptr);
                    state.getChildWithName("Slots").appendChild(slot, nullptr);
                }
                setRow(slot, "eng", mu_on::toIndex(engines[i]));
                if (i == 1) setRow(slot, "src", 0);
            }
            setRow(globals, "ch2_scSrc", 1);                              // the Hat ducks from product channel 0 (the Kick)
            setRow(globals, "ch3_scSrc", mu_tronic::kProductLayers + 1);  // the second Hat takes the external input
            const auto file = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("tronic_import_rumble.muOn");
            expect(file.replaceWithText(preset.toXmlString()));

            mu_tronic::PluginProcessor p;
            expect(p.setLayerType(0, LayerTypeId::Snare), "slot 0 is already filled");
            const auto r = p.importProductPreset(file);
            expect(r.ok, r.message);
            expectEquals(r.imported, 4);
            expect(p.getLayerTypeId(0) == LayerTypeId::Snare, "the filled slot is untouched");
            expect(p.getLayerTypeId(1) == LayerTypeId::Kick);
            expect(p.getLayerTypeId(2) == LayerTypeId::Rumble);
            expect(p.getLayerTypeId(3) == LayerTypeId::Hat);
            expect(p.getLayerTypeId(4) == LayerTypeId::Hat);
            expectEquals(plain(p.apvts, "t2_src"), 1.0f, "the Rumble listens to the Kick's new slot");
            expectEquals(plain(p.apvts, "ch3_scSrc"), 2.0f, "the Hat ducks from slot 1 (parameter 2)");
            expectEquals(plain(p.apvts, "ch4_scSrc"), (float) (MixerEngine::kExtSidechainSrc + 1), "the external input is this binary's");

            // A Rumble whose source layer was not in the preset falls to the first usable slot.
            setRow(slotOf(preset, 1), "src", 7);
            expect(file.replaceWithText(preset.toXmlString()));
            mu_tronic::PluginProcessor q;
            expect(q.setLayerType(0, LayerTypeId::Snare));
            expect(q.importProductPreset(file).ok);
            const int source = (int) plain(q.apvts, "t2_src");
            expect(source == 0 || source == 1, "an unusable source falls to a filled non-Rumble slot");
            file.deleteFile();
            p.flushPendingAsyncUpdates();
            q.flushPendingAsyncUpdates();
        }

        void filledSlotsAreNeverTouched()
        {
            beginTest("an import lands in Empty slots only and leaves a filled slot's layer and values alone");
            mu_seam_test::TempStorage storage;
            mu_tronic::PluginProcessor p;
            expect(p.setLayerType(0, LayerTypeId::Arp));
            mu_seam_test::setParam(p.apvts, "a0_cut", 1234.0f);
            const auto r = p.importProductPreset(fixtureDir.getChildFile("REAL_toni.muToni"));
            expect(r.ok, r.message);
            expect(p.getLayerTypeId(0) == LayerTypeId::Arp);
            expectEquals(plain(p.apvts, "a0_cut"), 1234.0f);
            expect(p.getLayerTypeId(1) == LayerTypeId::Arp, "the imported Arp took the first Empty slot");
            p.flushPendingAsyncUpdates();
        }

        void tooFewSlotsRefuses()
        {
            beginTest("too few free slots refuses the whole import and changes nothing");
            mu_seam_test::TempStorage storage;
            mu_tronic::PluginProcessor p;
            for (int s = 0; s < 15; ++s) expect(p.setLayerType(s, LayerTypeId::Kick));
            mu_seam_test::setParam(p.apvts, "ch15_lvl", 0.25f);
            const auto r = p.importProductPreset(fixtureDir.getChildFile("REAL_clid.muClid"));
            expect(! r.ok);
            expectEquals(r.imported, 0);
            expectEquals(r.message, juce::String("Needs 3 free slots, 1 free"));
            expect(p.getLayerTypeId(15) == LayerTypeId::Empty, "the one free slot is still Empty");
            for (int s = 0; s < 15; ++s) expect(p.getLayerTypeId(s) == LayerTypeId::Kick);
            expectEquals(plain(p.apvts, "ch15_lvl"), 0.25f, "no strip was written");
            p.flushPendingAsyncUpdates();
        }

        void unreadableFiles()
        {
            beginTest("an older mu-On, a wrong extension and a missing file are refused with a message");
            mu_seam_test::TempStorage storage;
            mu_tronic::PluginProcessor p;
            auto r = p.importProductPreset(fixtureDir.getChildFile("REAL_on_lanes.muOn"));
            expect(! r.ok);
            expect(r.message.contains("older mu-On"), r.message);
            const auto text = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("tronic_import_note.txt");
            expect(text.replaceWithText("not a preset"));
            r = p.importProductPreset(text);
            expectEquals(r.message, juce::String("Not a product preset: tronic_import_note.txt"));
            text.deleteFile();
            r = p.importProductPreset(fixtureDir.getChildFile("missing.muClid"));
            expect(! r.ok);
            expect(r.message.startsWith("Could not read"), r.message);
            for (int s = 0; s < 16; ++s) expect(p.getLayerTypeId(s) == LayerTypeId::Empty);
            p.flushPendingAsyncUpdates();
        }

        void savedStateAfterImport()
        {
            beginTest("the state saved after an import (.muTronic round trip) restores the layers, names, rows and strips");
            mu_seam_test::TempStorage storage;
            mu_tronic::PluginProcessor p;
            expect(p.importProductPreset(fixtureDir.getChildFile("REAL_clid.muClid")).ok);
            expect(p.importProductPreset(fixtureDir.getChildFile("REAL_toni.muToni")).ok);
            juce::MemoryBlock block;
            p.getStateInformation(block);

            mu_seam_test::TempStorage storage2;
            mu_tronic::PluginProcessor q;
            q.setStateInformation(block.getData(), (int) block.getSize());
            for (int s = 0; s < 3; ++s) expect(q.getLayerTypeId(s) == LayerTypeId::Rhythm, "slot " + juce::String(s));
            expect(q.getLayerTypeId(3) == LayerTypeId::Arp);
            expectEquals(juce::String(q.getLayer(1)->name), juce::String("Hat"));
            expectEquals(plain(q.apvts, "r0_stepsA"), plain(p.apvts, "r0_stepsA"));
            expectEquals(plain(q.apvts, "a3_cut"), plain(p.apvts, "a3_cut"));
            expectEquals(plain(q.apvts, "ch1_scSrc"), 1.0f, "the remapped sidechain survives the round trip");
            p.flushPendingAsyncUpdates();
            q.flushPendingAsyncUpdates();
        }
    };

    static MuTronicImportTests muTronicImportTests;
}
