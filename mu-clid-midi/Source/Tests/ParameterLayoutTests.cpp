// The product's parameter layout: the saved ids stay put, and every parameter the shared
// Euclid panel binds exists (a missing one would silently do nothing).

#include <juce_audio_processors/juce_audio_processors.h>
#include "Tests/StubProcessor.h"

using namespace mu_clid_midi;

class ParameterLayoutTest : public juce::UnitTest
{
public:
    ParameterLayoutTest() : juce::UnitTest("mu-Clid MIDI parameter layout", "MidiProduct") {}

    void runTest() override
    {
        tests::StubProcessor proc;

        beginTest("The saved ids exist with their old defaults");
        expectEquals(proc.actual("lite_midiNote"), 36.0f);
        expectEquals(proc.actual("lite_accentAmt"), 0.0f);
        expectEquals(proc.actual("mstrLoop"), 0.0f);

        beginTest("Every Euclid suffix the panel binds exists for rhythm 0");
        for (const char* suffix : { "stepsA", "hitsA", "rotA", "prePadA", "postPadA", "insStA", "insLenA",
                                    "insModeA", "prePadModeA", "postPadModeA",
                                    "stepsB", "hitsB", "rotB", "prePadB", "postPadB", "insStB", "insLenB",
                                    "insModeB", "prePadModeB", "postPadModeB",
                                    "stepsC", "hitsC", "rotC", "prePadC", "postPadC", "insStC", "insLenC",
                                    "insModeC", "prePadModeC", "postPadModeC",
                                    "logic", "patLeg", "vMono", "rstSt" })
            expect(proc.apvts.getParameter(juce::String("r0_") + suffix) != nullptr, juce::String("missing r0_") + suffix);

        beginTest("Only the product's parameters are declared (no voice, mixer or FX ids)");
        expectEquals((int) proc.getParameters().size(), 34 + 3);

        beginTest("Ranges match mu-Clid's shared Euclid set");
        expectEquals(proc.apvts.getParameter("r0_stepsA")->convertFrom0to1(1.0f), (float) mu_core::HitGenerator::kMaxSteps);
        expectEquals(proc.apvts.getParameter("r0_rotA")->convertFrom0to1(0.0f), -32.0f);
        expectEquals(proc.apvts.getParameter("r0_rstSt")->convertFrom0to1(0.0f), -1.0f);
        expectEquals(proc.actual("r0_stepsA"), 8.0f);
        expectEquals(proc.actual("r0_rstSt"), -1.0f);
    }
};

static ParameterLayoutTest parameterLayoutTest;
