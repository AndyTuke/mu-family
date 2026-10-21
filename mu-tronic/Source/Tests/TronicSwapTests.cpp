// Reordering the slots of mu-Tronic (docs/mu-tronic/design.md T1c): swapLayers on the real Published processor with the
// real layer types. A playing Kick, Bass, Arp and Rumble swapped between blocks, at block sizes 1, 7, 64 and 512, give the
// same mix as with no swap (every layer takes its strip, its rows and, for a Rumble, its source with it); a mixed Arp and
// Kick swap moves each layer's rows under its own prefix letter and the saved state restores; a Rumble's source follows its
// layer wherever either sits (the hook reads the types before the layers move); and the audio thread allocates nothing
// while the message thread swaps.

#include "Plugin/PluginProcessor.h"
#include "Persistence/TrackLayerTypes.h"   // chooseRumbleSource
#include "Sequencer/Track.h"               // mu-On's layer library: the layer a Track slot holds
#include "Tests/AllocationProbe.h"
#include "Tests/GoldenRender.h"            // mu-core: MockHost
#include "Tests/ProcessorSeamHelpers.h"

#include <atomic>
#include <cmath>
#include <thread>

namespace
{
    using mu_core::LayerTypeId;

    struct SwapProcessor : mu_tronic::PluginProcessor
    {
        using mu_tronic::PluginProcessor::layerAccess;
        using mu_tronic::PluginProcessor::retireBin;
    };

    mu_on::Track& trackIn(ProcessorBase& p, int slot) { return static_cast<mu_on::Track&>(*p.getLayer(slot)); }

    float getPlain(juce::AudioProcessorValueTreeState& apvts, const juce::String& id)
    {
        auto* p = apvts.getParameter(id);
        return p->convertFrom0to1(p->getValue());
    }

    int rowCount(const juce::ValueTree& node)
    {
        int n = 0;
        for (int i = 0; i < node.getNumChildren(); ++i)
            if (node.getChild(i).hasType(mu_pp::kRowTag)) ++n;
        return n;
    }

    juce::StringArray rowIds(const juce::ValueTree& node)
    {
        juce::StringArray ids;
        for (int i = 0; i < node.getNumChildren(); ++i)
            if (const auto row = node.getChild(i); row.hasType(mu_pp::kRowTag)) ids.add(row.getProperty("id").toString());
        return ids;
    }

    // The scene every swap test plays: a Kick in 3, an Arp in 5, a Rumble in 6 listening to the Kick, a Bass in 9, each
    // strip at its own level and pan so a strip that stayed behind would change the mix.
    void buildScene(SwapProcessor& p)
    {
        p.setLayerType(3, LayerTypeId::Kick);
        p.setLayerType(5, LayerTypeId::Arp);
        p.setLayerType(6, LayerTypeId::Rumble);
        p.setLayerType(9, LayerTypeId::Bass);
        mu_tronic::chooseRumbleSource(p, 6);   // the first filled non-Rumble slot: the Kick
        const int slots[] = { 3, 5, 6, 9 };
        for (int i = 0; i < 4; ++i)
        {
            mu_seam_test::setParam(p.apvts, "ch" + juce::String(slots[i]) + "_lvl", 0.45f + 0.12f * (float) i);
            mu_seam_test::setParam(p.apvts, "ch" + juce::String(slots[i]) + "_pan", -0.6f + 0.4f * (float) i);
        }
        p.flushPendingAsyncUpdates();
    }

    // A processor played one block at a time on a mock host; the first block holds a held chord for the Arp.
    struct Stream
    {
        Stream(SwapProcessor& proc, int block) : p(proc), blockSize(block), buf(2, block)
        {
            host.bpm = 120.0;
            p.setPlayHead(&host);
            p.setRateAndBufferSizeDetails(48000.0, 512);
            p.prepareToPlay(48000.0, 512);
        }
        ~Stream() { p.setPlayHead(nullptr); p.releaseResources(); }

        const juce::AudioBuffer<float>& block()
        {
            midi.clear();
            if (index == 0)
                for (const int note : { 57, 60, 64 }) midi.addEvent(juce::MidiMessage::noteOn(1, note, (juce::uint8) 100), 0);
            host.timeInSamples = (juce::int64) index * blockSize;
            buf.clear();
            p.processBlock(buf, midi);
            host.ppq += (double) blockSize * host.bpm / (60.0 * 48000.0);
            ++index;
            return buf;
        }

