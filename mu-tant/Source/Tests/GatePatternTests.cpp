// mu-tant gate-pattern tests - the attack/decay envelope model + evaluator the
// drawable gate editor + audio path both consume.

#include <juce_core/juce_core.h>
#include "Sequencer/GatePattern.h"

class GatePatternTest : public juce::UnitTest
{
public:
    GatePatternTest() : juce::UnitTest("mu-tant gate pattern", "mu-tant") {}

    void runTest() override
    {
        using namespace mu_tant;

        beginTest("subdivision -> total cell count (2-bar pattern)");
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

        beginTest("default envelope (split 0) is a pure linear decay 1 -> 0");
        {
            GateEnvelope env;   // split 0, bends 0
            expectWithinAbsoluteError(env.value(0.0f), 1.0f, 1e-5f, "phase 0 -> 1.0 (instant attack)");
            expectWithinAbsoluteError(env.value(0.5f), 0.5f, 1e-5f, "phase 0.5 -> 0.5 (linear)");
            expectWithinAbsoluteError(env.value(1.0f), 0.0f, 1e-5f, "phase 1 -> 0.0");
        }

        beginTest("split = 0.5 -> symmetric attack/decay triangle");
        {
            GateEnvelope env; env.split = 0.5f;
            expectWithinAbsoluteError(env.value(0.0f),  0.0f, 1e-5f, "starts at 0 (attack)");
            expectWithinAbsoluteError(env.value(0.25f), 0.5f, 1e-5f, "quarter -> mid attack");
            expectWithinAbsoluteError(env.value(0.5f),  1.0f, 1e-5f, "peak at the split");
            expectWithinAbsoluteError(env.value(0.75f), 0.5f, 1e-5f, "three-quarter -> mid decay");
            expectWithinAbsoluteError(env.value(1.0f),  0.0f, 1e-5f, "ends at 0 (decay)");
        }

        beginTest("reverse flips attack and decay (mirror in time)");
        {
            GateEnvelope env;                 // split 0 -> pure decay
            env.reverse = true;               // -> pure attack 0 -> 1
            expectWithinAbsoluteError(env.value(0.0f), 0.0f, 1e-5f, "reversed starts at 0");
            expectWithinAbsoluteError(env.value(0.5f), 0.5f, 1e-5f, "reversed linear midpoint");
            expectWithinAbsoluteError(env.value(1.0f), 1.0f, 1e-5f, "reversed ends at 1");
        }

        beginTest("attack / decay bends reshape the matching segment");
        {
            // split 0.5: attack midpoint at phase 0.25, decay midpoint at 0.75,
            // both 0.5 when linear.
            GateEnvelope up;   up.split = 0.5f;   up.attackBend = +1.0f; up.decayBend = +1.0f;
            GateEnvelope down; down.split = 0.5f;  down.attackBend = -1.0f; down.decayBend = -1.0f;

            expect(up.value(0.25f)   > 0.5f, "attackBend +1 bulges the attack up");
            expect(down.value(0.25f) < 0.5f, "attackBend -1 bends the attack down");
            expect(up.value(0.75f)   > 0.5f, "decayBend +1 sustains the decay");
            expect(down.value(0.75f) < 0.5f, "decayBend -1 drops the decay faster");
            // Endpoints + peak stay put regardless of bend.
            expectWithinAbsoluteError(up.value(0.5f), 1.0f, 1e-5f, "peak still 1.0");
            expectWithinAbsoluteError(up.value(1.0f), 0.0f, 1e-5f, "decay still ends at 0");
        }

        beginTest("Gap forces the trailing fraction of the region to silence");
        {
            GateEnvelope env;   // split 0 -> pure decay
            const float gap = 0.5f;
            expectWithinAbsoluteError(env.value(0.0f,  gap), 1.0f, 1e-5f, "phase 0 still full");
            expectWithinAbsoluteError(env.value(0.25f, gap), 0.5f, 1e-5f, "shape squeezed into first half");
            expectWithinAbsoluteError(env.value(0.5f,  gap), 0.0f, 1e-5f, "second half silent");
            expectWithinAbsoluteError(env.value(0.9f,  gap), 0.0f, 1e-5f, "well into the gap -> silent");
            // Gap = 100% -> whole region silent.
            expectWithinAbsoluteError(env.value(0.1f, 1.0f), 0.0f, 1e-5f, "gap 100% -> silent everywhere");
        }

        beginTest("addEnvelope: overlapping insert trims the underlying region");
        {
            GatePattern p;
            p.subdivision = GatePattern::Subdivision::Sixteenth;

            GateEnvelope wide; wide.startCell = 0; wide.lengthCells = 4;
            p.addEnvelope(wide);
            expect((int) p.envelopes.size() == 1, "one wide envelope");

            // Pencil a 1-cell envelope inside it -> the overlapping wide one is removed.
            GateEnvelope dot; dot.startCell = 2; dot.lengthCells = 1;
            p.addEnvelope(dot);
            expect((int) p.envelopes.size() == 1, "overlap replaced, not added");
            expect(p.envelopes[0].startCell == 2, "the 1-cell envelope remains");
            expect(p.envelopes[0].lengthCells == 1, "length is 1");

            // A non-overlapping envelope coexists, kept sorted by startCell.
            GateEnvelope other; other.startCell = 0; other.lengthCells = 1;
            p.addEnvelope(other);
            expect((int) p.envelopes.size() == 2, "non-overlapping coexists");
            expect(p.envelopes[0].startCell == 0, "sorted: cell 0 first");
            expect(p.envelopes[1].startCell == 2, "sorted: cell 2 second");
        }

        beginTest("envelopeAtCell + removeEnvelopeCovering span the whole region");
        {
            GatePattern p;
            p.subdivision = GatePattern::Subdivision::Sixteenth;
            GateEnvelope e; e.startCell = 4; e.lengthCells = 4;   // covers cells 4..7
            p.addEnvelope(e);

            expect(p.envelopeAtCell(4) != nullptr, "cell 4 covered");
            expect(p.envelopeAtCell(7) != nullptr, "cell 7 covered (region end)");
            expect(p.envelopeAtCell(8) == nullptr, "cell 8 outside the region");
            expect(p.envelopeAtCell(3) == nullptr, "cell 3 before the region");

            p.removeEnvelopeCovering(6);   // any cell inside removes the whole region
            expect((int) p.envelopes.size() == 0, "removed via an interior cell");

            p.addEnvelope(e);
            p.removeEnvelopeCovering(99);  // outside -> no-op
            expect((int) p.envelopes.size() == 1, "remove-outside is a no-op");
        }

        beginTest("mergeRange fills the dragged span + averages the shapes");
        {
            GatePattern p;
            p.subdivision = GatePattern::Subdivision::Sixteenth;

            GateEnvelope a; a.startCell = 0; a.lengthCells = 1; a.split = 0.2f; a.attackBend = 0.4f; a.decayBend = 0.6f;
            GateEnvelope b; b.startCell = 2; b.lengthCells = 1; b.split = 0.6f; b.attackBend = 0.2f; b.decayBend = 0.4f;
            p.addEnvelope(a);
            p.addEnvelope(b);

            p.mergeRange(0, 3);   // drag across cells 0..3
            expect((int) p.envelopes.size() == 1, "merged into one envelope");
            const auto& m = p.envelopes[0];
            expect(m.startCell == 0,    "merged region starts at the drag start");
            expect(m.lengthCells == 4,  "merged region fills the whole dragged span");
            expectWithinAbsoluteError(m.split,      0.4f, 1e-5f, "split is the average of the merged");
            expectWithinAbsoluteError(m.attackBend, 0.3f, 1e-5f, "attackBend averaged");
            expectWithinAbsoluteError(m.decayBend,  0.5f, 1e-5f, "decayBend averaged");
        }

        beginTest("gateAt: empty pattern is fully open (continuous drone)");
        {
            GatePattern p;
            p.subdivision = GatePattern::Subdivision::Sixteenth;
            expectWithinAbsoluteError(p.gateAt(0.0, 0.0f), 1.0f, 1e-5f, "empty -> 1.0 at beat 0");
            expectWithinAbsoluteError(p.gateAt(3.7, 0.0f), 1.0f, 1e-5f, "empty -> 1.0 mid-pattern");
        }

        beginTest("gateAt: covered cells play the curve, uncovered cells are silent");
        {
            GatePattern p;
            p.subdivision = GatePattern::Subdivision::Sixteenth;   // 32 cells over 8 beats
            GateEnvelope a; a.startCell = 0; a.lengthCells = 1;    // default decay
            p.addEnvelope(a);

            const double cellLen = 8.0 / 32.0;   // 0.25 beats per cell
            expectWithinAbsoluteError(p.gateAt(0.0, 0.0f), 1.0f, 1e-4f, "cell 0 phase 0 -> 1.0");
            expectWithinAbsoluteError(p.gateAt(cellLen * 0.5, 0.0f), 0.5f, 1e-3f, "cell 0 mid -> 0.5");
            expectWithinAbsoluteError(p.gateAt(cellLen * 1.5, 0.0f), 0.0f, 1e-5f, "cell 1 (uncovered) -> silent");
        }

        beginTest("gateAt: phase spans the whole region, not a single cell");
        {
            GatePattern p;
            p.subdivision = GatePattern::Subdivision::Sixteenth;
            GateEnvelope wide; wide.startCell = 0; wide.lengthCells = 4;   // default decay
            p.addEnvelope(wide);

            const double cellLen  = 8.0 / 32.0;
            const double regionLen = cellLen * 4.0;
            // Decay reaches 0.5 at the region midpoint (cell 2 boundary), not cell 0.5.
            expectWithinAbsoluteError(p.gateAt(regionLen * 0.5, 0.0f), 0.5f, 1e-3f,
                                      "region midpoint -> 0.5 (region-wide phase)");
        }

        beginTest("gateAt: Gap silences the region tail");
        {
            GatePattern p;
            p.subdivision = GatePattern::Subdivision::Sixteenth;
            GateEnvelope a; a.startCell = 0; a.lengthCells = 4;
            p.addEnvelope(a);

            const double cellLen  = 8.0 / 32.0;
            const double regionLen = cellLen * 4.0;
            // Gap 0.5 -> second half of the region is silent.
            expectWithinAbsoluteError(p.gateAt(regionLen * 0.75, 0.5f), 0.0f, 1e-5f,
                                      "75% through region with 50% gap -> silent");
            expect(p.gateAt(regionLen * 0.25, 0.5f) > 0.0f, "first quarter still sounds");
        }

        beginTest("gateAt: wraps mod 2 bars (8 beats)");
        {
            GatePattern p;
            p.subdivision = GatePattern::Subdivision::Sixteenth;
            GateEnvelope a; a.startCell = 0; a.lengthCells = 1;
            p.addEnvelope(a);
            expectWithinAbsoluteError(p.gateAt(8.0, 0.0f),  p.gateAt(0.0, 0.0f), 1e-4f, "beat 8 wraps to beat 0");
            expectWithinAbsoluteError(p.gateAt(16.0, 0.0f), p.gateAt(0.0, 0.0f), 1e-4f, "beat 16 wraps to beat 0");
        }
    }
};

static GatePatternTest gatePatternTestInstance;
