// The Accent setting's two MIDI velocities: the whole Euclid-to-MIDI loudness rule.

#include <juce_core/juce_core.h>
#include "Sequencer/AccentVelocity.h"

using namespace mu_clid_midi;

class AccentVelocityTest : public juce::UnitTest
{
public:
    AccentVelocityTest() : juce::UnitTest("mu-Clid MIDI accent velocity", "MidiProduct") {}

    void runTest() override
    {
        beginTest("Accent 0: accented hits at 100, plain hits at 100");
        {
            const auto v = accentVelocities(0.0f);
            expectWithinAbsoluteError(v.accented, 100.0f / 127.0f, 1.0e-6f);
            expectWithinAbsoluteError(v.normal,   100.0f / 127.0f, 1.0e-6f);
        }

        beginTest("Accent 50: accented hits at 127, plain hits at 100");
        {
            const auto v = accentVelocities(50.0f);
            expectWithinAbsoluteError(v.accented, 1.0f, 1.0e-6f);
            expectWithinAbsoluteError(v.normal,   100.0f / 127.0f, 1.0e-6f);
        }

        beginTest("Accent 100: accented hits at 127, plain hits at 75");
        {
            const auto v = accentVelocities(100.0f);
            expectWithinAbsoluteError(v.accented, 1.0f, 1.0e-6f);
            expectWithinAbsoluteError(v.normal,   75.0f / 127.0f, 1.0e-6f);
        }

        beginTest("The accented velocity never drops below the plain one");
        for (float a = 0.0f; a <= 100.0f; a += 5.0f)
        {
            const auto v = accentVelocities(a);
            expect(v.accented >= v.normal, "accent " + juce::String(a));
            expect(v.accented <= 1.0f && v.normal > 0.0f, "range at accent " + juce::String(a));
        }
    }
};

static AccentVelocityTest accentVelocityTest;
