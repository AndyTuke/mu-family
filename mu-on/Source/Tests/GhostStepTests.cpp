// Ghost steps: a ghost excludes an accent, survives a save, and plays at the Ghost level's share of a normal note.

#include "Sequencer/StepRow.h"
#include "Sequencer/StepClock.h"
#include <juce_core/juce_core.h>

namespace
{
    class GhostStepTest : public juce::UnitTest
    {
    public:
        GhostStepTest() : juce::UnitTest("mu-on ghost steps", "Sequencer") {}

        void runTest() override
        {
            beginTest("a ghost and an accent exclude each other");
            {
                mu_on::StepRow row;
                row.setOn(3, true);
                row.setAccent(3, true);
                row.setGhost(3, true);
                expect(row.isGhost(3) && ! row.isAccent(3), "ghosting a step clears its accent");
                row.setAccent(3, true);
                expect(row.isAccent(3) && ! row.isGhost(3), "accenting it clears the ghost");
                row.setGhost(3, false);
                expect(row.isAccent(3), "clearing a ghost leaves an accent alone");
            }

            beginTest("ghost steps survive a save, and a state without them has none");
            {
                mu_on::StepRow row;
                row.setOn(1, true); row.setOn(5, true);
                row.setGhost(5, true);
                const auto tree = row.serialise(0);
                mu_on::StepRow back;
                back.deserialise(tree);
                expect(back.isGhost(5) && ! back.isGhost(1), "the ghost comes back on its step");

                mu_on::StepRow plain;
                plain.setOn(2, true);
                expect(! plain.serialise(0).hasProperty("ghost"), "no ghost steps writes no ghost property");
                mu_on::StepRow loaded;
                loaded.deserialise(plain.serialise(0));
                expect(! loaded.isGhost(2), "and loads none");
            }

            beginTest("a ghost step fires at the Ghost level's share of a normal note");
            {
                mu_on::StepRow row;
                row.setOn(0, true); row.setOn(4, true);
                row.setGhost(4, true);
                row.setGhostLevel(0.5f);
                mu_on::StepClock clock;
                float normal = -1.0f, ghost = -1.0f;
                const double bps = 1.0 / 48000.0 * 2.0;   // two beats a second at 48 kHz: beats per sample
                // Step 4 starts one beat in; render the first two beats in one call.
                clock.processSpan(row, 0.0, 48000, bps, 0.0f, 1.0f, [&](float velocity, int offset)
                {
                    if (offset < 100) normal = velocity; else ghost = velocity;
                });
                expectWithinAbsoluteError(normal, mu_on::StepClock::kNormalVelocity, 1.0e-6f, "a normal step is the normal velocity");
                expectWithinAbsoluteError(ghost, mu_on::StepClock::kNormalVelocity * 0.5f, 1.0e-6f, "a ghost is half of it at 50 %");
            }
        }
    };

    static GhostStepTest ghostStepTest;
}
