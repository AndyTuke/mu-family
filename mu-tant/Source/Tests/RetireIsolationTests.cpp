// C3 (design 2.1.4): a retiring Pattern reads only its snapshot, its engine and its insert, never a parameter or a
// modulation matrix. The twin test: two identical processors drop the same voice by a count-reducing restore while
// playing; then on ONE of them every parameter of the processor is scrambled to a legal random value and every
// voice's modulators and envelopes are replaced. The dropped voice's fade, read on its own channel before the
// strip (the approved channel tap, N6), must be bit-identical in both for its whole life.

#include "GoldenTestRig.h"
#include "Tests/RetireIsolation.h"     // mu-core: the generic twin test
#include "Audio/SpinLock.h"            // mu-core: the modLock helpers

namespace
{
    // Three voices on a wavetable, voice 2 an octave up through a ringing insert, so the tail shows its snapshot.
    void setupVoices(ProcessorBase& pb)
    {
        auto& p = static_cast<mu_tant::PluginProcessor&>(pb);
        mu_golden_rig::growTo(p, 3);
        for (int v = 0; v < 3; ++v) { mu_golden_rig::openGate(p.apvts, v); mu_golden_rig::setVoice(p.apvts, v, "o1_wt", 1); }
        mu_golden_rig::setVoice(p.apvts, 2, "o1_oct", 1);
        mu_golden_rig::setVoice(p.apvts, 2, "drvChar", 11);
        mu_golden_rig::setVoice(p.apvts, 2, "insP1", 0.6f);
        mu_golden_rig::setVoice(p.apvts, 2, "insP2", 0.8f);
    }

    // The retire: a count-reducing restore to two voices while playing, so voice 2 fades in place.
    void restoreWithTwo(ProcessorBase& pb)
    {
        auto& p = static_cast<mu_tant::PluginProcessor&>(pb);
        auto tree = p.captureFullPreset();
        tree.setProperty("numVoices", 2, nullptr);
        juce::MemoryBlock mb;
        if (auto xml = tree.createXml()) juce::AudioProcessor::copyXmlToBinary(*xml, mb);
        p.setStateInformation(mb.getData(), (int) mb.getSize());
    }

    // Beyond the parameters (the helper scrambles every one): every voice's modulators and envelopes replaced, so
    // whatever a tail might wrongly read is now different on this twin.
    void scramble(ProcessorBase& pb)
    {
        auto& p = static_cast<mu_tant::PluginProcessor&>(pb);
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
        mu_contract_test::RetireIsolationCase c;
        c.make = [] { return std::unique_ptr<ProcessorBase>(new mu_golden_rig::OwnedProcessor()); };
        c.setup = setupVoices;
        c.retire = restoreWithTwo;     // the park and the count store; the scramble follows, before the first tail block
        c.scramble = scramble;
        c.slot = 2;
        c.blocksBefore = 12;
        c.blocksAfter = 8;             // 2048 frames: past the 1200-frame fade
        const auto r = mu_contract_test::checkRetireIsolation(c);
        expect(r.tailFrames > 1000, "the dropped voice kept rendering into its channel (its fade)");
        expect(r.sounded, "the tail is audible");
        expect(r.ok, "the scrambled twin's tail is bit-identical (the tail reads only its snapshot): " + r.message);
    }
};

static MuTantRetireIsolationTests muTantRetireIsolationTests;
