#pragma once

#include <juce_core/juce_core.h>

// Headless render-to-WAV mode for the standalone. Invoked when the standalone is
// launched with `--render --out <out.wav> [--seconds N --samplerate SR --blocksize BS]`.
// Bypasses the GUI window and the audio device, drives processBlock() in a tight loop,
// and writes the captured output as a WAV.
//
// mu-Tant is a wavetable synth: at default the transport is stopped, which holds the
// gate fully open, so the oscillators drone — the render produces guaranteed non-silent
// audio with NO preset and NO sample dependency. That makes it the family's host- and
// content-independent "does the Mac build actually make sound" check (see
// .github/workflows/mac-validate.yml + tests/expectations/mutant_drone.json).
namespace mu_tant::render_mode {

struct Args
{
    juce::File outputWav;
    double     seconds    = 4.0;
    double     sampleRate = 48000.0;
    int        blockSize  = 512;
    bool       valid      = false;
};

// Parse the standalone command line. Returns valid=true only when a recognised
// `--render` invocation is present; on parse error returns valid=false and prints
// an error to stderr.
Args parse(const juce::String& commandLine);

// Run the render. Returns 0 on success, non-zero on error. Caller quits the app after.
int execute(const Args& args);

} // namespace mu_tant::render_mode
