// The Arp layer type in a mu-Tronic slot (docs/mu-tronic/design.md T1b): its rows are in the 16-slot layout under
// the a{N}_ prefix and saved only for a slot that holds an Arp; it renders the same stream as in mu-Toni; a type
// change restarts the strip's sidechain follower; its presets come from mu-Toni's folder; and type changes under a
// running audio thread allocate nothing.

// This translation unit carries the allocation probe's global operator new / delete (test executable only).
#define MU_ALLOCATION_PROBE_DEFINE_OPERATORS
#include "Plugin/PluginProcessor.h"
#include "Tests/AllocationProbe.h"
#include "Tests/ArpLayerFixture.h"      // mu-toni: the recorded Arp stream shared with mu-Toni's own tests
#include "Tests/GoldenRender.h"         // mu-core: MockHost
#include "Tests/ProcessorSeamHelpers.h"

#include <atomic>
#include <thread>

namespace
{
    using mu_core::LayerTypeId;

    // The processor with the channel tap and the mixer exposed.
    struct ArpProcessor : mu_tronic::PluginProcessor
    {
        using mu_tronic::PluginProcessor::layerAccess;
        using mu_tronic::PluginProcessor::retireBin;
    };

    // Rows (<p>) a slot node holds.
    int rowCount(const juce::ValueTree& node)
    {
        int n = 0;
        for (int i = 0; i < node.getNumChildren(); ++i)
            if (node.getChild(i).hasType(mu_pp::kRowTag)) ++n;
        return n;
    }

    void setPlain(juce::AudioProcessorValueTreeState& apvts, const juce::String& id, float plain)
    {
        auto* p = apvts.getParameter(id);
        jassert(p != nullptr);
        p->setValueNotifyingHost(p->convertTo0to1(plain));
    }

    float getPlain(juce::AudioProcessorValueTreeState& apvts, const juce::String& id)
    {
        auto* p = apvts.getParameter(id);
        return p->convertFrom0to1(p->getValue());
    }

    // Appends channel 0's stream as the mixer receives it (sized up front: nothing allocates while rendering).
    struct Channel0Tap : MixerEngine::ChannelTap
    {
        explicit Channel0Tap(int frames) { buf.setSize(2, frames); buf.clear(); }
        void capture(int ch, const juce::AudioBuffer<float>& b, int n) noexcept override
        {
            if (ch != 0) return;
            const int len = juce::jmin(n, buf.getNumSamples() - written);
            for (int c = 0; c < 2 && c < b.getNumChannels(); ++c)
                if (len > 0) juce::FloatVectorOperations::copy(buf.getWritePointer(c) + written, b.getReadPointer(c), len);
            written += juce::jmax(0, len);
        }
        juce::AudioBuffer<float> buf;
        int written = 0;
    };
}

struct MuTronicArpTests : juce::UnitTest
{
    MuTronicArpTests() : juce::UnitTest("mu-Tronic Arp layer type", "mu-tronic-arp") {}

    void runTest() override
    {
        layoutAndRegistry();
        savedState();
        typeChangeResetsOnlyTheNewPrefix();
        sameStreamAsMuToni();
        followerRestarts();
        presetFolders();
        typeChangeStress();
    }

