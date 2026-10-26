#include "BusTestRig.h"
#include "TestProcessorBase.h"
#include "Plugin/MixerFxParams.h"

// The bus assignment is part of the channel strip: copyFrom, reset and swapChannelState carry it, a fresh
// strip is on the master, and the ch{N}_bus / bus{B}_* parameters reach the engine (including two-digit
// channel indices, which a 16-layer host uses).
struct BusStateResetTests : juce::UnitTest
{
    BusStateResetTests() : juce::UnitTest("Group bus state (copy, reset, swap, parameter sync)", "mu-core") {}

    struct StubProcessor : juce::AudioProcessor
    {
        const juce::String getName() const override { return "stub"; }
        void prepareToPlay(double, int) override {}
        void releaseResources() override {}
        void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override {}
        juce::AudioProcessorEditor* createEditor() override { return nullptr; }
        bool hasEditor() const override { return false; }
        bool acceptsMidi() const override { return false; }
        bool producesMidi() const override { return false; }
        double getTailLengthSeconds() const override { return 0.0; }
        int getNumPrograms() override { return 1; }
        int getCurrentProgram() override { return 0; }
        void setCurrentProgram(int) override {}
        const juce::String getProgramName(int) override { return {}; }
        void changeProgramName(int, const juce::String&) override {}
        void getStateInformation(juce::MemoryBlock&) override {}
        void setStateInformation(const void*, int) override {}
    };

    // A processor declaring every channel strip, the buses and the global rack, with the engine synced.
    struct SyncHost : TestProcessorBase
    {
        SyncHost() : TestProcessorBase(WrapperType::wrapperType_Undefined, makeLayout())
        {
            startFxParamSync();
        }
        static juce::AudioProcessorValueTreeState::ParameterLayout makeLayout()
        {
            juce::AudioProcessorValueTreeState::ParameterLayout layout;
            for (int i = 0; i < mu_limits::kMaxLayers; ++i)
                mu_mixfx::addChannelStripParams(layout, "ch" + juce::String(i) + "_", "Ch ");
            mu_mixfx::addBusParams(layout);
            mu_mixfx::addGlobalFxParams(layout);
            return layout;
        }
        void set(const juce::String& id, float plain)
        {
            if (auto* p = apvts.getParameter(id)) p->setValueNotifyingHost(p->convertTo0to1(plain));
            else jassertfalse;
        }
    };

