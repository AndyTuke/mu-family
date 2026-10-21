// Clips and scenes in mu-Tronic (docs/mu-tronic/design.md 3.4 and 3.6) on the real Published processor with the real
// layer types: a clip carries its type and loads only into a slot of that type; a scene launches clip n on every layer that
// stores one, all at the same boundary; the clip banks and the performance list survive the saved state; and a
// performance file of full presets restores whole mixed-type setups.

#include "Plugin/PluginProcessor.h"
#include "Persistence/PerformanceFile.h"
#include "Tests/GoldenRender.h"          // mu-core: MockHost
#include "Tests/ProcessorSeamHelpers.h"

#include <vector>

namespace
{
    using mu_core::LayerTypeId;
    using T = mu_core::ControlActionType;

    struct SceneProcessor : mu_tronic::PluginProcessor
    {
        using mu_tronic::PluginProcessor::drainDeferredWork;
        using mu_tronic::PluginProcessor::layerSwapper;
        bool isStandaloneApp() const noexcept override { return true; }   // Performance mode is a standalone feature
    };

    mu_core::ControlAction act(T type, int layer = 0, int index = -1)
    {
        mu_core::ControlAction a;
        a.type = type; a.layer = layer; a.index = index;
        return a;
    }

    // The first continuous row of a slot (its full parameter id): any one float parameter of the layer, so a clip's contents
    // can be told apart without knowing the type's rows.
    juce::String firstRow(SceneProcessor& p, int slot)
    {
        const auto* info = p.getLayerTypes().find(p.getLayerTypeId(slot));
        const auto tree = p.captureLayerPresetTree(slot);
        if (info == nullptr) return {};
        for (int i = 0; i < tree.getNumChildren(); ++i)
        {
            const auto local = tree.getChild(i).getProperty("id").toString();
            if (local.isEmpty()) continue;
            const auto id = juce::String::charToString((juce::juce_wchar) info->paramPrefixLetter) + juce::String(slot) + "_" + local;
            if (dynamic_cast<juce::AudioParameterFloat*>(p.apvts.getParameter(id)) != nullptr) return id;
        }
        return {};
    }

    // Moves the slot's first row to the normalised value `to` and returns the id used.
    juce::String setFirstRow(SceneProcessor& p, int slot, float to)
    {
        const auto id = firstRow(p, slot);
        if (auto* param = p.apvts.getParameter(id)) param->setValueNotifyingHost(to);
        return id;
    }

    float normalised(SceneProcessor& p, const juce::String& id) { return p.apvts.getParameter(id)->getValue(); }

    // Plays a MockHost transport through the processor one block at a time.
    struct Player
    {
        explicit Player(SceneProcessor& proc) : p(proc), buf(2, kBlock)
        {
            host.bpm = 120.0;
            p.setPlayHead(&host);
            p.setRateAndBufferSizeDetails(48000.0, kBlock);
            p.prepareToPlay(48000.0, kBlock);
        }
        ~Player() { p.setPlayHead(nullptr); p.releaseResources(); }

        void block()
        {
            juce::MidiBuffer midi;
            host.timeInSamples = (juce::int64) index * kBlock;
            buf.clear();
            p.processBlock(buf, midi);
            host.ppq += (double) kBlock * host.bpm / (60.0 * 48000.0);
            ++index;
        }

        static constexpr int kBlock = 256;
        SceneProcessor& p;
        int index = 0;
        mu_golden::MockHost host;
        juce::AudioBuffer<float> buf;
    };

    constexpr LayerTypeId kMix[] = { LayerTypeId::Kick, LayerTypeId::Arp, LayerTypeId::Rumble, LayerTypeId::Pattern, LayerTypeId::Rhythm };
    constexpr int kSlots[] = { 0, 2, 3, 5, 8 };   // gaps between them: the Empty slots in between are skipped
}

struct MuTronicSceneTests : juce::UnitTest
{
    MuTronicSceneTests() : juce::UnitTest("mu-Tronic clips, scenes and performances", "mu-tronic-scenes") {}

    void runTest() override
    {
        clipsCarryTheirType();
        sceneLaunchesEveryLayer();
        sceneLandsTogetherOnTheBar(2, 4, 4);   // On bar, 4/4
        sceneLandsTogetherOnTheBar(1, 4, 4);   // On layer loop: a scene still shares the bar line
        sceneLandsTogetherOnTheBar(0, 3, 4);   // On master loop (no master loop here), 3/4
        stateKeepsTheBanks();
        performanceOfFullPresets();
    }

