// Live modulation arcs: every target in mu-Toni's table must publish its post-matrix value, because
// the knobs' arcs read it (a target that publishes nothing shows no arc). Runs on the REAL
// PluginProcessor, built headless. (createEditor is defined in ProcessorSeamTests.cpp.)

#include "Plugin/PluginProcessor.h"
#include "Modulation/ModTargetChecks.h"
#include "Tests/ProcessorSeamHelpers.h"

struct MuToniModArcTests : juce::UnitTest
{
    MuToniModArcTests() : juce::UnitTest("mu-Toni live modulation arcs", "mu-toni-processor") {}

    void runTest() override
    {
        using namespace mu_seam_test;
        using namespace mu_toni;

        TempStorage storage;
        PluginProcessor proc;
        proc.prepareToPlay(48000.0, 256);

        beginTest("every modulation target names an existing layer parameter");
        {
            const auto missing = mu_mod::checks::missingParams(kModDestTable, proc.apvts, "a0_");
            expect(missing.isEmpty(), "missing: " + missing.joinIntoString(", "));
        }

        beginTest("every target's live value follows its modulation assignment");
        {
            // A constant full-scale unipolar sequence feeds the matrix.
            auto& layer = proc.getArp(0);
            auto& cs = layer.controlSequences[0];
            cs.mode       = ControlSequence::Mode::Stepped;
            cs.polarity   = ControlSequence::Polarity::Unipolar;
            cs.stepValues = { 100.0f };

            juce::AudioBuffer<float> buf(2, 256);
            juce::MidiBuffer midi;
            const auto block = [&] { buf.clear(); midi.clear(); proc.processBlock(buf, midi); };

            for (int i = 0; i < kNumModDests; ++i)
            {
                const auto id = juce::String("a0_") + kModDestTable[i].param;
                const auto range = proc.apvts.getParameterRange(id);

                // Park the knob mid-range so a positive depth has room to move it.
                setParam(proc.apvts, id, range.convertFrom0to1(0.5f));
                block();
                const float before = range.convertTo0to1(proc.getModLive(0, i));
                const float base   = range.convertTo0to1(getParam(proc.apvts, id));   // stepped knobs snap off 0.5
                expect(std::abs(before - base) < 0.02f,
                       juce::String(kModDestTable[i].id) + ": unassigned live value sits on the knob (" + juce::String(before) + ")");

                ModulationAssignment a;
                a.id = "arc"; a.sourceId = "cs0_output"; a.destinationId = kModDestTable[i].id; a.depth = 40.0f;
                expect(layer.modulationMatrix.addAssignment(a), juce::String(kModDestTable[i].id) + ": assignment accepted");

                block();
                const float after = range.convertTo0to1(proc.getModLive(0, i));
                expect(after > before + 0.2f,
                       juce::String(kModDestTable[i].id) + ": live value moves with the assignment (" + juce::String(before) + " -> " + juce::String(after) + ")");

                layer.modulationMatrix.removeAssignment("arc");
            }
        }

        beginTest("a block that misses the layer's matrix lock holds the last resolved values (C12)");
        {
            auto& layer = proc.getArp(0);
            auto& cs = layer.controlSequences[0];
            cs.mode       = ControlSequence::Mode::Stepped;
            cs.polarity   = ControlSequence::Polarity::Unipolar;
            cs.stepValues = { 100.0f };
            const auto id = juce::String("a0_") + kModDestTable[D_cut].param;
            const auto range = proc.apvts.getParameterRange(id);
            setParam(proc.apvts, id, range.convertFrom0to1(0.5f));

            juce::AudioBuffer<float> buf(2, 256);
            juce::MidiBuffer midi;
            const auto block = [&] { buf.clear(); midi.clear(); proc.processBlock(buf, midi); };

            ModulationAssignment a;
            a.id = "hold"; a.sourceId = "cs0_output"; a.destinationId = kModDestTable[D_cut].id; a.depth = 40.0f;
            expect(layer.modulationMatrix.addAssignment(a));
            block();
            const float modulated = proc.getModLive(0, D_cut);
            expect(range.convertTo0to1(modulated) > 0.7f, "the assignment moves the cutoff up");

            layer.modLock.store(true, std::memory_order_release);   // stands in for the message thread editing the modulators
            block();
            expectEquals(proc.getModLive(0, D_cut), modulated, "held: the cutoff does not drop to its knob for the missed block");
            layer.modLock.store(false, std::memory_order_release);

            layer.modulationMatrix.removeAssignment("hold");
            block();
            expect(std::abs(range.convertTo0to1(proc.getModLive(0, D_cut)) - 0.5f) < 0.02f, "and follows again once the lock is free");
        }

        proc.releaseResources();
    }
};

static MuToniModArcTests muToniModArcTests;