    void runTest() override
    {
        if (! bus_test::hasBuses(*this)) { beginTest("no buses in this build"); expect(true); return; }

        beginTest("copyFrom carries the bus assignment");
        {
            MixerEngine::ChannelState a, b;
            a.busAssign.store(2);
            b.copyFrom(a);
            expectEquals(b.busAssign.load(), 2);
        }

        beginTest("reset returns a strip to the master");
        {
            MixerEngine::ChannelState a;
            a.busAssign.store(mu_limits::kMaxBuses);
            a.reset();
            expectEquals(a.busAssign.load(), 0);
        }

        beginTest("swapChannelState swaps the bus assignment with the strip");
        {
            MixerEngine m;
            m.channels[1].busAssign.store(2);
            m.channels[3].busAssign.store(1);
            m.swapChannelState(1, 3);
            expectEquals(m.channels[1].busAssign.load(), 1);
            expectEquals(m.channels[3].busAssign.load(), 2);
        }

        beginTest("removing a layer shifts the assignments down with the strips (copyFrom) and the vacated top strip resets");
        {
            MixerEngine m;
            const int n = 5;
            for (int i = 0; i < n; ++i) m.channels[(size_t) i].busAssign.store(i % (mu_limits::kMaxBuses + 1));
            const int removed = 1;
            // The same sequence the processor runs when a layer is removed.
            for (int d = removed; d < n - 1; ++d) m.channels[(size_t) d].copyFrom(m.channels[(size_t) (d + 1)]);
            m.channels[(size_t) (n - 1)].reset();
            expectEquals(m.channels[1].busAssign.load(), 2 % (mu_limits::kMaxBuses + 1), "layer 2's assignment moved to slot 1");
            expectEquals(m.channels[(size_t) (n - 1)].busAssign.load(), 0, "the vacated slot is on the master");
        }

        beginTest("the reset value is the default of the ch{N}_bus parameter, and it is declared once per channel");
        {
            juce::AudioProcessorValueTreeState::ParameterLayout layout;
            for (int c = 0; c < mu_limits::kMaxLayers; ++c)
                mu_mixfx::addChannelStripParams(layout, "ch" + juce::String(c) + "_", "Ch ");
            StubProcessor stub;
            juce::AudioProcessorValueTreeState apvts(stub, nullptr, "BusReset", std::move(layout));
            MixerEngine::ChannelState st;
            st.busAssign.store(3);
            st.reset();
            for (int c = 0; c < mu_limits::kMaxLayers; ++c)
            {
                auto* p = apvts.getParameter("ch" + juce::String(c) + "_bus");
                expect(p != nullptr, "ch" + juce::String(c) + "_bus is declared");
                if (p != nullptr) expectEquals(juce::roundToInt(p->convertFrom0to1(p->getDefaultValue())), st.busAssign.load());
            }
        }

        beginTest("parameters reach the engine: ch{N}_bus, the bus strips and the bus inserts");
        {
            SyncHost host;
            auto& m = host.mixerEngine;
            host.set("ch2_bus", 2.0f);
            expectEquals(m.channels[2].busAssign.load(), 2, "ch2_bus");
            host.set("ch" + juce::String(mu_limits::kMaxLayers - 1) + "_bus", 1.0f);
            expectEquals(m.channels[(size_t) (mu_limits::kMaxLayers - 1)].busAssign.load(), 1, "the highest channel index (two digits at 16 layers)");
            if (mu_limits::kMaxLayers > 10)
            {
                host.set("ch12_lvl", 0.25f);
                expectWithinAbsoluteError(m.channels[12].level.load(), 0.25f, 1.0e-4f, "ch12_lvl (a two-digit strip is synced)");
            }

            host.set("bus1_lvl", 0.4f);
            host.set("bus1_pan", -0.5f);
            host.set("bus1_mute", 1.0f);
            host.set("bus1_solo", 1.0f);
            host.set("bus1_sendRev", 0.3f);
            host.set("bus1_scAmt", 0.7f);
            host.set("bus1_scSrc", 1.0f);   // parameter 1 = channel 0
            auto& b1 = m.buses[1];
            expectWithinAbsoluteError(b1.level.load(), 0.4f, 1.0e-4f, "bus1_lvl");
            expectWithinAbsoluteError(b1.pan.load(), -0.5f, 1.0e-4f, "bus1_pan");
            expect(b1.mute.load() && b1.solo.load(), "bus1 mute / solo");
            expectWithinAbsoluteError(b1.sendReverb.load(), 0.3f, 1.0e-4f, "bus1_sendRev");
            expectWithinAbsoluteError(b1.sidechainAmount.load(), 0.7f, 1.0e-4f, "bus1_scAmt");
            expectEquals(b1.sidechainSource.load(), 0, "bus1_scSrc is parameter - 1");
            host.set("bus1_scSrc", (float) (mu_limits::kMaxLayers + 2));   // parameter N + 2 = bus 0
            expectEquals(b1.sidechainSource.load(), MixerEngine::busSidechainSource(0), "bus1 listens to bus 0");

            host.set("bus0_i0_drvChar", 6.0f);
            host.set("bus0_i0_insP3", 0.35f);
            expectEquals(m.buses[0].insert[0].insertAlgo.load(), 6, "bus0_i0_drvChar");
            expectWithinAbsoluteError(m.buses[0].insert[0].insertParam[2].load(), 0.35f, 1.0e-4f, "bus0_i0_insP3");
            if (MixerEngine::kBusInserts >= 2)
            {
                host.set("bus0_i1_drvChar", 4.0f);
                host.set("bus0_i1_insP1", 0.8f);
                expectEquals(m.buses[0].insert[1].insertAlgo.load(), 4, "bus0_i1_drvChar");
                expectWithinAbsoluteError(m.buses[0].insert[1].insertParam[0].load(), 0.8f, 1.0e-4f, "bus0_i1_insP1");
                expectEquals(m.buses[0].insert[0].insertAlgo.load(), 6, "insert 0 untouched");
            }

            host.set("ret_rev_scSrc", (float) (mu_limits::kMaxLayers + 1 + mu_limits::kMaxBuses));   // the last bus
            expectEquals(m.returns[2].sidechainSource.load(), MixerEngine::busSidechainSource(mu_limits::kMaxBuses - 1), "a return takes the last bus");
        }

        beginTest("the declared bus parameters are exactly the strip, the insert and the assignment sets");
        {
            juce::AudioProcessorValueTreeState::ParameterLayout layout;
            mu_mixfx::addBusParams(layout);
            StubProcessor stub;
            juce::AudioProcessorValueTreeState apvts(stub, nullptr, "BusCount", std::move(layout));
            const int perBus = 12 + MixerEngine::kBusInserts * 5;   // 12 strip suffixes + (algorithm + 4 knobs) per insert
            expectEquals((int) stub.getParameters().size(), mu_limits::kMaxBuses * perBus);
        }
    }
};

static BusStateResetTests busStateResetTests;
