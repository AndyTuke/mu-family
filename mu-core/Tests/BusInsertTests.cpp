#include "BusTestRig.h"
#include "AllocationProbe.h"   // the counting operators are in MidiControlTests.cpp (one TU per executable)
#include "Audio/AlgorithmNames.h"
#include "Audio/InsertProcessor.h"
#include <cstring>

// The group bus insert chain (MixerEngine::BusState::insert): each insert is the layer's insert, so a bus
// insert must produce, bit for bit, what the shared InsertProcessor produces for the same input and
// parameters; the chain runs insert 0 then insert 1; an unused insert changes nothing. These tests run in a
// build that has buses with whatever MU_BUS_INSERTS it was built with (mu-core-tests-16: 2, mu-core-tests-bus1: 1).
struct BusInsertTests : juce::UnitTest
{
    BusInsertTests() : juce::UnitTest("Group bus inserts", "mu-core") {}

    using Rig = bus_test::Rig;
    static constexpr int kBlocks = 6;

    // Copies each channel buffer as the mixer gives it (before the headroom trim), so the test can feed a
    // reference InsertProcessor exactly what the bus insert sees.
    struct Tap : MixerEngine::ChannelTap
    {
        juce::AudioBuffer<float> last { 2, bus_test::kBlock };
        void capture(int channel, const juce::AudioBuffer<float>& buf, int n) noexcept override
        {
            if (channel != 0) return;
            for (int c = 0; c < 2; ++c) last.copyFrom(c, 0, buf, c, 0, n);
        }
    };

    static void setInsert(MixerEngine::BusInsert& ins, int algo, float p)
    {
        ins.insertAlgo.store(algo);
        for (auto& v : ins.insertParam) v.store(p);
    }

    static VoiceParams paramsFor(int algo, float p)
    {
        VoiceParams vp;
        vp.insertAlgo = algo;
        for (auto& v : vp.insertParam) v = p;
        return vp;
    }

    // Reference insert seeded exactly as the engine seeds bus b's insert i.
    static void prepareRef(InsertProcessor& ip, int bus, int slot)
    {
        ip.setSlotIndex(MixerEngine::MaxChannels + bus * MixerEngine::kBusInserts + slot);
        ip.prepare(bus_test::kRate, bus_test::kBlock);
    }

    static bool sameBits(const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b)
    {
        for (int c = 0; c < 2; ++c)
            if (std::memcmp(a.getReadPointer(c), b.getReadPointer(c), sizeof(float) * (size_t) bus_test::kBlock) != 0)
                return false;
        return true;
    }

    // Runs a rig with channel 0 (tone) on bus `bus` and returns whether every block's master output equals the
    // reference chain's output bit for bit. `chain` lists (algo, param) per insert slot from 0.
    bool chainMatches(const std::vector<std::pair<int, float>>& chain, int bus = 0)
    {
        Rig r;
        Tap tap;
        r.mixer.channelTap = &tap;
        r.tone[0] = 0.8f;
        r.mixer.channels[0].busAssign.store(bus + 1);
        std::vector<InsertProcessor> refs(chain.size());
        for (size_t i = 0; i < chain.size(); ++i)
        {
            setInsert(r.insertAt(bus, (int) i), chain[i].first, chain[i].second);
            prepareRef(refs[i], bus, (int) i);
        }
        for (int b = 0; b < kBlocks; ++b)
        {
            r.run();
            juce::AudioBuffer<float> ref(2, bus_test::kBlock);
            ref.makeCopyOf(tap.last);
            ref.applyGain(bus_test::kTrim);
            for (size_t i = 0; i < chain.size(); ++i)
                refs[i].process(ref, bus_test::kBlock, 2, paramsFor(chain[i].first, chain[i].second));
            if (! sameBits(r.out, ref)) return false;
        }
        r.mixer.channelTap = nullptr;
        return true;
    }

