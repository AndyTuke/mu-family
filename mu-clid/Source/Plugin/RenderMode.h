#pragma once

#include <juce_core/juce_core.h>

// Headless render-to-WAV mode for the standalone. Invoked when the standalone
// is launched with `--render <out.wav> --seconds N [...]` on the command line.
// Bypasses the GUI window and the audio device, drives processBlock() in a
// tight loop, writes the captured output as a stereo WAV, and quits the app.
//
// Used by the listening-test pipeline in `tests/scripts/run-listening-tests.py`
// to produce reproducible audio for automated analysis (see
// `tests/scripts/analyse.py`).
namespace mu_clid::render_mode {

struct Args
{
    juce::File outputWav;
    juce::File presetFile;          // optional — empty = use whatever loads by default
    juce::File swapPresetFile;      // optional — a 2nd preset loaded mid-render (--swap-preset)
    double     seconds      = 4.0;
    double     sampleRate   = 48000.0;
    int        blockSize    = 512;
    double     swapAtSeconds = -1.0; // when to load swapPresetFile (--swap-at); <0 = no swap

    // Per-rhythm hot-swap (A9): stage a .muRhythm onto one slot mid-render via
    // the deferred stageRhythmPreset path (--swap-rhythm-preset / -slot / -at).
    juce::File swapRhythmFile;
    int        swapRhythmSlot      = 0;
    double     swapRhythmAtSeconds = -1.0;

    // MIDI program-change → full-preset load (A2): seed the channel-9 full-preset
    // map with program->preset, then inject a PC mid-render
    // (--midi-program / --midi-program-preset / --midi-program-at).
    juce::File midiProgramPreset;
    int        midiProgram          = -1;
    double     midiProgramAtSeconds = -1.0;

    bool       valid        = false;
};

// Parse the standalone command line. Returns `valid=true` only when a recognised
// `--render` invocation is present; on success the caller should skip normal GUI
// startup and run `execute()` instead. On parse error returns valid=false AND
// prints an error to stderr.
Args parse(const juce::String& commandLine);

// Run the render. Returns 0 on success, non-zero on error. Caller is expected
// to quit the JUCEApplication after execute returns.
int execute(const Args& args);

} // namespace mu_clid::render_mode
