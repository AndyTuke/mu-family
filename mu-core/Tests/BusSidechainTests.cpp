#include "BusTestRig.h"
#include "Audio/SidechainRemap.h"

// Sidechain ducking with group buses: which strip may listen to which source, that no loop can form, a bus
// ducked by a channel (and by a lower bus), a return ducked by a bus, and the source renumbering when
// layers are removed or swapped (buses never move).
struct BusSidechainTests : juce::UnitTest
{
    BusSidechainTests() : juce::UnitTest("Group bus sidechain", "mu-core") {}

    using Rig = bus_test::Rig;
    static constexpr float kTrim = bus_test::kTrim;
    static constexpr int N = MixerEngine::MaxChannels;

    // Full duck needs a post-trim source level of 0.25 or more: a constant 1.0 source is plenty.
    static void duck(MixerEngine::ChannelState& s, int source, float amount)
    {
        s.sidechainSource.store(source);
        s.sidechainAmount.store(amount);
        s.sidechainAttackMs.store(0.0f);   // the follower reaches full depth within the first sample
        s.sidechainReleaseMs.store(10.0f);
    }

    void legalSources()
    {
        using mu_mix::StripKind;
        using mu_mix::isLegalSidechainSource;
        constexpr int B = mu_limits::kMaxBuses;
        auto legal = [&](StripKind k, int idx, int src) { return isLegalSidechainSource(k, idx, src, N, B); };

        beginTest("legal sources: a channel takes another channel or the external input, never a bus or itself");
        expect(legal(StripKind::Channel, 2, -1), "off");
        expect(legal(StripKind::Channel, 2, 0), "another channel");
        expect(! legal(StripKind::Channel, 2, 2), "itself");
        expect(legal(StripKind::Channel, 2, N), "external");
        for (int b = 0; b < B; ++b)
            expect(! legal(StripKind::Channel, 2, MixerEngine::busSidechainSource(b)), "bus " + juce::String(b));

        beginTest("legal sources: a bus takes a channel, a lower bus or the external input");
        expect(legal(StripKind::Bus, 1, 3), "a channel");
        expect(legal(StripKind::Bus, 1, N), "external");
        expect(legal(StripKind::Bus, 1, MixerEngine::busSidechainSource(0)), "a lower bus");
        expect(! legal(StripKind::Bus, 1, MixerEngine::busSidechainSource(1)), "itself");
        if (B > 2) expect(! legal(StripKind::Bus, 1, MixerEngine::busSidechainSource(2)), "a higher bus");
        expect(! legal(StripKind::Bus, 0, MixerEngine::busSidechainSource(0)), "bus 0 itself");

        beginTest("legal sources: a return takes a channel, any bus or the external input");
        expect(legal(StripKind::Return, 0, 5), "a channel");
        expect(legal(StripKind::Return, 0, N), "external");
        for (int b = 0; b < B; ++b)
            expect(legal(StripKind::Return, 0, MixerEngine::busSidechainSource(b)), "bus " + juce::String(b));
        expect(! legal(StripKind::Return, 0, MixerEngine::busSidechainSource(B)), "one past the last bus");
        expect(! legal(StripKind::Return, 0, N + B + 5), "far out of range");
    }