        SwapProcessor& p;
        int blockSize, index = 0;
        mu_golden::MockHost host;
        juce::AudioBuffer<float> buf;
        juce::MidiBuffer midi;
    };
}

struct MuTronicSwapTests : juce::UnitTest
{
    MuTronicSwapTests() : juce::UnitTest("mu-Tronic reorder (swapLayers on the Published host)", "mu-tronic-swap") {}

    void runTest() override
    {
        reorderIsOffered();
        mixedArpAndKickRows();
        rumbleSourceFollowsSwaps();
        mixIsUnchanged();
        swapStress();
    }

    void reorderIsOffered()
    {
        beginTest("the sidebar may reorder; two slots swap whether they hold a layer or not, with name and colour");
        mu_seam_test::TempStorage storage;
        SwapProcessor p;
        expect(p.canReorderLayers());
        expect(p.setLayerType(2, LayerTypeId::Kick));
        p.setLayerName(2, "Thump");
        const int colour = p.getLayerColourIndex(2);
        auto* layer = p.getLayer(2);
        expect(p.swapLayers(2, 11), "into an empty slot");
        expect(p.getLayer(11) == layer);
        expect(p.getLayerTypeId(11) == LayerTypeId::Kick && p.getLayerTypeId(2) == LayerTypeId::Empty);
        expectEquals(p.getLayerName(11), juce::String("Thump"));
        expectEquals(p.getLayerColourIndex(11), colour);
        expect(p.swapLayers(0, 15), "two empty slots");
        expect(! p.swapLayers(4, 4));
        expect(! p.swapLayers(0, 16));
        p.flushPendingAsyncUpdates();
    }

    void mixedArpAndKickRows()
    {
        beginTest("an Arp and a Kick swapped: each layer's rows land under its own prefix letter, the saved state restores");
        mu_seam_test::TempStorage storage;
        SwapProcessor p;
        expect(p.setLayerType(1, LayerTypeId::Arp));
        expect(p.setLayerType(2, LayerTypeId::Kick));
        mu_seam_test::setParam(p.apvts, "a1_cut", 2200.0f);
        mu_seam_test::setParam(p.apvts, "t2_swing", 0.4f);
        mu_seam_test::setParam(p.apvts, "ch1_lvl", 0.35f);
        mu_seam_test::setParam(p.apvts, "ch2_lvl", 0.85f);
        auto* arp = p.getLayer(1);
        auto* kick = p.getLayer(2);

        expect(p.swapLayers(1, 2));
        expect(p.getLayer(2) == arp && p.getLayer(1) == kick);
        expect(p.getLayerTypeId(1) == LayerTypeId::Kick && p.getLayerTypeId(2) == LayerTypeId::Arp);
        expectWithinAbsoluteError(getPlain(p.apvts, "a2_cut"), 2200.0f, 1.0f);
        expectWithinAbsoluteError(getPlain(p.apvts, "t1_swing"), 0.4f, 0.001f);
        expectWithinAbsoluteError(getPlain(p.apvts, "ch2_lvl"), 0.35f, 0.001f);
        expectWithinAbsoluteError(getPlain(p.apvts, "ch1_lvl"), 0.85f, 0.001f);
        expect(! p.getLayerName(1).isEmpty());

        const auto state = p.captureState();
        const int tRows = rowCount(mu_pp::findLayerNode(state, 1));
        expect(rowIds(mu_pp::findLayerNode(state, 1)).contains("swing") && ! rowIds(mu_pp::findLayerNode(state, 1)).contains("cut"),
               "slot 1 saves the Kick's rows only");
        expect(rowIds(mu_pp::findLayerNode(state, 2)).contains("cut") && ! rowIds(mu_pp::findLayerNode(state, 2)).contains("swing"),
               "slot 2 saves the Arp's rows only");
        expect(tRows > 0 && rowCount(mu_pp::findLayerNode(state, 2)) == 63);

        juce::MemoryBlock block;
        p.getStateInformation(block);
        mu_seam_test::TempStorage storage2;
        SwapProcessor q;
        q.setStateInformation(block.getData(), (int) block.getSize());
        expect(q.getLayerTypeId(1) == LayerTypeId::Kick && q.getLayerTypeId(2) == LayerTypeId::Arp);
        expectWithinAbsoluteError(getPlain(q.apvts, "a2_cut"), 2200.0f, 1.0f);
        expectWithinAbsoluteError(getPlain(q.apvts, "t1_swing"), 0.4f, 0.001f);
        expect(q.captureState().isEquivalentTo(state), "the swapped state restores and captures the same");

        expect(p.swapLayers(1, 2), "and back");
        expectWithinAbsoluteError(getPlain(p.apvts, "a1_cut"), 2200.0f, 1.0f);
        expectWithinAbsoluteError(getPlain(p.apvts, "t2_swing"), 0.4f, 0.001f);
        p.flushPendingAsyncUpdates();
        q.flushPendingAsyncUpdates();
    }