    void build(SceneProcessor& p)
    {
        for (int i = 0; i < 5; ++i) expect(p.setLayerType(kSlots[i], kMix[i]));
        p.flushPendingAsyncUpdates();
    }

    // Stores clip 0 (row at 0.2) and clip 1 (row at 0.8) of every layer, then leaves the row at 0.5.
    void fillBanks(SceneProcessor& p, std::vector<juce::String>& rows)
    {
        rows.clear();
        for (int i = 0; i < 5; ++i)
        {
            rows.push_back(setFirstRow(p, kSlots[i], 0.2f));
            expect(p.apvts.getParameter(rows.back()) != nullptr, "slot " + juce::String(kSlots[i]) + " has a first row parameter: '" + rows.back() + "'");
            expect(p.storeClip(kSlots[i], 0, "low"));
            setFirstRow(p, kSlots[i], 0.8f);
            expect(p.storeClip(kSlots[i], 1, "high"));
            setFirstRow(p, kSlots[i], 0.5f);
        }
    }

    void clipsCarryTheirType()
    {
        beginTest("a clip stores its layer's type: it launches into a slot of that type only, and a type change empties the bank");
        mu_seam_test::TempStorage storage;
        SceneProcessor p;
        build(p);
        std::vector<juce::String> rows;
        fillBanks(p, rows);
        for (int i = 0; i < 5; ++i)
            expectEquals((int) p.clipMask(kSlots[i]), 0b11, "slot " + juce::String(kSlots[i]) + " stores two clips");
        expectEquals((int) p.clipMask(1), 0, "an Empty slot has no bank");
        expect(! p.storeClip(1, 0), "and cannot store one (nothing to capture)");

        // A foreign clip put into the Arp's bank by hand (the tree of the Kick) is refused at launch.
        const auto kickTree = p.getLayer(0)->clips->get(0);
        p.getLayer(2)->clips->store(4, kickTree, "alien");
        expect(! p.launchClip(2, 4), "a clip of another type is not launchable");
        expect(! p.hasPendingSwap(2));
        expect(p.launchClip(2, 0), "its own clips still are");

        // The same through a file: a layer preset of another type does not load into a clip.
        p.saveLayerPreset(0, "KickFile");
        const auto kickFile = p.slotPresetFiles(0).getFirst();
        expect(kickFile.existsAsFile());
        bool refused = false;
        p.onLoadError = [&refused](const juce::String&) { refused = true; };
        expect(! p.loadClipFromFile(2, 5, kickFile), "a Kick preset does not become an Arp clip");
        expect(refused);
        expect(p.loadClipFromFile(0, 5, kickFile), "but becomes a Kick clip");
        expectEquals((int) p.clipMask(0), 0b100011);

        // Changing the type empties the bank (a clip carries its type).
        expect(p.setLayerType(0, LayerTypeId::Bass));
        expectEquals((int) p.clipMask(0), 0, "the Kick's clips went with the Kick");
        p.flushPendingAsyncUpdates();
    }

    void sceneLaunchesEveryLayer()
    {
        beginTest("a scene launches clip n on every layer that stores it; an Empty slot, a layer without it and a foreign clip are left alone");
        mu_seam_test::TempStorage storage;
        SceneProcessor p;
        build(p);
        std::vector<juce::String> rows;
        fillBanks(p, rows);
        p.clearClip(kSlots[3], 1);                        // the Pattern has no clip 1
        const auto kickTree = p.getLayer(0)->clips->get(0);
        p.getLayer(kSlots[1])->clips->store(2, kickTree, "alien");   // the Arp's clip 2 is a Kick's
        const auto patternBefore = normalised(p, rows[3]);

        // Stopped: applied at once.
        expect(p.launchScene(1));
        for (int i : { 0, 1, 2, 4 })
            expectWithinAbsoluteError(normalised(p, rows[(size_t) i]), 0.8f, 0.02f, "slot " + juce::String(kSlots[i]) + " took clip 1");
        expectWithinAbsoluteError(normalised(p, rows[3]), patternBefore, 0.0001f, "the Pattern without clip 1 kept playing");
        expect(! p.launchScene(2), "a scene whose only clip is foreign launches nothing");
        expect(! p.launchScene(6), "a scene nobody stores does nothing");

        // The action path (the surface, MIDI map, screen) is the same call, and works with Performance mode off.
        expect(! p.getLiveView()->isPerformanceMode());
        expect(p.perform(act(T::LaunchScene, 0, 0)));
        for (int i : { 0, 1, 2, 4 })
            expectWithinAbsoluteError(normalised(p, rows[(size_t) i]), 0.2f, 0.02f);
        expectEquals(p.getLiveView()->getPlayingClip(kSlots[0]), 0);
        p.flushPendingAsyncUpdates();
    }

