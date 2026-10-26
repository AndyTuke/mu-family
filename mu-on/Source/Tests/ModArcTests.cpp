// Live modulation arcs: every target in every engine's table must publish its post-matrix value, because
// the knobs' arcs read it (a target that publishes nothing shows no arc). Runs on the REAL
// PluginProcessor, built headless. (createEditor is defined in ProcessorSeamTests.cpp.)

#include "Plugin/PluginProcessor.h"
#include "Modulation/ModTargetChecks.h"
#include "Tests/ProcessorSeamHelpers.h"

struct MuOnModArcTests : juce::UnitTest
{
    MuOnModArcTests() : juce::UnitTest("mu-On live modulation arcs", "mu-on-processor") {}

    void runTest() override
    {
        using namespace mu_seam_test;
        using namespace mu_on;

        TempStorage storage;
        PluginProcessor proc;
        proc.prepareToPlay(48000.0, 256);

        juce::AudioBuffer<float> buf(2, 256);
        juce::MidiBuffer midi;
        const auto block = [&] { buf.clear(); midi.clear(); proc.processBlock(buf, midi); };

        for (int e = 0; e < kNumEngineTypes; ++e)
        {
            const auto engine = engineFromIndex(e);
            int count = 0;
            const ModDestEntry* table = destsForEngine(engine, count);

            beginTest(juce::String(engineName(engine)) + ": every modulation target names an existing track parameter");
            {
                juce::StringArray missing;
                for (int i = 0; i < count; ++i)
                    if (proc.apvts.getParameter(mu_mod::paramIdFor(layerParamPrefix(0), 0, table[i].param)) == nullptr)   // a pan or send is the strip's
                        missing.add(table[i].id);
                expect(missing.isEmpty(), "missing: " + missing.joinIntoString(", "));
            }

            beginTest(juce::String(engineName(engine)) + ": every target's live value follows its modulation assignment");
            {
                proc.selectTrackEngine(0, engine);

                // A constant full-scale unipolar sequence feeds the matrix.
                auto& track = proc.getTrack(0);
                auto& cs = track.controlSequences[0];
                cs.mode       = ControlSequence::Mode::Stepped;
                cs.polarity   = ControlSequence::Polarity::Unipolar;
                cs.stepValues = { 100.0f };

                for (int i = 0; i < count; ++i)
                {
                    const auto id = layerParamPrefix(0) + table[i].param;
                    const auto range = proc.apvts.getParameterRange(id);

                    // Park the knob mid-range so a positive depth has room to move it.
                    setParam(proc.apvts, id, range.convertFrom0to1(0.5f));
                    block();
                    const float before = range.convertTo0to1(proc.getModLive(0, engine, i));
                    const float base   = range.convertTo0to1(getParam(proc.apvts, id));
                    expect(std::abs(before - base) < 0.02f,
                           juce::String(table[i].id) + ": unassigned live value sits on the knob (" + juce::String(before) + ")");

                    ModulationAssignment a;
                    a.id = "arc"; a.sourceId = "cs0_output"; a.destinationId = table[i].id; a.depth = 40.0f;
                    expect(track.modulationMatrix.addAssignment(a), juce::String(table[i].id) + ": assignment accepted");

                    block();
                    const float after = range.convertTo0to1(proc.getModLive(0, engine, i));
                    expect(after > before + 0.2f,
                           juce::String(table[i].id) + ": live value moves with the assignment (" + juce::String(before) + " -> " + juce::String(after) + ")");

                    track.modulationMatrix.removeAssignment("arc");
                }

                // The values belong to the running engine: another engine's table reads NaN.
                const auto other = engineFromIndex((e + 1) % kNumEngineTypes);
                expect(std::isnan(proc.getModLive(0, other, 0)), "a different engine's live value is NaN");
            }
        }

        beginTest("a block that misses the track's matrix lock holds the last resolved values (C12)");
        {
            proc.selectTrackEngine(0, EngineType::Bass);
            auto& track = proc.getTrack(0);
            auto& cs = track.controlSequences[0];
            cs.mode       = ControlSequence::Mode::Stepped;
            cs.polarity   = ControlSequence::Polarity::Unipolar;
            cs.stepValues = { 100.0f };
            int count = 0;
            const ModDestEntry* table = destsForEngine(EngineType::Bass, count);
            const int cut = 2;   // b.cut.prop
            const auto id = layerParamPrefix(0) + table[cut].param;
            const auto range = proc.apvts.getParameterRange(id);
            setParam(proc.apvts, id, range.convertFrom0to1(0.5f));

            ModulationAssignment a;
            a.id = "hold"; a.sourceId = "cs0_output"; a.destinationId = table[cut].id; a.depth = 40.0f;
            expect(track.modulationMatrix.addAssignment(a));
            block();
            const float modulated = proc.getModLive(0, EngineType::Bass, cut);
            expect(range.convertTo0to1(modulated) > 0.7f, "the assignment moves the cutoff up");

            track.modLock.store(true, std::memory_order_release);   // stands in for the message thread editing the modulators
            block();
            expectEquals(proc.getModLive(0, EngineType::Bass, cut), modulated, "held: the cutoff does not drop to its knob for the missed block");
            track.modLock.store(false, std::memory_order_release);

            track.modulationMatrix.removeAssignment("hold");
            block();
            expect(std::abs(range.convertTo0to1(proc.getModLive(0, EngineType::Bass, cut)) - 0.5f) < 0.02f, "and it follows the matrix again");
        }

        proc.releaseResources();
    }
};

static MuOnModArcTests muOnModArcTests;
