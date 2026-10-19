// C12 (extraction-plan 2.1a): a block that misses a voice's modulation-matrix lock holds the last modulated
// values instead of snapping every target back to its knob. The message thread holds the lock while it edits the
// voice's modulators; here the test holds it for one block, on the real processor.

#include "GoldenTestRig.h"
#include "Audio/SpinLock.h"            // mu-core: the modLock helpers
#include "Modulation/MuTantModSnap.h"
#include "Tests/GoldenRender.h"        // mu-core: MockHost

struct MuTantModHoldTests : juce::UnitTest
{
    MuTantModHoldTests() : juce::UnitTest("mu-Tant modulation hold on a matrix-lock miss", "mu-tant-processor") {}

    void runTest() override
    {
        beginTest("C12: a block that misses the matrix lock keeps the modulated cutoff, then the modulation follows again");

        mu_golden_rig::OwnedProcessor p;
        mu_golden::MockHost host;
        host.bpm = 120.0;
        p.setPlayHead(&host);
        p.setRateAndBufferSizeDetails(48000.0, 512);
        p.prepareToPlay(48000.0, 512);
        mu_golden_rig::openGate(p.apvts, 0);
        mu_golden_rig::setVoice(p.apvts, 0, "flt_cut", 1500.0f);

        // A smooth sequence holding the cutoff well above its knob for the whole render.
        auto& pat = p.getPattern(0);
        {
            mu_core::spinLock(pat.modLock);
            auto& cs = pat.controlSequences[0];
            cs.mode = ControlSequence::Mode::Smooth;
            cs.polarity = ControlSequence::Polarity::Unipolar;
            cs.curvePoints = { { 0.0f, 1.0f }, { 1.0f, 1.0f } };
            ModulationAssignment a;
            a.id = "m1"; a.sourceId = "cs0_output"; a.destinationId = "filter.cutoff"; a.depth = 40.0f;
            pat.modulationMatrix.addAssignment(a);
            mu_core::spinUnlock(pat.modLock);
        }

        juce::AudioBuffer<float> buf(2, 512);
        juce::MidiBuffer midi;
        auto block = [&]
        {
            buf.clear();
            p.processBlock(buf, midi);
            host.ppq += 512.0 * host.bpm / (60.0 * 48000.0);
        };
        auto cutoff = [&] { return p.getTantSnap(0, mu_tant::kTantSnapFilterCutoff); };

        for (int i = 0; i < 8; ++i) block();
        const float modulated = cutoff();
        expect(modulated > 2000.0f, "the sequence lifts the cutoff above its 1500 Hz knob: " + juce::String(modulated));

        mu_core::spinLock(pat.modLock);   // the message thread editing this voice's modulators
        block();
        mu_core::spinUnlock(pat.modLock);
        expectEquals(cutoff(), modulated, "the missed block holds the last modulated value (it would snap to 1500 Hz)");

        block();
        expectEquals(cutoff(), modulated, "and the next block resolves it again");

        p.setPlayHead(nullptr);
        p.releaseResources();
    }
};

static MuTantModHoldTests muTantModHoldTests;