    void sceneLandsTogetherOnTheBar(int timing, int numerator, int denominator)
    {
        beginTest("a scene lands on one bar line whatever the swap timing (timing " + juce::String(timing) + ", meter "
                  + juce::String(numerator) + "/" + juce::String(denominator) + "): all layers staged, all land in the same block");
        mu_seam_test::TempStorage storage;
        SceneProcessor p;
        build(p);
        std::vector<juce::String> rows;
        fillBanks(p, rows);
        mu_seam_test::setParam(p.apvts, "mstr_swap", (float) timing);
        p.setTimeSignature(numerator, denominator);
        p.flushPendingAsyncUpdates();

        Player player(p);
        for (int b = 0; b < 4; ++b) player.block();
        expect(p.launchScene(1));
        for (int i = 0; i < 5; ++i)
            expect(p.hasPendingSwap(kSlots[i]), "slot " + juce::String(kSlots[i]) + " is staged");
        for (int i = 0; i < 5; ++i)
            expectEquals(p.getLiveView()->getQueuedClip(kSlots[i]), 1);

        int landedBlock = -1;
        for (int b = 0; b < 400 && landedBlock < 0; ++b)
        {
            player.block();
            p.drainDeferredWork();
            int pending = 0;
            for (int i = 0; i < 5; ++i) pending += p.hasPendingSwap(kSlots[i]) ? 1 : 0;
            expect(pending == 5 || pending == 0, "after any block the scene is wholly staged or wholly landed, block " + juce::String(b) + " pending " + juce::String(pending));
            if (pending == 0) landedBlock = b;
        }
        expect(landedBlock >= 0, "it landed");
        const double barBeats = (double) numerator * 4.0 / (double) denominator;
        const double landedBeat = (double) (landedBlock + 5) * Player::kBlock * 2.0 / 48000.0;   // 120 bpm
        expect(std::abs(landedBeat - barBeats) < 0.1 || std::abs(landedBeat - 2.0 * barBeats) < 0.1,
               "on the meter's bar line: beat " + juce::String(landedBeat) + " for a bar of " + juce::String(barBeats));
        for (int i = 0; i < 5; ++i)
        {
            expectEquals(p.getLiveView()->getPlayingClip(kSlots[i]), 1);
            expectWithinAbsoluteError(normalised(p, rows[(size_t) i]), 0.8f, 0.02f);
        }
        p.flushPendingAsyncUpdates();
    }

    void stateKeepsTheBanks()
    {
        beginTest("the saved state keeps every clip with its type and a restore rebuilds the layers; a clip of the wrong type is dropped");
        mu_seam_test::TempStorage storage;
        SceneProcessor p;
        build(p);
        std::vector<juce::String> rows;
        fillBanks(p, rows);

        juce::MemoryBlock blob;
        p.getStateInformation(blob);
        SceneProcessor q;
        q.setStateInformation(blob.getData(), (int) blob.getSize());
        q.flushPendingAsyncUpdates();
        for (int i = 0; i < 5; ++i)
        {
            expect(q.getLayerTypeId(kSlots[i]) == kMix[i], "slot " + juce::String(kSlots[i]) + " restored its type");
            expectEquals((int) q.clipMask(kSlots[i]), 0b11, "and both clips");
        }
        expect(q.launchScene(1));
        for (int i = 0; i < 5; ++i)
            expectWithinAbsoluteError(normalised(q, rows[(size_t) i]), 0.8f, 0.02f, "a restored clip launches");

        // Corrupt the state: the Kick's clip 0 becomes an Arp's tree.
        auto state = p.captureState();
        const auto arpTree = p.getLayer(kSlots[1])->clips->get(0);
        auto slotNode = mu_pp::findLayerNode(state, kSlots[0]);
        auto clips = slotNode.getChildWithName("Clips");
        expect(clips.isValid(), "the bank is saved under the slot node");
        if (clips.isValid() && clips.getNumChildren() > 0)
        {
            auto first = clips.getChild(0);
            first.removeAllChildren(nullptr);
            first.addChild(arpTree.createCopy(), -1, nullptr);
        }
        SceneProcessor r;
        r.applyStateTree(state);
        r.flushPendingAsyncUpdates();
        expect(r.clipMask(kSlots[0]) != 0b11, "a clip of the wrong type did not survive the restore");
        p.flushPendingAsyncUpdates();
    }

