// The Smooth modulator curve passes through every anchor and never swings past its neighbours, however unevenly the
// anchors are spaced (the Catmull-Rom tangents it replaced looped between close anchors beside a long segment).

#include <juce_core/juce_core.h>

#include "Sequencer/ControlSequence.h"

#include <algorithm>
#include <cmath>

struct SmoothCurveTests : juce::UnitTest
{
    SmoothCurveTests() : juce::UnitTest("The Smooth curve passes through its anchors and never overshoots them", "mu-core") {}

    static ControlSequence::CurvePoint pt(float x, float y) { return { x, y, false, 0.0f, 0.0f }; }

    // The curve's output at loop position `phase` (0..1), through the public evaluate.
    static float at(ControlSequence& cs, double phase)
    {
        cs.mode = ControlSequence::Mode::Smooth;
        return cs.evaluate(std::min(phase, 0.999999) * cs.getLoopLengthBeats());   // phase 1 wraps to the start
    }

    void runTest() override
    {
        // Anchors as in a hand-drawn curve: clusters and long gaps, peaks and troughs.
        ControlSequence cs;
        cs.polarity = ControlSequence::Polarity::Bipolar;
        cs.curvePoints = { pt(0.0f, -0.9f), pt(0.08f, -0.1f), pt(0.30f, 0.7f), pt(0.33f, 0.45f), pt(0.40f, -0.6f),
                           pt(0.50f, 0.5f), pt(0.62f, 0.3f), pt(0.70f, 1.0f), pt(0.78f, -0.7f), pt(0.85f, 0.2f), pt(1.0f, 0.1f) };

        beginTest("the curve passes through every anchor");
        for (const auto& p : cs.curvePoints)
            expectWithinAbsoluteError(at(cs, (double) p.x) / 100.0f, p.y, 1.0e-4f, "anchor at x = " + juce::String(p.x));

        beginTest("between two anchors the curve stays between their heights");
        for (size_t i = 0; i + 1 < cs.curvePoints.size(); ++i)
        {
            const auto& a = cs.curvePoints[i];
            const auto& b = cs.curvePoints[i + 1];
            const float lo = std::min(a.y, b.y) - 1.0e-4f, hi = std::max(a.y, b.y) + 1.0e-4f;
            float worst = 0.0f;
            for (int k = 1; k < 100; ++k)
            {
                const float v = at(cs, (double) (a.x + (b.x - a.x) * (float) k / 100.0f)) / 100.0f;
                if (v < lo || v > hi) worst = std::max(worst, std::max(lo - v, v - hi));
            }
            expectLessThan(worst, 1.0e-4f, "segment " + juce::String((int) i) + " leaves its anchors' range");
        }

        beginTest("two anchors are a straight line");
        {
            ControlSequence line;
            line.polarity = ControlSequence::Polarity::Bipolar;
            line.curvePoints = { pt(0.0f, -0.5f), pt(1.0f, 0.5f) };
            expectWithinAbsoluteError(at(line, 0.25) / 100.0f, -0.25f, 1.0e-4f);
            expectWithinAbsoluteError(at(line, 0.75) / 100.0f,  0.25f, 1.0e-4f);
        }
    }
};

static SmoothCurveTests smoothCurveTests;
