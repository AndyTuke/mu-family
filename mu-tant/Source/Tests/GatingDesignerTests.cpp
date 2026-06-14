// mu-tant gating designer tests - covers the subdivision math (cells per
// 2 bars), the subdivision-denominator dropdown round-trip, default/fallback
// behaviour, and the underlying GatePattern tool operations (pencil / eraser /
// glue / reverse) that the UI tools drive.

#include <juce_core/juce_core.h>
#include "UI/GatingDesigner.h"
#include "Sequencer/GatePattern.h"

class GatingDesignerTest : public juce::UnitTest
{
public:
    GatingDesignerTest() : juce::UnitTest("mu-tant gating designer", "mu-tant") {}

    void runTest() override
    {
        using namespace mu_tant;

        beginTest("subdivision setter round-trips");
        {
            GatingDesigner gd;
            // Default is 1/16 per the header constant.
            expect(gd.getSubdivision() == 16, "default subdivision is 1/16");

            gd.setSubdivision(4);   expect(gd.getSubdivision() == 4,  "set to 1/4");
            gd.setSubdivision(8);   expect(gd.getSubdivision() == 8,  "set to 1/8");
            gd.setSubdivision(16);  expect(gd.getSubdivision() == 16, "set to 1/16");
            gd.setSubdivision(32);  expect(gd.getSubdivision() == 32, "set to 1/32");
        }

        beginTest("view window is exactly 2 bars");
        {
            expect(GatingDesigner::kViewBars == 2,
                   "design spec: gating view window = 2 bars");
        }

        // kViewBars x subdivision = cells in view; cell-count math check.
        beginTest("subdivision implies expected cell count per view window");
        {
            struct Case { int denom; int expectedCells; };
            const Case cases[] = {
                { 4,  8  },   // 1/4  over 2 bars = 8 cells
                { 8,  16 },   // 1/8  over 2 bars = 16 cells
                { 16, 32 },   // 1/16 over 2 bars = 32 cells
                { 32, 64 },   // 1/32 over 2 bars = 64 cells
            };
            for (const auto& c : cases)
            {
                const int cells = GatingDesigner::kViewBars * c.denom;
                expect(cells == c.expectedCells,
                       "denom 1/" + juce::String(c.denom)
                           + " -> " + juce::String(c.expectedCells) + " cells");
            }
        }

        // ── Pencil tool: addEnvelope ──────────────────────────────────────────
        beginTest("pencil: addEnvelope inserts and sorts by start cell");
        {
            GatePattern p;
            GateEnvelope a; a.startCell = 4; a.lengthCells = 2;
            GateEnvelope b; b.startCell = 0; b.lengthCells = 2;
            p.addEnvelope(a);
            p.addEnvelope(b);
            expectEquals((int) p.envelopes.size(), 2, "two envelopes added");
            expectEquals(p.envelopes[0].startCell, 0, "sorted: cell 0 first");
            expectEquals(p.envelopes[1].startCell, 4, "sorted: cell 4 second");
            expect(p.hasEnvelopes.load(), "hasEnvelopes set after add");
        }

        beginTest("pencil: addEnvelope removes overlapping envelope");
        {
            GatePattern p;
            GateEnvelope e1; e1.startCell = 0; e1.lengthCells = 4;   // cells 0..3
            p.addEnvelope(e1);
            expectEquals((int) p.envelopes.size(), 1);

            GateEnvelope e2; e2.startCell = 2; e2.lengthCells = 2;   // cells 2..3 overlap
            p.addEnvelope(e2);
            expectEquals((int) p.envelopes.size(), 1, "old overlapping envelope removed");
            expectEquals(p.envelopes[0].startCell, 2, "new envelope kept");
        }

        beginTest("pencil: non-overlapping envelopes coexist");
        {
            GatePattern p;
            GateEnvelope e1; e1.startCell = 0; e1.lengthCells = 4;
            GateEnvelope e2; e2.startCell = 6; e2.lengthCells = 2;
            p.addEnvelope(e1);
            p.addEnvelope(e2);
            expectEquals((int) p.envelopes.size(), 2, "two non-overlapping envelopes");
        }

        // ── Eraser tool: removeEnvelopeCovering ──────────────────────────────
        beginTest("eraser: removeEnvelopeCovering removes the hit envelope");
        {
            GatePattern p;
            GateEnvelope e; e.startCell = 4; e.lengthCells = 3;   // covers cells 4,5,6
            p.addEnvelope(e);
            expect(!p.envelopes.empty());

            p.removeEnvelopeCovering(5);   // eraser click in the middle
            expect(p.envelopes.empty(), "envelope removed");
            expect(!p.hasEnvelopes.load(), "hasEnvelopes cleared");
        }

        beginTest("eraser: removeEnvelopeCovering is no-op on empty cell");
        {
            GatePattern p;
            GateEnvelope e; e.startCell = 0; e.lengthCells = 2;
            p.addEnvelope(e);

            p.removeEnvelopeCovering(8);   // no envelope covers cell 8
            expectEquals((int) p.envelopes.size(), 1, "unaffected");
        }

        // ── Glue tool: mergeRange ─────────────────────────────────────────────
        beginTest("glue: mergeRange merges two adjacent envelopes into one");
        {
            GatePattern p;
            GateEnvelope e1; e1.startCell = 0; e1.lengthCells = 2;
            GateEnvelope e2; e2.startCell = 2; e2.lengthCells = 2;
            p.addEnvelope(e1);
            p.addEnvelope(e2);
            expectEquals((int) p.envelopes.size(), 2);

            p.mergeRange(0, 3);
            expectEquals((int) p.envelopes.size(), 1, "merged to one envelope");
            expectEquals(p.envelopes[0].startCell,   0, "merged start");
            expectEquals(p.envelopes[0].lengthCells, 4, "merged length");
        }

        beginTest("glue: mergeRange over an empty range creates one cell");
        {
            GatePattern p;   // no envelopes
            p.mergeRange(5, 7);
            expectEquals((int) p.envelopes.size(), 1, "empty range creates one envelope");
            expectEquals(p.envelopes[0].startCell,   5, "start from left edge");
            expectEquals(p.envelopes[0].lengthCells, 3, "length = 7-5+1 = 3 cells");
        }

        beginTest("glue: mergeRange averages split values of merged envelopes");
        {
            GatePattern p;
            GateEnvelope e1; e1.startCell = 0; e1.lengthCells = 2; e1.split = 0.0f;
            GateEnvelope e2; e2.startCell = 2; e2.lengthCells = 2; e2.split = 1.0f;
            p.addEnvelope(e1);
            p.addEnvelope(e2);

            p.mergeRange(0, 3);
            expectWithinAbsoluteError(p.envelopes[0].split, 0.5f, 0.001f, "split averaged");
        }

        // ── Reverse tool ──────────────────────────────────────────────────────
        beginTest("reverse: toggling the reverse flag on an envelope");
        {
            GatePattern p;
            GateEnvelope e; e.startCell = 0; e.lengthCells = 4; e.reverse = false;
            p.addEnvelope(e);

            // Simulate the reverse tool: flip the reverse flag on the envelope.
            for (auto& env : p.envelopes)
                if (env.covers(2))
                    env.reverse = !env.reverse;

            expect(p.envelopes[0].reverse == true, "reverse flag toggled on");

            for (auto& env : p.envelopes)
                if (env.covers(2))
                    env.reverse = !env.reverse;

            expect(p.envelopes[0].reverse == false, "reverse flag toggled back off");
        }

        // ── envelopeAtCell ────────────────────────────────────────────────────
        beginTest("envelopeAtCell finds the correct envelope");
        {
            GatePattern p;
            GateEnvelope e1; e1.startCell = 0; e1.lengthCells = 4;
            GateEnvelope e2; e2.startCell = 8; e2.lengthCells = 3;
            p.addEnvelope(e1);
            p.addEnvelope(e2);

            expect(p.envelopeAtCell(2) == &p.envelopes[0], "cell 2 -> first envelope");
            expect(p.envelopeAtCell(9) == &p.envelopes[1], "cell 9 -> second envelope");
            expect(p.envelopeAtCell(5) == nullptr,          "cell 5 -> no envelope");
        }
    }
};

static GatingDesignerTest gatingDesignerTestInstance;