    void runTest() override
    {
        if (! bus_test::hasBuses(*this, 2)) { beginTest("no buses in this build"); expect(true); return; }
        legalSources();

        beginTest("a bus ducked by a channel: the bus signal falls by the amount, the GR meter reads it");
        {
            Rig r;
            r.dc[0] = 0.8f;   // on bus 1, the target
            r.dc[1] = 1.0f;   // the source, on the master
            r.mixer.channels[0].busAssign.store(1);
            duck(r.mixer.buses[0], 1, 0.5f);
            r.run(2);
            const float target = 0.8f * kTrim * (1.0f - 0.5f);
            expectWithinAbsoluteError(r.mixer.busPeaks[0].load(), target, 1.0e-5f, "bus is ducked by half");
            expectWithinAbsoluteError(r.outL(), target + 1.0f * kTrim, 1.0e-5f, "master = ducked bus + the source");
            expectWithinAbsoluteError(r.mixer.busSidechainGR[0].load(), 0.5f, 1.0e-5f, "bus GR meter");
        }

        beginTest("the source's own fader does not change how hard a bus ducks (pre-fader tap)");
        {
            Rig r;
            r.dc[0] = 0.8f; r.dc[1] = 1.0f;
            r.mixer.channels[0].busAssign.store(1);
            r.mixer.channels[1].level.store(0.0f);   // source fader down: the duck still follows its pre-fader level
            duck(r.mixer.buses[0], 1, 0.5f);
            r.run(2);
            expectWithinAbsoluteError(r.mixer.busSidechainGR[0].load(), 0.5f, 1.0e-5f, "bus GR meter");
        }

        beginTest("a muted source channel does not duck the bus");
        {
            Rig r;
            r.dc[0] = 0.8f; r.dc[1] = 1.0f;
            r.mixer.channels[0].busAssign.store(1);
            r.mixer.channels[1].mute.store(true);
            duck(r.mixer.buses[0], 1, 0.5f);
            r.run(2);
            expectEquals(r.mixer.busSidechainGR[0].load(), 0.0f, "no ducking");
            expectWithinAbsoluteError(r.outL(), 0.8f * kTrim, 1.0e-6f, "bus at full level");
        }

        beginTest("a bus ducked by a lower bus follows that bus's signal");
        {
            Rig r;
            r.dc[0] = 1.0f;   // bus 1: the source
            r.dc[1] = 0.8f;   // bus 2: the target
            r.mixer.channels[0].busAssign.store(1);
            r.mixer.channels[1].busAssign.store(2);
            duck(r.mixer.buses[1], MixerEngine::busSidechainSource(0), 0.5f);
            r.run(2);
            expectWithinAbsoluteError(r.mixer.busSidechainGR[1].load(), 0.5f, 1.0e-5f, "bus 2 ducks");
            expectWithinAbsoluteError(r.mixer.busPeaks[1].load(), 0.8f * kTrim * 0.5f, 1.0e-5f, "bus 2 level");
            expectEquals(r.mixer.busSidechainGR[0].load(), 0.0f, "the source bus itself does not");
        }

        beginTest("no loop: a bus cannot take itself or a higher bus as a source (the engine ignores it)");
        {
            Rig r;
            r.dc[0] = 1.0f; r.dc[1] = 1.0f;
            r.mixer.channels[0].busAssign.store(1);
            r.mixer.channels[1].busAssign.store(2);
            duck(r.mixer.buses[0], MixerEngine::busSidechainSource(0), 0.5f);   // itself
            duck(r.mixer.buses[1], MixerEngine::busSidechainSource(1), 0.5f);   // itself
            r.run(2);
            expectEquals(r.mixer.busSidechainGR[0].load(), 0.0f, "bus 1 from itself");
            expectEquals(r.mixer.busSidechainGR[1].load(), 0.0f, "bus 2 from itself");

            duck(r.mixer.buses[0], MixerEngine::busSidechainSource(1), 0.5f);   // bus 1 from bus 2 (higher): ignored
            duck(r.mixer.buses[1], MixerEngine::busSidechainSource(0), 0.5f);   // bus 2 from bus 1: legal
            r.run(2);
            expectEquals(r.mixer.busSidechainGR[0].load(), 0.0f, "bus 1 from the higher bus 2 is ignored");
            expectWithinAbsoluteError(r.mixer.busSidechainGR[1].load(), 0.5f, 1.0e-5f, "bus 2 from bus 1 works");
        }

        beginTest("a channel can not duck from a bus (the engine ignores a bus source on a channel)");
        {
            Rig r;
            r.dc[0] = 1.0f; r.dc[1] = 0.8f;
            r.mixer.channels[0].busAssign.store(1);
            duck(r.mixer.channels[1], MixerEngine::busSidechainSource(0), 0.5f);
            r.run(2);
            expectEquals(r.mixer.sidechainGR[1].load(), 0.0f, "no ducking");
            expectWithinAbsoluteError(r.outL(), (1.0f + 0.8f) * kTrim, 1.0e-6f, "both at full level");
        }

        beginTest("a bus ducked by the DAW input uses the external input and is idle without one");
        {
            Rig r;
            r.dc[0] = 0.8f;
            r.mixer.channels[0].busAssign.store(1);
            duck(r.mixer.buses[0], MixerEngine::kExtSidechainSrc, 0.5f);
            r.run(2);
            expectEquals(r.mixer.busSidechainGR[0].load(), 0.0f, "no external signal supplied");
            std::vector<float> ext((size_t) bus_test::kBlock, 1.0f);
            r.mixer.setExternalSidechain(ext.data(), nullptr);
            r.run(2);
            expectWithinAbsoluteError(r.mixer.busSidechainGR[0].load(), 0.5f, 1.0e-5f, "ducked by the external input");
        }

        beginTest("a return ducked by a bus: the bus's signal drives the return's duck");
        {
            Rig r;
            r.dc[0] = 1.0f;                       // on bus 2, the source
            r.tone[1] = 0.8f;                     // master channel feeding the reverb return
            r.mixer.channels[0].busAssign.store(2);
            r.mixer.channels[1].sendReverb.store(1.0f);
            auto& ret = r.mixer.returns[2];
            ret.sidechainSource.store(MixerEngine::busSidechainSource(1));
            ret.sidechainAmount.store(0.6f);
            ret.sidechainAttackMs.store(0.0f);
            ret.sidechainReleaseMs.store(10.0f);
            float gr = 0.0f;
            for (int b = 0; b < 40; ++b) { r.run(); gr = juce::jmax(gr, r.mixer.returnSidechainGR[2].load()); }
            expectWithinAbsoluteError(gr, 0.6f, 1.0e-5f, "the return ducks to the set depth");

            // A muted bus is not an active source.
            r.mixer.buses[1].mute.store(true);
            r.run(80);   // the follower releases (the release has a 30 ms floor) and nothing re-triggers it
            expect(r.mixer.returnSidechainGR[2].load() < 1.0e-3f, "a muted bus does not duck the return");
        }

        beginTest("an empty bus is no source: the return does not duck");
        {
            Rig r;
            r.tone[1] = 0.8f;
            r.mixer.channels[1].sendReverb.store(1.0f);
            auto& ret = r.mixer.returns[2];
            ret.sidechainSource.store(MixerEngine::busSidechainSource(0));
            ret.sidechainAmount.store(0.6f);
            ret.sidechainAttackMs.store(0.0f);
            r.run(4);
            expectEquals(r.mixer.returnSidechainGR[2].load(), 0.0f, "silent source, no duck");
        }

        beginTest("renumbering: removing a layer re-points channel sources and leaves bus sources alone");
        {
            MixerEngine m;
            const int busSrc = MixerEngine::busSidechainSource(1);
            m.channels[0].sidechainSource.store(3);          // layer 3
            m.channels[1].sidechainSource.store(1);          // layer 1 (the one removed)
            m.channels[2].sidechainSource.store(MixerEngine::kExtSidechainSrc);
            m.buses[0].sidechainSource.store(2);              // a bus ducking from layer 2
            m.buses[1].sidechainSource.store(MixerEngine::busSidechainSource(0));   // from bus 1
            m.returns[0].sidechainSource.store(busSrc);       // a return from bus 2
            m.returns[1].sidechainSource.store(4);
            m.renumberSidechainsAfterRemove(1);
            expectEquals(m.channels[0].sidechainSource.load(), 2, "layer 3 became 2");
            expectEquals(m.channels[1].sidechainSource.load(), -1, "the removed layer's follower goes off");
            expectEquals(m.channels[2].sidechainSource.load(), MixerEngine::kExtSidechainSrc, "external stays");
            expectEquals(m.buses[0].sidechainSource.load(), 1, "a bus following layer 2 follows it down");
            expectEquals(m.buses[1].sidechainSource.load(), MixerEngine::busSidechainSource(0), "a bus source never moves");
            expectEquals(m.returns[0].sidechainSource.load(), busSrc, "a return's bus source never moves");
            expectEquals(m.returns[1].sidechainSource.load(), 3, "a return's channel source follows its layer");
        }

        beginTest("renumbering: swapping two layers re-points channel and bus sources, not bus sources");
        {
            MixerEngine m;
            m.channels[0].sidechainSource.store(1);
            m.buses[0].sidechainSource.store(1);
            m.buses[1].sidechainSource.store(MixerEngine::busSidechainSource(0));
            m.returns[2].sidechainSource.store(3);
            m.returns[0].sidechainSource.store(MixerEngine::busSidechainSource(1));
            m.renumberSidechainsAfterSwap(1, 3);
            expectEquals(m.channels[0].sidechainSource.load(), 3, "channel source followed layer 1 to slot 3");
            expectEquals(m.buses[0].sidechainSource.load(), 3, "bus source followed layer 1 to slot 3");
            expectEquals(m.buses[1].sidechainSource.load(), MixerEngine::busSidechainSource(0), "bus source untouched");
            expectEquals(m.returns[2].sidechainSource.load(), 1, "return source followed layer 3 to slot 1");
            expectEquals(m.returns[0].sidechainSource.load(), MixerEngine::busSidechainSource(1), "return's bus source untouched");
        }

        beginTest("renumbering helpers: the bus range is only a range check");
        {
            for (int b = 0; b < mu_limits::kMaxBuses; ++b)
            {
                const int s = MixerEngine::busSidechainSource(b);
                expectEquals(mu_mix::sidechainAfterRemove(s, 0, N), s, "remove, bus " + juce::String(b));
                expectEquals(mu_mix::sidechainAfterSwap(s, 0, 1, N), s, "swap, bus " + juce::String(b));
                // The parameter encoding is one more: 0 off, then channels, external, buses.
                expectEquals(mu_mix::sidechainParamAfterRemove(s + 1, 0, N), s + 1, "param remove, bus " + juce::String(b));
                expectEquals(mu_mix::sidechainParamAfterSwap(s + 1, 0, 1, N), s + 1, "param swap, bus " + juce::String(b));
            }
        }
    }
};

static BusSidechainTests busSidechainTests;
