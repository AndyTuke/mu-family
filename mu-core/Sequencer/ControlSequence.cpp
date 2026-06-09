#include "Sequencer/ControlSequence.h"

#include <algorithm>
#include <cmath>

double ControlSequence::noteValueToBeats(NoteValue nv, NoteMod mod)
{
    double base = 0.0;
    switch (nv)
    {
        case NoteValue::Whole:        base = 4.000; break;
        case NoteValue::Half:         base = 2.000; break;
        case NoteValue::Quarter:      base = 1.000; break;
        case NoteValue::Eighth:       base = 0.500; break;
        case NoteValue::Sixteenth:    base = 0.250; break;
        case NoteValue::ThirtySecond: base = 0.125; break;
    }
    switch (mod)
    {
        case NoteMod::Triplet: base *= 2.0 / 3.0; break;
        case NoteMod::Dotted:  base *= 1.5;        break;
        case NoteMod::None:                         break;
    }
    return base;
}

double ControlSequence::getLoopLengthBeats() const
{
    return noteValueToBeats(loopNoteValue, loopNoteMod) * loopMultiplier;
}

int ControlSequence::getStepCount() const
{
    const double loop = getLoopLengthBeats();
    const double step = noteValueToBeats(stepNoteValue, stepNoteMod) * stepMultiplier;
    if (step <= 0.0)
        return 1;
    return std::max(1, static_cast<int>(std::round(loop / step)));
}

double ControlSequence::getStepFraction() const
{
    const double loop = getLoopLengthBeats();
    const double step = noteValueToBeats(stepNoteValue, stepNoteMod) * stepMultiplier;
    if (loop <= 0.0 || step <= 0.0)
        return 1.0;
    return std::min(1.0, step / loop);
}

float ControlSequence::evaluate(double songBeatPos) const
{
    const double loop = getLoopLengthBeats();
    if (loop <= 0.0)
        return 0.0f;

    double phase = std::fmod(songBeatPos, loop) / loop;
    if (phase < 0.0)
        phase += 1.0;

    // Evaluate the active mode's shape, but fall back to the other representation
    // when the active one carries no data (e.g. a preset authored as mode="Smooth"
    // but populated with <Step>s, or a runtime mode flip that outran its data).
    // Without this a mode/data mismatch silently outputs a constant 0 — modulation
    // that looks wired but does nothing. The loader (deserialiseModulators) also
    // corrects the mode at load time; this is the belt to that braces.
    const bool haveStep  = ! stepValues.empty();
    const bool haveCurve = ! curvePoints.empty();
    float raw;
    if (mode == Mode::Stepped)
        raw = haveStep  ? evaluateStepped(phase) : (haveCurve ? evaluateSmooth(phase)  : 0.0f);
    else
        raw = haveCurve ? evaluateSmooth(phase)  : (haveStep  ? evaluateStepped(phase) : 0.0f);

    // Clamp to the matrix's contract range so a bent curve / Catmull-Rom overshoot can't
    // push the modulation past the destination's intended swing. Unipolar is floored at 0.
    raw = (polarity == Polarity::Unipolar) ? std::clamp(raw, 0.0f, 100.0f)
                                           : std::clamp(raw, -100.0f, 100.0f);

    return raw;
}

float ControlSequence::evaluateStepped(double phase) const
{
    const int count = getStepCount();
    if (count == 0 || stepValues.empty())
        return 0.0f;

    const int idx = std::min(static_cast<int>(phase * count), count - 1);
    return (idx < static_cast<int>(stepValues.size())) ? stepValues[idx] : 0.0f;
}

float ControlSequence::evaluateSmooth(double phase) const
{
    if (curvePoints.empty())
        return 0.0f;

    if (curvePoints.size() == 1)
        return curvePoints[0].y * 100.0f;

    const float x = static_cast<float>(phase);

    if (x <= curvePoints.front().x)
        return curvePoints.front().y * 100.0f;

    if (x >= curvePoints.back().x)
        return curvePoints.back().y * 100.0f;

    // Binary search for the segment [lo, lo+1] that contains x.
    int lo = 0;
    int hi = static_cast<int>(curvePoints.size()) - 2;
    while (lo < hi)
    {
        const int mid = (lo + hi + 1) / 2;
        if (curvePoints[mid].x <= x)
            lo = mid;
        else
            hi = mid - 1;
    }

    const CurvePoint& p0 = curvePoints[lo];
    const CurvePoint& p1 = curvePoints[lo + 1];
    const float segLen = p1.x - p0.x;

    if (segLen <= 0.0f)
        return p0.y * 100.0f;

    const float t = (x - p0.x) / segLen;

    // Smooth (Catmull-Rom) cubic so the curve flows through the anchors without kinks.
    // Control-point y-values come from Catmull-Rom tangents: interior points use centred
    // differences, the boundary points use one-sided differences (so a 2-point curve stays
    // exactly linear). An explicit per-segment bend (handleY, set by dragging a segment's
    // grab handle) adds on top. handleX is ignored — t is chord progress, keeping the
    // playhead-vs-time mapping simple. Must match LFOEditor::buildCurvePath.
    const int n = static_cast<int>(curvePoints.size());
    auto py = [&](int i) { return curvePoints[static_cast<size_t>(std::min(std::max(i, 0), n - 1))].y; };
    const float m0 = (lo == 0)         ? (py(lo + 1) - py(lo)) : 0.5f * (py(lo + 1) - py(lo - 1));
    const float m1 = (lo + 1 == n - 1) ? (py(lo + 1) - py(lo)) : 0.5f * (py(lo + 2) - py(lo));
    // Clamp the bezier control points to the visible range. A bezier lies within the convex
    // hull of its control points, so with the anchors + handles bounded the curve can't
    // overshoot the range — and this matches LFOEditor::buildCurvePath exactly.
    const float yFloor = (polarity == Polarity::Unipolar) ? 0.0f : -1.0f;
    const float c1y = std::clamp(p0.y + m0 / 3.0f + (p0.hasBezierHandle ? p0.handleY : 0.0f), yFloor, 1.0f);
    const float c2y = std::clamp(p1.y - m1 / 3.0f + (p1.hasBezierHandle ? p1.handleY : 0.0f), yFloor, 1.0f);

    const float one_t = 1.0f - t;
    const float b = one_t * one_t * one_t * p0.y
                  + 3.0f * one_t * one_t * t * c1y
                  + 3.0f * one_t * t * t     * c2y
                  + t * t * t                * p1.y;
    return b * 100.0f;
}