    void performanceOfFullPresets()
    {
        beginTest("a performance file of .muTronic presets: items restore their mixed-type setups; the session keeps the list, item and mode");
        mu_seam_test::TempStorage storage;
        SceneProcessor p;
        const auto dir = p.getFullPresetDir();
        dir.createDirectory();
        const auto product = p.getProductStateId();
        expectEquals(product, juce::String("MuTronicState"));

        // Item A: a Kick in slot 0 and an Arp in slot 1. Item B: a Rhythm in slot 0 and a Pattern in slot 4.
        expect(p.setLayerType(0, LayerTypeId::Kick));
        expect(p.setLayerType(1, LayerTypeId::Arp));
        p.flushPendingAsyncUpdates();
        const auto rowA = setFirstRow(p, 0, 0.3f);
        p.storeClip(0, 2, "keep");
        const auto fileA = dir.getChildFile("perfA.muTronic");
        expect(p.saveFullPresetTo(fileA));

        expect(p.setLayerType(0, LayerTypeId::Rhythm));
        expect(p.setLayerType(1, LayerTypeId::Empty));
        expect(p.setLayerType(4, LayerTypeId::Pattern));
        p.flushPendingAsyncUpdates();
        const auto fileB = dir.getChildFile("perfB.muTronic");
        expect(p.saveFullPresetTo(fileB));

        mu_pp::PerformanceFile list;
        list.name = "Gig";
        list.setItem(0, product, fileA, dir, "A");
        list.setItem(1, product, fileB, dir, "B");
        list.setItem(2, "MuTantState", dir.getChildFile("other.muTant"), dir, "Other");
        const auto gig = dir.getChildFile("gig.muPerformance");
        expect(list.save(gig));

        mu_pp::PerformanceFile read;
        expect(mu_pp::PerformanceFile::load(gig, read) == mu_pp::PerformanceFile::LoadResult::Ok);
        expectEquals(read.name, juce::String("Gig"));
        expect(read.status(0, dir, product) == mu_pp::PerformanceFile::ItemStatus::Ok);
        expect(read.status(2, dir, product) == mu_pp::PerformanceFile::ItemStatus::OtherProduct, "another product's item is marked, not loaded");
        expect(p.getLiveState().loadPerformance(read, gig));

        expect(p.perform(act(T::PerformanceMode, 0, 1)), "the standalone enters the mode");
        expect(p.perform(act(T::PerformanceItem, 0, 0)), "item A loads");
        expect(p.getLayerTypeId(0) == LayerTypeId::Kick && p.getLayerTypeId(1) == LayerTypeId::Arp, "A: Kick and Arp");
        expect(p.getLayerTypeId(4) == LayerTypeId::Empty);
        expectWithinAbsoluteError(normalised(p, rowA), 0.3f, 0.02f);
        expectEquals((int) p.clipMask(0), 1 << 2, "and the Kick's clip came back with it");
        expect(p.perform(act(T::PerformanceItem, 0, 1)), "item B loads");
        expect(p.getLayerTypeId(0) == LayerTypeId::Rhythm && p.getLayerTypeId(1) == LayerTypeId::Empty
               && p.getLayerTypeId(4) == LayerTypeId::Pattern, "B: Rhythm and Pattern");
        expect(! p.perform(act(T::PerformanceItem, 0, 2)), "another product's item is refused");
        expectEquals(p.getLiveView()->getCurrentItem(), 1);

        // The host session keeps the list, the current item and the mode.
        juce::MemoryBlock blob;
        p.getStateInformation(blob);
        SceneProcessor q;
        q.setStateInformation(blob.getData(), (int) blob.getSize());
        q.flushPendingAsyncUpdates();
        expect(q.getLiveView()->isPerformanceMode(), "the mode returns");
        expectEquals(q.getLiveView()->getCurrentItem(), 1);
        expectEquals(q.getLiveView()->getPerformance().count(), 3);
        expectEquals(q.getLiveView()->getPerformanceFile().getFullPathName(), gig.getFullPathName());

        // A plug-in instance never enters the mode.
        mu_tronic::PluginProcessor plugin;
        expect(plugin.getLiveState().loadPerformance(read, gig));
        expect(! plugin.perform(act(T::PerformanceMode, 0, 1)), "a plug-in refuses the mode");
        p.flushPendingAsyncUpdates();
    }
};

static MuTronicSceneTests muTronicSceneTests;
