#pragma once

#include <string>
#include <vector>

enum class NoteValue  { Whole, Half, Quarter, Eighth, Sixteenth, ThirtySecond };
enum class NoteMod    { None, Triplet, Dotted };

class ControlSequence
{
public:
    enum class Mode        { Smooth, Stepped };
    enum class Polarity    { Unipolar, Bipolar };
    enum class InputSource { Internal };  // MIDI_CC removed in #136

    struct CurvePoint
    {
        float x = 0.0f;          // normalised loop position 0..1
        float y = 0.0f;          // normalised output -1..1
        bool  hasBezierHandle = false;
        // #225: handleX is currently unused by evaluateSmooth (the evaluator is 1-D
        // in y-over-chord). Kept in the struct for forward-compat with a future 2-D
        // cubic evaluator. Anyone authoring presets externally should not expect
        // horizontal-handle effects.
        float handleX = 0.0f;
        float handleY = 0.0f;    // offset from segment midpoint
    };

    std::string id;              // stable ID used as modulation source key, e.g. "cs0"

    Mode        mode        = Mode::Stepped;
    Polarity    polarity    = Polarity::Bipolar;
    InputSource inputSource = InputSource::Internal;
    int         midiCCNumber = 0;

    NoteValue loopNoteValue  = NoteValue::Quarter;
    NoteMod   loopNoteMod    = NoteMod::None;
    int       loopMultiplier = 4;           // 4 quarter notes = 1 bar default

    NoteValue stepNoteValue  = NoteValue::Quarter;  // stepped mode only
    NoteMod   stepNoteMod    = NoteMod::None;
    int       stepMultiplier = 1;

    std::vector<float>      stepValues;   // stepped mode: value per step, -100..+100
    std::vector<CurvePoint> curvePoints;  // smooth mode: sorted by x ascending

    // Returns the loop length in beats (assumes 4/4 quarter-note = 1 beat).
    double getLoopLengthBeats() const;

    // Returns the step count for stepped mode (= round(loopLength / stepLength)).
    int getStepCount() const;

    // Evaluates the output at the given absolute song position in beats.
    // Returns a value in [-100, +100] (bipolar) or [0, +100] (unipolar).
    float evaluate(double songBeatPos) const;

private:
    static double noteValueToBeats(NoteValue nv, NoteMod mod);

    float evaluateStepped(double phase) const;
    float evaluateSmooth(double phase) const;
};
