// mu-tant gate-pattern tests — the data model + envelope evaluator that the
// drawable gate editor + audio path will both consume.

#include <juce_core/juce_core.h>
#include "Sequencer/GatePattern.h"

class GatePatternTest : public juce::UnitTest
{
public:
    GatePatternTest() : juce::UnitTest("mu-tant gate pattern", "mu-tant") {}

    void runTest() override
    {
        using namespace mu_tant;

        beginTest("subdivision → total cell count (2-bar pattern)");
        {
            GatePattern p;
            p.subdivision = GatePattern::Subdivision::Quarter;
            expect(p.totalCells() == 8,  "1/4 over 2 bars = 8 cells");
            p.subdivision = GatePattern::Subdivision::Eighth;
            expect(p.totalCells() == 16, "1/8 over 2 bars = 16 cells");
            p.subdivision = GatePattern::Subdivision::Sixteenth;
            expect(p.totalCells() == 32, "1/16 over 2 bars = 32 cells");
            p.subdivision = GatePattern::Subdivision::ThirtySecond;
            expect(p.totalCells() == 64, "1/32 over 2 bars = 64 cells");
        }

        beginTest("envelope linear decay: 1 → 0 across phase");
        {
            GateEnvelope env;
            env.curveBend = 0.0f;
            expectWithinAbsoluteError(env.value(0.0f), 1.0f, 1e-5f, "phase 0 → 1.0");
            expectWithinAbsoluteError(env.value(0.5f), 0.5f, 1e-5f, "phase 0.5 → 0.5 (linear)");
            expectWithinAbsoluteError(env.value(1.0f), 0.0f, 1e-5f, "phase 1 → 0.0");
        }

        beginTest("envelope reverse swaps phase polarity (attack)");
        {
            GateEnvelope env;
            env.curveBend = 0.0f;
            env.options.reverse = true;
            expectWithinAbsoluteError(env.value(0.0f), 0.0f, 1e-5f, "reverse phase 0 → 0");
            expectWithinAbsoluteError(env.value(0.5f), 0.5f, 1e-5f, "reverse linear midpoint");
            expectWithinAbsoluteError(env.value(1.0f), 1.0f, 1e-5f, "reverse phase 1 → 1");
        }

        beginTest("envelope curve bend reshapes decay (concave / convex)");
        {
            GateEnvelope concave; concave.curveBend = -1.0f;
            GateEnvelope convex;  convex.curveBend  = +1.0f;
            // At phase 0.5, linear = 0.5. Concave should be < 0.5 (already
            // dropped faster); convex should be > 0.5 (still sustaining).
            const float concaveAtHalf = concave.value(0.5f);
            const float convexAtHalf  = convex.value(0.5f);
            expect(concaveAtHalf < 0.5f, "concave drops below linear at midpoint");
            expect(convexAtHalf  > 0.5f, "convex sustains above linear at midpoint");
            // Both still hit endpoints exactly.
            expectWithinAbsoluteError(concave.value(1.0f), 0.0f, 1e-5f, "concave hits 0 at end");
            expectWithinAbsoluteError(convex.value(1.0f),  0.0f, 1e-5f, "convex hits 0 at end");
            expectWithinAbsoluteError(concave.value(0.0f), 1.0f, 1e-5f, "concave starts at 1");
            expectWithinAbsoluteError(convex.value(0.0f),  1.0f, 1e-5f, "convex starts at 1");
        }

        beginTest("addOrReplaceEnvelope: insert + replace + sort");
        {
            GatePattern p;
            p.subdivision = GatePattern::Subdivision::Sixteenth;

            GateEnvelope a; a.cell = 4;  a.curveBend = +0.3f;
            GateEnvelope b; b.cell = 0;  b.curveBend = -0.2f;
            GateEnvelope c; c.cell = 8;  c.curveBend = 0.0f;

            p.addOrReplaceEnvelope(a);
            p.addOrReplaceEnvelope(b);
            p.addOrReplaceEnvelope(c);
            expect((int) p.envelopes.size() == 3, "three envelopes inserted");
            // Stored in ascending cell order.
            expect(p.envelopes[0].cell == 0, "sorted: cell 0 first");
            expect(p.envelopes[1].cell == 4, "sorted: cell 4 second");
            expect(p.envelopes[2].cell == 8, "sorted: cell 8 third");

            // Replace cell 4 with a new bend; count unchanged.
            GateEnvelope replaceA; replaceA.cell = 4; replaceA.curveBend = -0.5f;
            p.addOrReplaceEnvelope(replaceA);
            expect((int) p.envelopes.size() == 3, "replace doesn't grow count");
            for (const auto& e : p.envelopes)
                if (e.cell == 4)
                    expectWithinAbsoluteError(e.curveBend, -0.5f, 1e-6f, "cell 4 bend updated");
        }

        beginTest("removeEnvelopeAt drops the envelope at the given cell");
        {
            GatePattern p;
            GateEnvelope a; a.cell = 0;
            GateEnvelope b; b.cell = 4;
            GateEnvelope c; c.cell = 8;
            p.addOrReplaceEnvelope(a);
            p.addOrReplaceEnvelope(b);
            p.addOrReplaceEnvelope(c);

            p.removeEnvelopeAt(4);
            expect((int) p.envelopes.size() == 2, "size drops to 2");
            expect(p.cellEnvelope(4) == nullptr, "no envelope at cell 4 after remove");
            expect(p.cellEnvelope(0) != nullptr, "cell 0 envelope retained");
            expect(p.cellEnvelope(8) != nullptr, "cell 8 envelope retained");

            // Remove non-existent cell — no-op.
            p.removeEnvelopeAt(99);
            expect((int) p.envelopes.size() == 2, "remove-nonexistent is no-op");
        }

        beginTest("envelopeSpan: cell range up to next envelope or pattern end");
        {
            GatePattern p;
            p.subdivision = GatePattern::Subdivision::Sixteenth;   // 32 cells total

            GateEnvelope a; a.cell = 0;
            GateEnvelope b; b.cell = 4;
            GateEnvelope c; c.cell = 20;
            p.addOrReplaceEnvelope(a);
            p.addOrReplaceEnvelope(b);
            p.addOrReplaceEnvelope(c);

            // Sorted: 0 → 4 → 20 → (end 32).
            expect(p.envelopeSpan(0) == 4,  "env[0] spans cells 0..3 (= 4)");
            expect(p.envelopeSpan(1) == 16, "env[1] spans cells 4..19 (= 16)");
            expect(p.envelopeSpan(2) == 12, "env[2] spans cells 20..31 (= 12)");
            expect(p.envelopeSpan(99) == 0, "invalid index returns 0");
        }
    }
};

static GatePatternTest gatePatternTestInstance;
