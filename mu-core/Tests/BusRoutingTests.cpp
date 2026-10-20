#include "BusTestRig.h"

// Group-bus routing in the shared mixer (design: docs/mu-tronic/design.md section 5): a channel's dry signal
// goes to its bus (ChannelState::busAssign) or the master, the bus sums and feeds the master through its own
// fader, and the mute / solo rules judge paths. The sources are constants, so every expected output is an
// exact product of the headroom trim and the faders.
struct BusRoutingTests : juce::UnitTest
{
    BusRoutingTests() : juce::UnitTest("Group bus routing", "mu-core") {}

    using Rig = bus_test::Rig;
    static constexpr float kTrim = bus_test::kTrim;

    void runTest() override
    {
        if (! bus_test::hasBuses(*this, 2)) { beginTest("no buses in this build"); expect(true); return; }

        beginTest("a channel defaults to the master: the output is the channel alone and the buses are silent");
        {
            Rig r;
            r.dc[0] = 0.8f;
            r.run();
            expectEquals(r.outL(), 0.8f * kTrim, "master mix");
            for (size_t b = 0; b < r.mixer.buses.size(); ++b)
                expectEquals(r.mixer.busPeaks[b].load(), 0.0f, "bus " + juce::String((int) b) + " peak");
        }

        beginTest("a channel assigned to a bus reaches the master through the bus fader");
        {
            Rig r;
            r.dc[0] = 0.8f;
            r.mixer.channels[0].busAssign.store(1);
            r.mixer.buses[0].level.store(0.5f);
            r.run();
            expectEquals(r.outL(), 0.8f * kTrim * 0.5f, "bus fader scales the output");
            expectEquals(r.mixer.busPeaks[0].load(), 0.8f * kTrim * 0.5f, "bus 1 peak is post-fader");
            expectEquals(r.mixer.channelPeaks[0].load(), 0.8f * kTrim, "the channel's own peak is unchanged");
            expectEquals(r.mixer.busPeaks[1].load(), 0.0f, "bus 2 is untouched");
        }

        beginTest("reassigning mid-play moves the signal from one bus to the next block's destination");
        {
            Rig r;
            r.dc[0] = 0.8f;
            r.mixer.buses[0].level.store(0.5f);
            r.mixer.buses[1].level.store(0.25f);
            r.mixer.channels[0].busAssign.store(1);
            r.run(2);
            expectEquals(r.outL(), 0.8f * kTrim * 0.5f, "on bus 1");
            r.mixer.channels[0].busAssign.store(2);
            r.run();
            expectEquals(r.outL(), 0.8f * kTrim * 0.25f, "on bus 2 the next block");
            expectEquals(r.mixer.busPeaks[0].load(), 0.0f, "bus 1 went quiet");
            r.mixer.channels[0].busAssign.store(0);
            r.run();
            expectEquals(r.outL(), 0.8f * kTrim, "back on the master");
            expectEquals(r.mixer.busPeaks[1].load(), 0.0f, "bus 2 went quiet");
        }

        beginTest("a bus sums its channels and the master-routed channels add to it");
        {
            Rig r;
            r.dc[0] = 0.4f; r.dc[1] = 0.2f; r.dc[2] = 0.6f;
            r.mixer.channels[0].busAssign.store(1);
            r.mixer.channels[1].busAssign.store(1);
            r.mixer.channels[2].busAssign.store(0);
            r.run();
            expectWithinAbsoluteError(r.mixer.busPeaks[0].load(), (0.4f + 0.2f) * kTrim, 1.0e-6f, "bus 1 sums two channels");
            expectWithinAbsoluteError(r.outL(), (0.4f + 0.2f + 0.6f) * kTrim, 1.0e-6f, "master = bus + the direct-to-master channel");
        }

        beginTest("a bus has its own pan");
        {
            Rig r;
            r.dc[0] = 0.8f;
            r.mixer.channels[0].busAssign.store(1);
            r.mixer.buses[0].pan.store(1.0f);   // hard right
            r.run();
            expectEquals(r.outL(), 0.0f, "left side silent");
            expectEquals(r.outR(), 0.8f * kTrim, "right side full");
            expectEquals(r.mixer.busPeaksL[0].load(), 0.0f, "left bus peak");
        }

        beginTest("a muted bus silences the dry path of its channels, not the other channels");
        {
            Rig r;
            r.dc[0] = 0.4f; r.dc[1] = 0.6f;
            r.mixer.channels[0].busAssign.store(1);
            r.mixer.buses[0].mute.store(true);
            r.run();
            expectEquals(r.outL(), 0.6f * kTrim, "only the master-routed channel is heard");
            expectEquals(r.mixer.busPeaks[0].load(), 0.0f, "the muted bus reads silent");
        }

        beginTest("a muted bus leaves its channels' FX sends alone");
        {
            // Channel 0 sends to the reverb while routed to a muted bus: the return still sounds, the dry does not.
            Rig r;
            r.tone[0] = 0.8f;
            r.mixer.channels[0].busAssign.store(1);
            r.mixer.channels[0].sendReverb.store(1.0f);
            r.mixer.buses[0].mute.store(true);
            float maxRet = 0.0f;
            for (int b = 0; b < 40; ++b) { r.run(); maxRet = juce::jmax(maxRet, r.mixer.returnPeaks[2].load()); }
            expect(maxRet > 1.0e-4f, "the reverb return sounds (" + juce::String(maxRet, 6) + ")");

            // The same channel muted itself sends nothing.
            Rig m;
            m.tone[0] = 0.8f;
            m.mixer.channels[0].busAssign.store(1);
            m.mixer.channels[0].sendReverb.store(1.0f);
            m.mixer.channels[0].mute.store(true);
            float maxMuted = 0.0f;
            for (int b = 0; b < 40; ++b) { m.run(); maxMuted = juce::jmax(maxMuted, m.mixer.returnPeaks[2].load()); }
            expectEquals(maxMuted, 0.0f, "a muted channel feeds no send");
        }

        beginTest("a channel's sends are taken post-fader whatever its routing");
        {
            Rig toBus, toMaster;
            for (auto* r : { &toBus, &toMaster })
            {
                r->tone[0] = 0.8f;
                r->mixer.channels[0].sendReverb.store(1.0f);
            }
            toBus.mixer.channels[0].busAssign.store(1);
            toBus.mixer.buses[0].level.store(0.1f);   // the bus fader must not change what the channel sends
            float a = 0.0f, b = 0.0f;
            for (int i = 0; i < 40; ++i)
            {
                toBus.run(); toMaster.run();
                a = juce::jmax(a, toBus.mixer.returnPeaks[2].load());
                b = juce::jmax(b, toMaster.mixer.returnPeaks[2].load());
            }
            expect(a > 1.0e-4f, "the bus-routed channel feeds the reverb");
            expectWithinAbsoluteError(a, b, 1.0e-6f, "same return level as the master-routed twin");
        }

        beginTest("a bus has its own FX sends, taken after its fader");
        {
            Rig r;
            r.tone[0] = 0.8f;
            r.mixer.channels[0].busAssign.store(1);
            r.mixer.buses[0].sendReverb.store(1.0f);
            float maxRet = 0.0f;
            for (int b = 0; b < 40; ++b) { r.run(); maxRet = juce::jmax(maxRet, r.mixer.returnPeaks[2].load()); }
            expect(maxRet > 1.0e-4f, "the bus send reaches the reverb return (" + juce::String(maxRet, 6) + ")");
        }

        beginTest("solo: a soloed channel makes its bus audible and cuts everything off its path");
        {
            Rig r;
            r.dc[0] = 0.4f; r.dc[1] = 0.2f; r.dc[2] = 0.6f;
            r.mixer.channels[0].busAssign.store(1);   // soloed, on bus 1
            r.mixer.channels[1].busAssign.store(1);   // not soloed, same bus
            r.mixer.channels[2].busAssign.store(0);   // not soloed, master
            r.mixer.channels[0].solo.store(true);
            r.run();
            expectWithinAbsoluteError(r.outL(), 0.4f * kTrim, 1.0e-6f, "only the soloed channel is heard");
        }

        beginTest("solo: a soloed bus solos all its members");
        {
            Rig r;
            r.dc[0] = 0.4f; r.dc[1] = 0.2f; r.dc[2] = 0.6f; r.dc[3] = 0.1f;
            r.mixer.channels[0].busAssign.store(2);
            r.mixer.channels[1].busAssign.store(2);
            r.mixer.channels[2].busAssign.store(1);
            r.mixer.channels[3].busAssign.store(0);
            r.mixer.buses[1].solo.store(true);
            r.run();
            expectWithinAbsoluteError(r.outL(), (0.4f + 0.2f) * kTrim, 1.0e-6f, "bus 2's channels only");
            expectEquals(r.mixer.busPeaks[0].load(), 0.0f, "bus 1 is off the soloed path");
        }

        beginTest("solo: a soloed channel on a muted bus stays silent (the mute is the bus's)");
        {
            Rig r;
            r.dc[0] = 0.4f; r.dc[1] = 0.6f;
            r.mixer.channels[0].busAssign.store(1);
            r.mixer.channels[0].solo.store(true);
            r.mixer.buses[0].mute.store(true);
            r.run();
            expectEquals(r.outL(), 0.0f, "nothing is heard");
        }

        beginTest("a DAW direct out wins: that channel feeds neither the bus nor the master");
        {
            Rig r;
            r.dc[0] = 0.8f;
            r.mixer.channels[0].busAssign.store(1);
            r.mixer.channels[0].outputBus.store(1);   // direct out 1
            juce::AudioBuffer<float> direct(2, bus_test::kBlock);
            direct.clear();
            r.run(1, &direct);
            expectEquals(r.outL(), 0.0f, "master is silent");
            expectEquals(r.mixer.busPeaks[0].load(), 0.0f, "bus is silent");
            expectEquals(direct.getSample(0, bus_test::kBlock - 1), 0.8f * kTrim, "the direct out carries the channel");
        }

        beginTest("an out-of-range assignment is treated as the master");
        {
            Rig r;
            r.dc[0] = 0.8f;
            r.mixer.channels[0].busAssign.store(mu_limits::kMaxBuses + 1);
            r.run();
            expectEquals(r.outL(), 0.8f * kTrim, "master mix");
        }

        beginTest("returns have no bus: an FX return reaches the master with a muted bus present");
        {
            Rig r;
            r.tone[0] = 0.8f;
            r.mixer.channels[0].sendReverb.store(1.0f);   // master-routed channel
            for (auto& b : r.mixer.buses) b.mute.store(true);
            float maxRet = 0.0f, maxOut = 0.0f;
            for (int b = 0; b < 40; ++b)
            {
                r.run();
                maxRet = juce::jmax(maxRet, r.mixer.returnPeaks[2].load());
                maxOut = juce::jmax(maxOut, r.outMax());
            }
            expect(maxRet > 1.0e-4f && maxOut > 1.0e-4f, "the return sounds at the master");
        }
    }
};

static BusRoutingTests busRoutingTests;