    // The hook once read the types from the layer map after the pointers moved, with the indices from before: the source
    // of a Rumble then followed the wrong layer. With the Rumble in slot 6 (not 0) and the source and the Rumble each
    // swapped, a Rumble keeps listening to the layer it listened to.
    void rumbleSourceFollowsSwaps()
    {
        beginTest("a Rumble's source follows its layer when the source, the Rumble or an unrelated slot is swapped");
        mu_seam_test::TempStorage storage;
        SwapProcessor p;
        buildScene(p);
        auto src = [&](int slot) { return (int) std::lround(getPlain(p.apvts, "t" + juce::String(slot) + "_src")); };
        expectEquals(src(6), 3, "the Rumble in slot 6 listens to the Kick in 3");

        expect(p.swapLayers(3, 9));   // the Kick moves to 9, the Bass to 3
        expectEquals(src(6), 9, "the source followed the Kick");
        expect(p.swapLayers(0, 12));  // slots the Rumble does not use
        expectEquals(src(6), 9);
        expect(p.swapLayers(6, 1));   // the Rumble itself moves to slot 1
        expect(p.getLayerTypeId(1) == LayerTypeId::Rumble);
        expectEquals(src(1), 9, "its source value moved with it");
        expect(p.swapLayers(9, 14));  // the Kick again, now past the Rumble
        expectEquals(src(1), 14);
        expect(p.getLayerTypeId(14) == LayerTypeId::Kick);
        expect(p.swapLayers(14, 1));  // the Kick and the Rumble swap with each other
        expect(p.getLayerTypeId(14) == LayerTypeId::Rumble && p.getLayerTypeId(1) == LayerTypeId::Kick);
        expectEquals(src(14), 1, "a Rumble swapped with its own source still listens to it");
        p.flushPendingAsyncUpdates();
    }

    // Reordering moves nothing in the mix: the same scene, with a swap between blocks, sums to the same master.
    void mixIsUnchanged()
    {
        beginTest("swapping Kick, Bass, Arp, Rumble and empty slots between blocks leaves the mix unchanged, at block sizes 1, 7, 64 and 512");
        for (const int blockSize : { 1, 7, 64, 512 })
        {
            mu_seam_test::TempStorage storage;
            SwapProcessor ref, swapped;
            buildScene(ref);
            buildScene(swapped);
            const int totalFrames = 36000;   // a beat and a half: two Kick hits, the Rumble's tail, the Arp's steps
            const int swapEvery = juce::jmax(1, 256 / blockSize);
            const int pool[] = { 0, 3, 5, 6, 9, 12, 15 };

            float refPeak = 0.0f, worstDiff = 0.0f, worstStep = 0.0f, worstRefStep = 0.0f, lastL = 0.0f, lastRefL = 0.0f;
            int swaps = 0;
            std::uint32_t rng = 4242u;
            {
                Stream a(ref, blockSize), b(swapped, blockSize);
                for (int f = 0, block = 0; f < totalFrames; f += blockSize, ++block)
                {
                    if (block % swapEvery == swapEvery - 1)
                    {
                        rng = rng * 1664525u + 1013904223u;
                        const int x = pool[(rng >> 8) % 7u];
                        rng = rng * 1664525u + 1013904223u;
                        const int y = pool[(rng >> 8) % 7u];
                        if (x != y) { expect(swapped.swapLayers(x, y)); ++swaps; }
                    }
                    const auto& outRef = a.block();
                    const auto& out = b.block();
                    for (int i = 0; i < blockSize; ++i)
                    {
                        for (int c = 0; c < 2; ++c)
                        {
                            refPeak = juce::jmax(refPeak, std::abs(outRef.getSample(c, i)));
                            worstDiff = juce::jmax(worstDiff, std::abs(out.getSample(c, i) - outRef.getSample(c, i)));
                        }
                        worstStep = juce::jmax(worstStep, std::abs(out.getSample(0, i) - lastL));
                        worstRefStep = juce::jmax(worstRefStep, std::abs(outRef.getSample(0, i) - lastRefL));
                        lastL = out.getSample(0, i);
                        lastRefL = outRef.getSample(0, i);
                    }
                }
            }
            const juce::String tag = "block " + juce::String(blockSize) + ": ";
            expect(refPeak > 0.01f, tag + "the scene sounds (peak " + juce::String(refPeak, 4) + ")");
            expect(swaps > 10, tag + "swapped " + juce::String(swaps) + " times");
            expect(worstDiff < 1.0e-4f, tag + "the swapped mix equals the reference (worst " + juce::String(worstDiff, 8) + ")");
            expect(worstStep <= worstRefStep + 1.0e-4f, tag + "no step the reference does not have (" + juce::String(worstStep, 6)
                                                              + " vs " + juce::String(worstRefStep, 6) + ")");
            expectEquals(swapped.layerAccess.views.overflowCount(), 0);
            ref.flushPendingAsyncUpdates();
            swapped.flushPendingAsyncUpdates();
        }
    }

