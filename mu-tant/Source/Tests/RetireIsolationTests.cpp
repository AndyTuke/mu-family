// C3 (design 2.1.4): a retiring Pattern reads only its snapshot, its engine and its insert, never a parameter or a
// modulation matrix. The twin test: two identical processors drop the same voice by a count-reducing restore while
// playing; then on ONE of them every parameter of the processor is scrambled to a legal random value and every
// voice's modulators and envelopes are replaced. The dropped voice's fade, read on its own channel before the
// strip (the approved channel tap, N6), must be bit-identical in both for its whole life.

#include "GoldenTestRig.h"
#include "Tests/GoldenRender.h"        // mu-core: MockHost
#include "Tests/LayerEquivalence.h"    // mu-core: ChannelStreamTap
#include "Audio/SpinLock.h"            // mu-core: the modLock helpers

#include <cstring>

#include <random>

namespace
{
    struct Twin
    {
        Twin()
        {
            host.bpm = 120.0;
            p.setPlayHead(&host);
            p.setRateAndBufferSizeDetails(48000.0, 256);
            p.prepareToPlay(48000.0, 256);
            mu_golden_rig::growTo(p, 3);
            for (int v = 0; v < 3; ++v) { mu_golden_rig::openGate(p.apvts, v); mu_golden_rig::setVoice(p.apvts, v, "o1_wt", 1); }
            mu_golden_rig::setVoice(p.apvts, 2, "o1_oct", 1);
            mu_golden_rig::setVoice(p.apvts, 2, "drvChar", 11);   // a ringing insert, so the tail shows its snapshot
            mu_golden_rig::setVoice(p.apvts, 2, "insP1", 0.6f);
            mu_golden_rig::setVoice(p.apvts, 2, "insP2", 0.8f);
            p.setChannelTapForTest(&tap);
        }
        ~Twin() { p.setChannelTapForTest(nullptr); p.setPlayHead(nullptr); p.releaseResources(); }

        void block()
        {
            juce::AudioBuffer<float> buf(2, 256);
            juce::MidiBuffer midi;
            buf.clear();
            p.processBlock(buf, midi);
            host.ppq += 256.0 * host.bpm / (60.0 * 48000.0);
        }

        void restoreWith(int voices)
        {
            auto tree = p.captureFullPreset();
            tree.setProperty("numVoices", voices, nullptr);
            juce::MemoryBlock mb;
            if (auto xml = tree.createXml()) juce::AudioProcessor::copyXmlToBinary(*xml, mb);
            p.setStateInformation(mb.getData(), (int) mb.getSize());
        }

        mu_golden_rig::OwnedProcessor p;
        mu_golden::MockHost host;
        mu_layer_equiv::ChannelStreamTap tap { 256 * 64 };
    };

    // Every parameter of the processor to a legal random value, and every voice's modulators and envelopes
    // replaced: whatever a tail might wrongly read is now different on this twin.
    void scramble(mu_tant::PluginProcessor& p)
    {
        std::mt19937 rng(1234);
        std::uniform_real_distribution<float> u(0.0f, 1.0f);
        for (auto* param : p.getParameters())
            param->setValueNotifyingHost(u(rng));
        for (int v = 0; v < mu_limits::kMaxLayers; ++v)
        {
            auto& pat = p.getPattern(v);
            mu_core::spinLock(pat.modLock);
            auto& cs = pat.controlSequences[0];
            cs.mode = ControlSequence::Mode::Smooth;
            cs.curvePoints = { { 0.0f, 1.0f }, { 1.0f, 0.0f } };
            ModulationAssignment a;
            a.id = "scr"; a.sourceId = "cs0_output"; a.destinationId = "filter.cutoff"; a.depth = 80.0f;
            pat.modulationMatrix.addAssignment(a);
            mu_core::spinUnlock(pat.modLock);
            pat.gate.addEnvelope({ 0, 2, 0.5f, 0.0f, 0.0f, false });
            pat.filterGate.addEnvelope({ 0, 4, 0.5f, 0.0f, 0.0f, false });
        }
    }
}

struct MuTantRetireIsolationTests : juce::UnitTest
{
    MuTantRetireIsolationTests() : juce::UnitTest("mu-Tant retire tail isolation (C3 twin)", "mu-tant-processor") {}

    void runTest() override
    {
        beginTest("A dropped voice's fade is the same whatever every parameter, modulator and envelope does meanwhile");
        mu_seam_test::ScopedEnvVar deterministic("MU_DETERMINISTIC_NOISE", "1");
        auto a = std::make_unique<Twin>(), b = std::make_unique<Twin>();
        for (int i = 0; i < 12; ++i) { a->block(); b->block(); }
        const int before = a->tap.written[2];
        expectEquals(b->tap.written[2], before, "both twins rendered voice 2 alike so far");

        a->restoreWith(2);
        b->restoreWith(2);
        scramble(b->p);   // after the park and the count store, before the first tail block
        for (int i = 0; i < 8; ++i) { a->block(); b->block(); }   // 2048 frames: past the 1200-frame fade

        expect(a->tap.written[2] > before + 1000, "the dropped voice kept rendering into its channel (its fade)");
        expectEquals(b->tap.written[2], a->tap.written[2], "both twins rendered the same tail length");
        bool same = true, sounded = false;
        for (int c = 0; c < 2; ++c)
            for (int i = before; i < a->tap.written[2]; ++i)
            {
                const float x = a->tap.bufs[2].getSample(c, i), y = b->tap.bufs[2].getSample(c, i);
                same = same && std::memcmp(&x, &y, sizeof (float)) == 0;
                sounded = sounded || x != 0.0f;
            }
        expect(sounded, "the tail is audible");
        expect(same, "the scrambled twin's tail is bit-identical (the tail reads only its snapshot)");
    }
};

static MuTantRetireIsolationTests muTantRetireIsolationTests;