    void runTest() override
    {
        if (! bus_test::hasBuses(*this)) { beginTest("no buses in this build"); expect(true); return; }
        logMessage("buses " + juce::String(mu_limits::kMaxBuses) + ", inserts per bus " + juce::String(MixerEngine::kBusInserts));

        beginTest("each bus insert algorithm is the layer insert: bit-identical to InsertProcessor on the same input");
        for (int algo = 0; algo < mu_audio::kInsertAlgorithmCount; ++algo)
            expect(chainMatches({ { algo, 0.5f } }), "algorithm " + juce::String(algo) + " (" + mu_audio::kInsertAlgorithmNames[algo] + ")");

        beginTest("every bus and every insert slot carries the same insert behaviour");
        for (int b = 0; b < mu_limits::kMaxBuses; ++b)
            for (int slot = 0; slot < MixerEngine::kBusInserts; ++slot)
            {
                // Fill the slots before `slot` with the pass-through None insert.
                std::vector<std::pair<int, float>> chain(slot + 1, { 0, 0.0f });
                chain.back() = { 2, 0.7f };   // HardClip, an audible change
                expect(chainMatches(chain, b), "bus " + juce::String(b + 1) + " insert " + juce::String(slot + 1));
            }

        beginTest("an unused insert (None) is a bit-exact pass-through");
        {
            Rig onBus, onMaster;
            for (auto* r : { &onBus, &onMaster }) r->tone[0] = 0.8f;
            onBus.mixer.channels[0].busAssign.store(1);
            for (int b = 0; b < kBlocks; ++b)
            {
                onBus.run(); onMaster.run();
                expect(sameBits(onBus.out, onMaster.out), "block " + juce::String(b));
            }
        }

        beginTest("a bus insert changes only its own bus");
        {
            Rig r;
            r.tone[0] = 0.8f; r.tone[1] = 0.8f;
            r.mixer.channels[0].busAssign.store(1);
            r.mixer.channels[1].busAssign.store(juce::jmin(2, mu_limits::kMaxBuses));
            setInsert(r.insertAt(0, 0), 2, 0.9f);   // HardClip on bus 1 only
            r.run(3);
            if (mu_limits::kMaxBuses >= 2)
            {
                // Bus 2 carries a plain trimmed tone (peak at most 0.8 x trim), bus 1 is clipped lower.
                expect(r.mixer.busPeaks[1].load() > r.mixer.busPeaks[0].load() + 1.0e-3f, "bus 1 is clipped, bus 2 is not");
            }
        }

        if (MixerEngine::kBusInserts >= 2)
        {
            beginTest("insert 1 sees insert 0's output: the chain equals the two reference inserts in order");
            expect(chainMatches({ { 2, 0.7f }, { 4, 0.7f } }), "HardClip then Bitcrusher");
            expect(chainMatches({ { 4, 0.7f }, { 2, 0.7f } }), "Bitcrusher then HardClip");
            expect(chainMatches({ { 1, 0.8f }, { 6, 0.6f } }), "SoftClip then EQ");

            beginTest("the order matters: the engine's chain is not the reversed chain");
            {
                Rig r;
                Tap tap;
                r.mixer.channelTap = &tap;
                r.tone[0] = 0.8f;
                r.mixer.channels[0].busAssign.store(1);
                setInsert(r.insertAt(0, 0), 2, 0.7f);   // HardClip
                setInsert(r.insertAt(0, 1), 4, 0.7f);   // Bitcrusher
                InsertProcessor first, second;
                prepareRef(first, 0, 1);    // reversed: Bitcrusher first
                prepareRef(second, 0, 0);
                float maxDiff = 0.0f;
                for (int b = 0; b < kBlocks; ++b)
                {
                    r.run();
                    juce::AudioBuffer<float> ref(2, bus_test::kBlock);
                    ref.makeCopyOf(tap.last);
                    ref.applyGain(bus_test::kTrim);
                    first.process(ref, bus_test::kBlock, 2, paramsFor(4, 0.7f));
                    second.process(ref, bus_test::kBlock, 2, paramsFor(2, 0.7f));
                    for (int i = 0; i < bus_test::kBlock; ++i)
                        maxDiff = juce::jmax(maxDiff, std::abs(r.out.getSample(0, i) - ref.getSample(0, i)));
                }
                r.mixer.channelTap = nullptr;
                expect(maxDiff > 1.0e-4f, "reversed order differs (max difference " + juce::String(maxDiff, 6) + ")");
            }
        }

        beginTest("the insert chain runs before the bus fader and pan");
        {
            Rig r;
            r.dc[0] = 0.8f;
            r.mixer.channels[0].busAssign.store(1);
            setInsert(r.insertAt(0, 0), 2, 0.3f);
            r.run(40);   // the insert's own parameter smoothing settles
            const float clipped = r.mixer.busPeaks[0].load();
            r.mixer.buses[0].level.store(0.5f);
            r.run(2);
            expectWithinAbsoluteError(r.mixer.busPeaks[0].load(), clipped * 0.5f, 1.0e-6f, "fader scales the inserted signal");
        }

        beginTest("a Compressor on a bus insert reports gain reduction; None reports none");
        {
            Rig r;
            r.dc[0] = 0.8f;
            r.mixer.channels[0].busAssign.store(1);
            setInsert(r.insertAt(0, 0), 7, 1.0f);   // Compressor, threshold at -40 dB
            float gr = 0.0f;
            for (int b = 0; b < 40; ++b) { r.run(); gr = juce::jmax(gr, r.insertAt(0, 0).processor.grReduction.load()); }
            expect(gr > 0.05f, "compressor GR " + juce::String(gr, 4));
            expectEquals(r.insertAt(mu_limits::kMaxBuses - 1, 0).processor.grReduction.load(), 0.0f, "an idle insert reads zero");
            setInsert(r.insertAt(0, 0), 0, 0.0f);
            r.run(2);
            expectEquals(r.insertAt(0, 0).processor.grReduction.load(), 0.0f, "None reports no reduction");
        }

        beginTest("a block through buses, inserts and a bus sidechain allocates nothing");
        {
            Rig r;
            r.tone[0] = 0.8f; r.tone[1] = 0.8f;
            r.mixer.channels[0].busAssign.store(1);
            r.mixer.channels[1].busAssign.store(juce::jmin(2, mu_limits::kMaxBuses));
            for (int b = 0; b < mu_limits::kMaxBuses; ++b)
                for (int i = 0; i < MixerEngine::kBusInserts; ++i)
                    setInsert(r.insertAt(b, i), 7 + (i % 2), 0.6f);
            if (mu_limits::kMaxBuses >= 2)
            {
                auto& b2 = r.mixer.buses[1];
                b2.sidechainSource.store(MixerEngine::busSidechainSource(0));
                b2.sidechainAmount.store(0.5f);
            }
            r.mixer.returns[2].sidechainSource.store(MixerEngine::busSidechainSource(0));
            r.mixer.returns[2].sidechainAmount.store(0.5f);
            r.mixer.buses[0].sendReverb.store(0.5f);
            r.run(4);   // warm up
            mu_alloc::Armed armed;
            r.run(4);
            armed.stop();
            expectEquals(armed.allocs(), 0, "allocations in four blocks");
        }

        beginTest("a NaN in a bus insert is contained and the inserts are reset");
        {
            Rig r;
            r.dc[0] = std::numeric_limits<float>::quiet_NaN();
            r.mixer.channels[0].busAssign.store(1);
            r.run();
            expectEquals(r.mixer.nonFiniteBlocks.load(), 1, "the block was silenced");
            expectEquals(r.outMax(), 0.0f, "silent output");
            r.dc[0] = 0.5f;
            r.run();
            expect(std::isfinite(r.outL()) && r.outL() != 0.0f, "the next block plays");
        }
    }
};

static BusInsertTests busInsertTests;