    // The message thread swaps as fast as it can while a second thread renders the playing scene.
    void swapStress()
    {
        beginTest("two-thread stress with the real layers playing: swaps allocate and free nothing on the audio thread");
        constexpr int kSwaps = 800;
        constexpr int kBlock = 128;
        mu_seam_test::TempStorage storage;
        SwapProcessor p;
        buildScene(p);
        mu_golden::MockHost host;
        p.setPlayHead(&host);
        p.setRateAndBufferSizeDetails(48000.0, kBlock);
        p.prepareToPlay(48000.0, kBlock);
        {   // the first block after a prepare sizes the host MIDI buffer (one allocation, at start-up): not counted here
            juce::AudioBuffer<float> warm(2, kBlock);
            juce::MidiBuffer warmMidi;
            p.processBlock(warm, warmMidi);
        }
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
        std::uint32_t rng = 99u;
        int done = 0, refused = 0;
        double totalMs = 0.0, worstMs = 0.0;   // how long the blocks are kept out per swap (logged, not asserted)
        for (int i = 0; i < kSwaps; ++i)
        {
            rng = rng * 1664525u + 1013904223u;
            const int x = (int) ((rng >> 8) % 16u);
            rng = rng * 1664525u + 1013904223u;
            const int y = (int) ((rng >> 8) % 16u);
            if (x == y) continue;
            const double t0 = juce::Time::getMillisecondCounterHiRes();
            if (p.swapLayers(x, y)) ++done; else ++refused;
            const double took = juce::Time::getMillisecondCounterHiRes() - t0;
            totalMs += took;
            worstMs = juce::jmax(worstMs, took);
            if (i % 50 == 0) p.flushPendingAsyncUpdates();
        }
        stop.store(true);
        audio.join();
        p.flushPendingAsyncUpdates();
        p.setPlayHead(nullptr);
        p.releaseResources();
        logMessage("swap stress: " + juce::String(done) + " swaps, " + juce::String(refused) + " refused, " + juce::String(blocks.load())
                   + " blocks, " + juce::String(p.layerAccess.views.overflowCount()) + " overflow views, a swap took "
                   + juce::String(totalMs / juce::jmax(1, done + refused), 3) + " ms on average, " + juce::String(worstMs, 3) + " ms at worst");
        expectEquals(allocs.load() + frees.load(), 0, "the audio thread allocated or freed");
        expectGreaterThan(blocks.load(), 50, "audio blocks ran during the swaps");
        expectGreaterThan(done, kSwaps / 2, "most swaps went through");

        // Whatever order the layers ended in, the four kinds are all still there and the Rumble listens to the Kick.
        int kicks = 0, rumbles = 0;
        int kickSlot = -1, rumbleSlot = -1;
        for (int s = 0; s < 16; ++s)
        {
            if (p.getLayerTypeId(s) == LayerTypeId::Kick)   { ++kicks; kickSlot = s; }
            if (p.getLayerTypeId(s) == LayerTypeId::Rumble) { ++rumbles; rumbleSlot = s; }
        }
        expectEquals(kicks, 1);
        expectEquals(rumbles, 1);
        if (kicks == 1 && rumbles == 1)
            expectEquals((int) std::lround(getPlain(p.apvts, "t" + juce::String(rumbleSlot) + "_src")), kickSlot,
                         "after hundreds of swaps the Rumble still listens to the Kick");
    }
};

static MuTronicSwapTests muTronicSwapTests;
