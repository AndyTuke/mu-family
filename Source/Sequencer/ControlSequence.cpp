#include "ControlSequence.h"

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

float ControlSequence::evaluate(double songBeatPos) const
{
    const double loop = getLoopLengthBeats();
    if (loop <= 0.0)
        return 0.0f;

    double phase = std::fmod(songBeatPos, loop) / loop;
    if (phase < 0.0)
        phase += 1.0;

    float raw = (mode == Mode::Stepped) ? evaluateStepped(phase) : evaluateSmooth(phase);

    if (polarity == Polarity::Unipolar)
        raw = std::max(0.0f, raw);

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

    // Linear interpolation; bezier evaluation is added in Stage 5 with the LFO editor.
    const float t = (x - p0.x) / segLen;
    return (p0.y + t * (p1.y - p0.y)) * 100.0f;
}
