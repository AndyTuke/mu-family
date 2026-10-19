// A smooth modulator on Osc Semi moves the Pattern in scale DEGREES (audit fix e392de7, ported into
// Pattern::applyModulation): in the Major scale a smooth source at exactly +1 degree plays D over a C root, the
// note a stepped source or the Semi knob at +1 plays, not the chromatic C#.

#include "GoldenTestRig.h"
#include "Audio/SpinLock.h"            // mu-core: the modLock helpers
#include "Tests/GoldenRender.h"        // mu-core: MockHost

namespace
{
    // Positive-going zero crossings per second on channel 0: the pitch of a clean tone.
    double crossingHz(const juce::AudioBuffer<float>& b, int from, int to)
    {
        int n = 0;
        for (int i = from + 1; i < to; ++i)
            if (b.getSample(0, i - 1) <= 0.0f && b.getSample(0, i) > 0.0f) ++n;
        return (double) n * 48000.0 / (double) juce::jmax(1, to - from);
    }

    // Voice 0 alone, a plain oscillator, gater open; `smoothDegrees` != 0 adds a constant smooth modulator on
    // osc1.semi worth that many degrees, `knobSemi` sets the Semi knob. Returns the pitch after it settles.
    double pitchOf(float smoothDegrees, float knobSemi)
    {
        mu_golden_rig::OwnedProcessor p;
        mu_golden::MockHost host;
        host.bpm = 120.0;
        p.setPlayHead(&host);
        p.setRateAndBufferSizeDetails(48000.0, 512);
        p.prepareToPlay(48000.0, 512);
        mu_golden_rig::openGate(p.apvts, 0);
        mu_golden_rig::setVoice(p.apvts, 0, "o1_wt", 0);
        mu_golden_rig::setVoice(p.apvts, 0, "o2_lvl", -60.0f);
        mu_golden_rig::setVoice(p.apvts, 0, "flt_cut", 12000.0f);
        mu_golden_rig::setVoice(p.apvts, 0, "flt_res", 0.0f);
        mu_golden_rig::setVoice(p.apvts, 0, "o1_semi", knobSemi);
        if (smoothDegrees != 0.0f)
        {
            auto& pat = p.getPattern(0);
            mu_core::spinLock(pat.modLock);
            auto& cs = pat.controlSequences[0];
            cs.mode = ControlSequence::Mode::Smooth;
            cs.polarity = ControlSequence::Polarity::Unipolar;
            cs.curvePoints = { { 0.0f, 1.0f }, { 1.0f, 1.0f } };
            ModulationAssignment a;
            a.id = "m1"; a.sourceId = "cs0_output"; a.destinationId = "osc1.semi";
            a.depth = smoothDegrees * 100.0f / 24.0f;   // depth is % of the -12..12 range
            pat.modulationMatrix.addAssignment(a);
            mu_core::spinUnlock(pat.modLock);
            p.refreshPitchQuantFlags(0);
        }

        juce::AudioBuffer<float> all(2, 48 * 512);
        for (int b = 0; b < 48; ++b)
        {
            juce::AudioBuffer<float> view(all.getArrayOfWritePointers(), 2, b * 512, 512);
            view.clear();
            juce::MidiBuffer midi;
            p.processBlock(view, midi);
            host.ppq += 512.0 * host.bpm / (60.0 * 48000.0);
        }
        p.setPlayHead(nullptr);
        p.releaseResources();
        return crossingHz(all, 16 * 512, 48 * 512);
    }
}

struct MuTantPatternToneModTests : juce::UnitTest
{
    MuTantPatternToneModTests() : juce::UnitTest("mu-Tant Pattern smooth Semi modulation in scale degrees", "mu-tant-processor") {}

    void runTest() override
    {
        beginTest("Major, C root: a smooth source at +1 degree plays D (as the Semi knob at +1 does), not C#");
        mu_seam_test::ScopedEnvVar deterministic("MU_DETERMINISTIC_NOISE", "1");
        const double c     = pitchOf(0.0f, 0.0f);
        const double knobD = pitchOf(0.0f, 1.0f);
        const double modD  = pitchOf(1.0f, 0.0f);
        logMessage("C " + juce::String(c, 2) + " Hz, Semi knob +1 " + juce::String(knobD, 2) + " Hz, smooth +1 degree " + juce::String(modD, 2) + " Hz");
        expectWithinAbsoluteError(modD, knobD, 0.005 * knobD);                    // the smooth source plays the knob's note
        expectWithinAbsoluteError(knobD / c, std::pow(2.0, 2.0 / 12.0), 0.02);   // the knob: one degree is D (zero-crossing estimate)
        expectWithinAbsoluteError(modD / c, std::pow(2.0, 2.0 / 12.0), 0.02);    // the smooth source lands on D too
        expect(std::abs(modD / c - std::pow(2.0, 1.0 / 12.0)) > 0.03, "and not on the chromatic C#");
    }
};

static MuTantPatternToneModTests muTantPatternToneModTests;
