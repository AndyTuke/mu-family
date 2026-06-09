#pragma once

#include <string>
#include <vector>

enum class NoteValue  { Whole, Half, Quarter, Eighth, Sixteenth, ThirtySecond };
enum class NoteMod    { None, Triplet, Dotted };

class ControlSequence
{
public:
    enum class Mode     { Smooth, Stepped };
    enum class Polarity { Unipolar, Bipolar };

    struct CurvePoint
    {
        float x = 0.0f;          // normalised loop position 0..1
        float y = 0.0f;          // normalised output -1..1
        bool  hasBezierHandle = false;
        // handleX is currently unused by evaluateSmooth (the evaluator is 1-D
        // in y-over-chord). Kept in the struct for forward-compat with a future 2-D
        // cubic evaluator. Anyone authoring presets externally should not expect
        // horizontal-handle effects.
        float handleX = 0.0f;
        float handleY = 0.0f;    // offset from segment midpoint
    };

    std::string id;              // stable ID used as modulation source key, e.g. "cs0"

    Mode     mode     = Mode::Stepped;
    Polarity polarity = Polarity::Unipolar;   // new modulators start unipolar; presets restore their saved polarity

    NoteValue loopNoteValue  = NoteValue::Whole;
    NoteMod   loopNoteMod    = NoteMod::None;
    int       loopMultiplier = 1;           // 1 whole note = 1 bar default

    NoteValue stepNoteValue  = NoteValue::Sixteenth;  // stepped mode only
    NoteMod   stepNoteMod    = NoteMod::None;
    int       stepMultiplier = 1;

    std::vector<float>      stepValues;   // stepped mode: value per step, -100..+100
    std::vector<CurvePoint> curvePoints;  // smooth mode: sorted by x ascending

    // Returns the loop length in beats (assumes 4/4 quarter-note = 1 beat).
    double getLoopLengthBeats() const;

    // Returns the step count for stepped mode = ceil(loopLength / stepLength): the loop is
    // tiled by fixed-length steps plus a partial final step for any remainder (e.g. a 3/16
    // step in a 16/16 loop → 6 steps: 3,3,3,3,3,1). For a step that divides the loop evenly
    // this equals round. Pairs with getStepFraction() for the editor grid + evaluateStepped.
    int getStepCount() const;

    // Returns one step's length as a fraction of the loop (= stepLength / loopLength,
    // clamped to (0,1]). Drives the smooth editor's step grid + X-snap: the loop is tiled
    // by fixed-width steps with a partial final cell (e.g. step 3/16 in a 16/16 loop →
    // 0.1875 → five full cells + a 1/16 remainder), NOT divided into equal cells. Returns
    // 1.0 (single cell / no internal grid) when the step is ≥ the loop or either is zero.
    double getStepFraction() const;

    // Evaluates the output at the given absolute song position in beats.
    // Returns a value in [-100, +100] (bipolar) or [0, +100] (unipolar).
    float evaluate(double songBeatPos) const;

private:
    static double noteValueToBeats(NoteValue nv, NoteMod mod);

    float evaluateStepped(double phase) const;
    float evaluateSmooth(double phase) const;
};
