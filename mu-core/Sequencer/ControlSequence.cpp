#include "Sequencer/ControlSequence.h"
#include "Modulation/SmoothCurve.h"   // the shared Smooth curve tangents

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
    // Tile the loop with fixed-length steps + a partial final step for any remainder
    // (ceil), NOT equal division: a 3/16 step in a 16/16 loop gives 3,3,3,3,3,1 = 6 steps.
    // For a step that divides the loop evenly, ceil == round, so integer configs are
    // unchanged. Matches getStepFraction() and the editors' grid.
    return std::max(1, static_cast<int>(std::ceil(loop / step - 1.0e-9)));
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

    // Clamp to the matrix's contract range so a bent curve can't
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

    // Tile by the actual step width (phase / stepFraction), so the final partial step
    // occupies only its real remainder rather than an equal 1/count slice. For a step that
    // divides the loop evenly this equals the old phase*count. Matches the editor grid.
    const double frac = getStepFraction();
    const int idx = (frac > 0.0)
        ? std::min(count - 1, static_cast<int>(phase / frac))
        : std::min(static_cast<int>(phase * count), count - 1);
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

    // Smooth cubic through the anchors (mu_mod::smoothControlY: monotone tangents, so it never swings past a
    // neighbour). t is chord progress, which keeps the playhead-vs-time mapping simple; handleX is ignored. Must
    // match LFOEditor::buildCurvePath, which draws from the same control heights.
    const float yFloor = (polarity == Polarity::Unipolar) ? 0.0f : -1.0f;
    float c1y, c2y;
    mu_mod::smoothControlY(curvePoints, lo, yFloor, c1y, c2y);

    const float one_t = 1.0f - t;
    const float b = one_t * one_t * one_t * p0.y
                  + 3.0f * one_t * one_t * t * c1y
                  + 3.0f * one_t * t * t     * c2y
                  + t * t * t                * p1.y;
    return b * 100.0f;
}