    void layoutAndRegistry()
    {
        beginTest("the Arp type is offered, and its 63 rows exist under a{N}_ for all sixteen slots");
        mu_seam_test::TempStorage storage;
        ArpProcessor p;
        expectEquals(p.getLayerTypes().size(), 1);
        const auto* info = p.getLayerTypes().find(LayerTypeId::Arp);
        expect(info != nullptr);
        if (info != nullptr)
        {
            expectEquals(juce::String(info->name), juce::String("Arp"));
            expectEquals((int) info->paramPrefixLetter, (int) 'a');
        }
        for (int s : { 0, 7, 15 })
            expect(p.apvts.getParameter("a" + juce::String(s) + "_cut") != nullptr, "a" + juce::String(s) + "_cut");
        expect(p.apvts.getParameter("a16_cut") == nullptr, "no seventeenth slot");
        int arpRows = 0;
        for (auto* raw : p.getParameters())
            if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*>(raw))
                if (withId->getParameterID().startsWith("a3_")) ++arpRows;
        expectEquals(arpRows, 63);
    }

    // A slot's node holds the rows of its live type only: an Arp slot's 63, an Empty slot's none, and a parameter
    // of a dormant prefix, even when changed, never reaches the file.
    void savedState()
    {
        beginTest("a slot saves the rows of its live type only, and the type attribute selects the prefix on load");
        mu_seam_test::TempStorage storage;
        ArpProcessor p;
        expect(p.setLayerType(3, LayerTypeId::Arp));
        setPlain(p.apvts, "a3_cut", 2200.0f);
        setPlain(p.apvts, "a7_cut", 900.0f);   // slot 7 is Empty: this prefix is dormant

        auto state = p.captureState();
        expectEquals(rowCount(mu_pp::findLayerNode(state, 3)), 63, "an Arp slot saves its rows");
        expectEquals(rowCount(mu_pp::findLayerNode(state, 7)), 0, "a dormant prefix is not written");
        expectEquals(rowCount(mu_pp::findLayerNode(state, 0)), 0, "an Empty slot writes no rows");
        bool anyPrefixed = false;
        for (int i = 0; i < mu_pp::findLayerNode(state, 3).getNumChildren(); ++i)
            if (const auto row = mu_pp::findLayerNode(state, 3).getChild(i); row.hasType(mu_pp::kRowTag))
                anyPrefixed = anyPrefixed || row.getProperty("id").toString().startsWith("a3_");
        expect(! anyPrefixed, "row ids are saved without the slot's prefix");

        juce::MemoryBlock block;
        p.getStateInformation(block);
        mu_seam_test::TempStorage storage2;
        ArpProcessor q;
        q.setStateInformation(block.getData(), (int) block.getSize());
        expect(q.getLayerTypeId(3) == LayerTypeId::Arp);
        expectWithinAbsoluteError(getPlain(q.apvts, "a3_cut"), 2200.0f, 1.0f);
        expectWithinAbsoluteError(getPlain(q.apvts, "a7_cut"), q.apvts.getParameter("a7_cut")->convertFrom0to1(q.apvts.getParameter("a7_cut")->getDefaultValue()), 0.01f,
                                  "the dormant value did not travel");
        auto again = q.captureState();
        expect(again.isEquivalentTo(state), "restoring then capturing reproduces the saved state");
        expectEquals(rowCount(mu_pp::findLayerNode(again, 3)), 63);

        // A hand-made node: rows without a prefix, applied under the prefix of the type the node names.
        auto hand = p.captureState();
        auto node5 = mu_pp::findLayerNode(hand, 5);
        node5.setProperty("type", "Arp", nullptr);
        juce::ValueTree row(mu_pp::kRowTag);
        row.setProperty("id", "cut", nullptr);
        row.setProperty("x", 3300.0, nullptr);
        node5.appendChild(row, nullptr);
        mu_seam_test::TempStorage storage3;
        ArpProcessor r;
        r.applyStateTree(hand);
        expect(r.getLayerTypeId(5) == LayerTypeId::Arp);
        expectWithinAbsoluteError(getPlain(r.apvts, "a5_cut"), 3300.0f, 1.0f);
        expectWithinAbsoluteError(getPlain(r.apvts, "a3_cut"), 2200.0f, 1.0f);

        // A slot that loads Empty leaves the Arp rows of the previous tenant out of the next save.
        r.flushPendingAsyncUpdates();
        expect(r.setLayerType(5, LayerTypeId::Empty));
        expectEquals(rowCount(mu_pp::findLayerNode(r.captureState(), 5)), 0);
        r.flushPendingAsyncUpdates();
        p.flushPendingAsyncUpdates();
        q.flushPendingAsyncUpdates();
    }

    void typeChangeResetsOnlyTheNewPrefix()
    {
        beginTest("a type change resets the new type's rows of that slot to defaults and touches nothing else");
        mu_seam_test::TempStorage storage;
        ArpProcessor p;
        expect(p.setLayerType(2, LayerTypeId::Arp));
        expect(p.setLayerType(4, LayerTypeId::Arp));
        setPlain(p.apvts, "a2_cut", 2200.0f);
        setPlain(p.apvts, "a4_cut", 2200.0f);
        setPlain(p.apvts, "ch2_lvl", 0.5f);
        const float def = p.apvts.getParameter("a2_cut")->convertFrom0to1(p.apvts.getParameter("a2_cut")->getDefaultValue());

        expect(p.setLayerType(2, LayerTypeId::Empty));
        expect(p.setLayerType(2, LayerTypeId::Arp));
        expectWithinAbsoluteError(getPlain(p.apvts, "a2_cut"), def, 0.01f, "a new Arp starts at the defaults");
        expectWithinAbsoluteError(getPlain(p.apvts, "a4_cut"), 2200.0f, 1.0f, "another slot's rows are untouched");
        expectWithinAbsoluteError(getPlain(p.apvts, "ch2_lvl"), 0.5f, 0.001f, "the mixer strip is kept");

        // An initial node lands on the new type's rows.
        juce::ValueTree node(mu_pp::kLayerTag);
        juce::ValueTree row(mu_pp::kRowTag);
        row.setProperty("id", "cut", nullptr);
        row.setProperty("x", 1500.0, nullptr);
        node.appendChild(row, nullptr);
        expect(p.setLayerType(2, LayerTypeId::Empty));
        expect(p.setLayerType(2, LayerTypeId::Arp, &node));
        expectWithinAbsoluteError(getPlain(p.apvts, "a2_cut"), 1500.0f, 1.0f);
        p.flushPendingAsyncUpdates();
    }

    // The recorded Arp stream of mu-Toni (Tests/ArpLayerFixture.h): the layer in a mu-Tronic slot must reproduce it
    // bit for bit, which is "sample-identical to mu-Toni's own" since mu-Toni's tests hold the same hash.
    void sameStreamAsMuToni()
    {
        beginTest("an Arp in a mu-Tronic slot renders the stream mu-Toni records");
        namespace fx = mu_toni_fixture;
        mu_seam_test::TempStorage storage;
        mu_seam_test::ScopedEnvVar deterministic("MU_DETERMINISTIC_NOISE", "1");

        ArpProcessor p;
        mu_golden::MockHost host;
        host.bpm = fx::kBpm;
        p.setPlayHead(&host);
        p.setRateAndBufferSizeDetails(fx::kSampleRate, fx::kBlock);
        p.prepareToPlay(fx::kSampleRate, fx::kBlock);
        expect(p.setLayerType(0, LayerTypeId::Arp));
        fx::configure(p.apvts, 0);

        Channel0Tap tap(fx::kBlock * fx::kBlocks);
        p.setChannelTapForTest(&tap);
        juce::AudioBuffer<float> buf(2, fx::kBlock);
        for (int b = 0; b < fx::kBlocks; ++b)
        {
            juce::MidiBuffer midi;
            for (const auto& k : fx::keys())
                if (k.block == b) midi.addEvent(juce::MidiMessage(k.status, k.note, k.velocity), 0);
            host.timeInSamples = (juce::int64) b * fx::kBlock;
            buf.clear();
            p.processBlock(buf, midi);
            host.ppq += (double) fx::kBlock * fx::kBpm / (60.0 * fx::kSampleRate);
        }
        p.setChannelTapForTest(nullptr);
        p.setPlayHead(nullptr);
        p.releaseResources();

        expectEquals(tap.written, fx::kBlock * fx::kBlocks, "every block reached the mixer");
        fx::StreamHash hash;
        for (int b = 0; b < fx::kBlocks; ++b) hash.add(tap.buf, fx::kBlock, b * fx::kBlock);
        logMessage("mu-Tronic Arp stream hash " + juce::String::toHexString((juce::int64) hash.h));
        expect(hash.h == fx::kRecordedHash, "the slot renders the recorded Arp stream");
        expect(hash.peak > 0.01f && hash.peak < 4.0f, "and it is a real, bounded sound");
        p.flushPendingAsyncUpdates();
    }

    // C17: a strip that ducks from another keeps its follower in the mixer; a new layer in the slot starts it at zero.
    void followerRestarts()
    {
        beginTest("after a type change the strip's sidechain follower starts at zero, consumed by the audio thread");
        mu_seam_test::TempStorage storage;
        ArpProcessor p;
        p.prepareToPlay(48000.0, 256);
        mu_seam_test::setParam(p.apvts, "ch2_scSrc", 4.0f);   // ducks from slot 4 (1-based source numbers), which is Empty and silent
        mu_seam_test::setParam(p.apvts, "ch2_scAmt", 1.0f);
        p.mixerEngine.setSidechainEnv(2, 0.9f);
        p.mixerEngine.setSidechainEnv(6, 0.9f);

        juce::AudioBuffer<float> buf(2, 256);
        juce::MidiBuffer midi;
        p.processBlock(buf, midi);
        expect(p.mixerEngine.getSidechainEnv(2) > 0.5f, "without a type change the follower decays only a little in a block");

        p.mixerEngine.setSidechainEnv(2, 0.9f);
        expect(p.setLayerType(2, LayerTypeId::Arp));
        expectWithinAbsoluteError(p.mixerEngine.getSidechainEnv(2), 0.9f, 1.0e-6f, "the request is a flag: nothing writes the follower off the audio thread");
        buf.clear();
        p.processBlock(buf, midi);
        expectEquals(p.mixerEngine.getSidechainEnv(2), 0.0f, "the next block starts the follower from zero");

        // resetLayer on a Published host restarts it too, also for an Empty slot where no type change happens.
        p.mixerEngine.setSidechainEnv(2, 0.9f);
        p.resetLayer(2);
        buf.clear();
        p.processBlock(buf, midi);
        expectEquals(p.mixerEngine.getSidechainEnv(2), 0.0f, "resetLayer restarts the follower");
        p.releaseResources();
        p.flushPendingAsyncUpdates();
    }

    // The preset folder, extension and listing follow the selected slot's type: mu-Toni's own Arps folder.
    void presetFolders()
    {
        beginTest("layer presets come from mu-Toni's Arps folder, by the selected slot's type");
        mu_seam_test::TempStorage storage;
        ArpProcessor p;
        const auto arps = p.getContentDir().getSiblingFile("muToni").getChildFile("Arps");
        arps.createDirectory();
        arps.getChildFile("Bright.muArp").replaceWithText("<MuToniLayer/>");
        arps.getChildFile("Notes.txt").replaceWithText("not a preset");

        p.setSelectedSlot(0);
        expect(p.getLayerPresetDir() == juce::File(), "an Empty selected slot has no folder");
        expectEquals(p.getLayerPresetExtension(), juce::String());
        expectEquals(p.slotPresetFiles(0).size(), 1, "an Empty slot lists the offered types' presets");
        expect(p.slotPresetFiles(0)[0].getFileName() == "Bright.muArp");

        expect(p.setLayerType(1, LayerTypeId::Arp));
        p.setSelectedSlot(1);
        expect(p.getLayerPresetDir() == arps, "the folder is mu-Toni's, not a copy under mu-Tronic");
        expectEquals(p.getLayerPresetExtension(), juce::String("muArp"));
        expectEquals(p.slotPresetFiles(1).size(), 1);
        expect(! p.getContentDir().getChildFile("Arps").exists(), "mu-Tronic makes no folder of its own");
        p.flushPendingAsyncUpdates();
    }

    // 1500 type changes on eight slots (Arp, Empty and back) while a second thread renders: the audio thread
    // allocates and frees nothing, and no layer is rendered after it is freed.
    void typeChangeStress()
    {
        beginTest("two-thread stress with the real Arp in the mix: audio thread allocation-free");
        constexpr int kChanges = 1500;
        constexpr int kBlock = 128;

        // The probe has teeth: an allocation it is armed for is counted.
        {
            mu_alloc::Armed armed;
            auto* probe = new int(7);
            const int counted = armed.allocs();
            delete probe;
            armed.stop();
            expectGreaterThan(counted, 0, "the allocation probe counts a new");
        }
        mu_seam_test::TempStorage storage;
        ArpProcessor p;
        mu_golden::MockHost host;
        p.setPlayHead(&host);
        p.setRateAndBufferSizeDetails(48000.0, kBlock);
        p.prepareToPlay(48000.0, kBlock);
        expect(p.setLayerType(0, LayerTypeId::Arp));   // one Arp that stays, with held keys feeding it

        std::atomic<bool> stop { false };
        std::atomic<int> blocks { 0 }, allocs { 0 }, frees { 0 };
        std::thread audio([&]
        {
            juce::AudioBuffer<float> buf(2, kBlock);
            juce::MidiBuffer midi;
            while (! stop.load())
            {
                midi.clear();
                if (blocks.load() % 16 == 0) midi.addEvent(juce::MidiMessage::noteOn(1, 60 + blocks.load() % 12, (juce::uint8) 100), 0);
                buf.clear();
                {
                    mu_alloc::Armed armed;
                    p.processBlock(buf, midi);
                    armed.stop();
                    allocs += armed.allocs();
                    frees += armed.frees();
                }
                host.timeInSamples += kBlock;
                host.ppq += (double) kBlock * host.bpm / (60.0 * 48000.0);
                blocks.fetch_add(1);
            }
        });
        bool allOk = true;
        for (int i = 0; i < kChanges; ++i)
        {
            const int slot = 1 + i % 7;
            const auto type = ((i / 7) % 2 == 0) ? LayerTypeId::Arp : LayerTypeId::Empty;
            allOk = p.setLayerType(slot, type) && allOk;
            if (i % 50 == 0) p.flushPendingAsyncUpdates();
        }
        stop.store(true);
        audio.join();
        p.flushPendingAsyncUpdates();
        p.setPlayHead(nullptr);
        p.releaseResources();
        logMessage("Arp type-change stress: " + juce::String(kChanges) + " changes, " + juce::String(blocks.load()) + " blocks, "
                   + juce::String(p.layerAccess.views.overflowCount()) + " overflow views");
        expect(allOk, "every type change succeeded");
        expectEquals(allocs.load() + frees.load(), 0, "the audio thread allocated or freed");
        // The audio thread must have run through the changes, not just before or after them.
        expectGreaterThan(blocks.load(), 50, "audio blocks ran during the type changes");
    }
};

static MuTronicArpTests muTronicArpTests;
