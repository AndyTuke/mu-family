// mu-tant modulator-evaluator tests - ControlSequence is shared mu-core DSP that
// drives every mu-tant modulation source, so pin its evaluate() invariants.

#include <juce_core/juce_core.h>
#include "Sequencer/ControlSequence.h"

class ControlSequenceTest : public juce::UnitTest
{
public:
    ControlSequenceTest() : juce::UnitTest("mu-tant control sequence", "mu-tant") {}

    void runTest() override
    {
        beginTest("getLoopLengthBeats: whole note = 4 beats, x2 = 8");
        {
            ControlSequence cs;
            cs.loopNoteValue = NoteValue::Whole; cs.loopMultiplier = 1;
            expectWithinAbsoluteError(cs.getLoopLengthBeats(), 4.0, 0.001);
            cs.loopMultiplier = 2;
            expectWithinAbsoluteError(cs.getLoopLengthBeats(), 8.0, 0.001);
        }

        beginTest("getStepCount: whole-note loop / quarter step = 4");
        {
            ControlSequence cs;
            cs.loopNoteValue = NoteValue::Whole; cs.loopMultiplier = 1;
            cs.stepNoteValue = NoteValue::Quarter; cs.stepMultiplier = 1;
            expectEquals(cs.getStepCount(), 4);
            cs.stepNoteValue = NoteValue::Eighth;
            expectEquals(cs.getStepCount(), 8);
            // Non-dividing step tiles with a partial final step (ceil): 16/16 loop, 3/16 step
            // -> 3,3,3,3,3,1 = 6 steps (was round()=5).
            cs.stepNoteValue = NoteValue::Sixteenth; cs.stepMultiplier = 3;
            expectEquals(cs.getStepCount(), 6);
        }

        beginTest("evaluateStepped tiles by step width (partial final step), not equal division");
        {
            // 16/16 loop, 3/16 step -> 6 steps tiling 3,3,3,3,3,1. Distinct value per step so we
            // can read back which step each phase lands in.
            ControlSequence cs;
            cs.mode = ControlSequence::Mode::Stepped;
            cs.polarity = ControlSequence::Polarity::Bipolar;
            cs.loopNoteValue = NoteValue::Whole;     cs.loopMultiplier = 1;
            cs.stepNoteValue = NoteValue::Sixteenth; cs.stepMultiplier = 3;
            cs.loopNoteMod = NoteMod::None; cs.stepNoteMod = NoteMod::None;
            cs.stepValues = { 10.0f, 20.0f, 30.0f, 40.0f, 50.0f, 60.0f };   // 6 steps
            const double loopBeats = cs.getLoopLengthBeats();              // 4 beats
            // Step boundaries at 3/16,6/16,9/16,12/16,15/16 of the loop. Sample mid-cell.
            auto at = [&](double sixteenths) { return cs.evaluate(loopBeats * sixteenths / 16.0); };
            expectWithinAbsoluteError(at(1.5),  10.0f, 0.01f);   // step 0  (0..3/16)
            expectWithinAbsoluteError(at(4.5),  20.0f, 0.01f);   // step 1  (3..6/16)
            expectWithinAbsoluteError(at(13.5), 50.0f, 0.01f);   // step 4  (12..15/16)
            expectWithinAbsoluteError(at(15.5), 60.0f, 0.01f);   // step 5  partial (15..16/16)
        }

        beginTest("getStepFraction: tiles by step width, not equal division");
        {
            ControlSequence cs;
            // 1-bar loop (whole note = 4 beats = 16 sixteenths), step = 1/16 x 3 = 3/16.
            cs.loopNoteValue = NoteValue::Whole;     cs.loopMultiplier = 1;
            cs.stepNoteValue = NoteValue::Sixteenth; cs.stepMultiplier = 3;
            // 3/16 of the loop -> the grid tiles 3/16 x5 + a 1/16 remainder, NOT 5 equal cells.
            expectWithinAbsoluteError(cs.getStepFraction(), 0.1875, 1.0e-6);
            // An evenly-dividing step gives the exact reciprocal.
            cs.stepMultiplier = 1;
            expectWithinAbsoluteError(cs.getStepFraction(), 0.0625, 1.0e-6);   // 1/16
            // A step ≥ the loop collapses to a single cell (no internal grid).
            cs.stepNoteValue = NoteValue::Whole; cs.stepMultiplier = 2;
            expectWithinAbsoluteError(cs.getStepFraction(), 1.0, 1.0e-6);
        }

        beginTest("stepped: constant step values yield a constant output anywhere in the loop");
        {
            ControlSequence cs;
            cs.mode = ControlSequence::Mode::Stepped;
            cs.polarity = ControlSequence::Polarity::Bipolar;
            cs.loopNoteValue = NoteValue::Whole; cs.stepNoteValue = NoteValue::Quarter;
            cs.stepValues = { 42.0f, 42.0f, 42.0f, 42.0f };
            for (double b : { 0.0, 0.5, 1.3, 2.7, 3.9, 4.1, 7.5 })
                expectWithinAbsoluteError(cs.evaluate(b), 42.0f, 0.5f);
        }

        beginTest("bipolar output stays within [-100, +100]");
        {
            ControlSequence cs;
            cs.mode = ControlSequence::Mode::Stepped;
            cs.polarity = ControlSequence::Polarity::Bipolar;
            cs.loopNoteValue = NoteValue::Whole; cs.stepNoteValue = NoteValue::Quarter;
            cs.stepValues = { -100.0f, 100.0f, -50.0f, 50.0f };
            for (int i = 0; i < 16; ++i)
            {
                const float v = cs.evaluate(i * 0.25);
                expect(v >= -100.001f && v <= 100.001f, "bipolar in range at " + juce::String(i));
            }
        }

        beginTest("unipolar output never goes negative");
        {
            ControlSequence cs;
            cs.mode = ControlSequence::Mode::Stepped;
            cs.polarity = ControlSequence::Polarity::Unipolar;
            cs.loopNoteValue = NoteValue::Whole; cs.stepNoteValue = NoteValue::Quarter;
            cs.stepValues = { -100.0f, 100.0f, -50.0f, 50.0f };
            for (int i = 0; i < 16; ++i)
            {
                const float v = cs.evaluate(i * 0.25);
                expect(v >= -0.001f && v <= 100.001f, "unipolar in [0,100] at " + juce::String(i));
            }
        }

        beginTest("stepped output loops (position wraps at the loop length)");
        {
            ControlSequence cs;
            cs.mode = ControlSequence::Mode::Stepped;
            cs.loopNoteValue = NoteValue::Whole; cs.stepNoteValue = NoteValue::Quarter;
            cs.stepValues = { 10.0f, 20.0f, 30.0f, 40.0f };
            // Same phase one + two loops later (loop = 4 beats) reads the same step.
            expectWithinAbsoluteError(cs.evaluate(0.5), cs.evaluate(4.5), 0.5f);
            expectWithinAbsoluteError(cs.evaluate(2.5), cs.evaluate(10.5), 0.5f);
        }

        beginTest("smooth: empty curve evaluates to 0");
        {
            ControlSequence cs;
            cs.mode = ControlSequence::Mode::Smooth;
            cs.curvePoints.clear();
            cs.loopNoteValue = NoteValue::Whole;
            expectWithinAbsoluteError(cs.evaluate(0.0), 0.0f, 0.001f);
            expectWithinAbsoluteError(cs.evaluate(2.0), 0.0f, 0.001f);
        }

        beginTest("smooth: a rising ramp curve increases through the loop");
        {
            ControlSequence cs;
            cs.mode = ControlSequence::Mode::Smooth;
            cs.polarity = ControlSequence::Polarity::Bipolar;
            cs.loopNoteValue = NoteValue::Whole;
            cs.curvePoints = { { 0.0f, -1.0f, false, 0.0f, 0.0f },
                               { 1.0f,  1.0f, false, 0.0f, 0.0f } };
            const float start = cs.evaluate(0.01);
            const float end   = cs.evaluate(3.99);
            expect(end > start, "ramp rises across the loop");
        }
    }
};

static ControlSequenceTest controlSequenceTest;
