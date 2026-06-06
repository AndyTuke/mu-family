#pragma once

#include "MidiClockEstimator.h"
#include <juce_audio_devices/juce_audio_devices.h>

// MidiClockInput — the thin JUCE adapter between a live MIDI input and MidiClockEstimator
// (L7). Registered on the AudioDeviceManager as a MIDI-input callback; it parses the realtime
// bytes (clock / start / continue / stop) and forwards them to the pure estimator, stamping
// each pulse with a monotonic hi-res time so the tempo estimate doesn't depend on platform
// MIDI timestamps. All the logic lives in the estimator (which is unit-tested headless); this
// class is just the wiring, so it stays in mu-link where the audio modules are available.
namespace mu_link
{

class MidiClockInput : public juce::MidiInputCallback
{
public:
    MidiClockEstimator&       estimator()       noexcept { return est; }
    const MidiClockEstimator& estimator() const noexcept { return est; }

    void handleIncomingMidiMessage(juce::MidiInput*, const juce::MidiMessage& m) override
    {
        if (m.isMidiClock())
            est.onClockPulse(juce::Time::getMillisecondCounterHiRes() * 0.001);
        else if (m.isMidiStart())
            est.onStart();
        else if (m.isMidiContinue())
            est.onContinue();
        else if (m.isMidiStop())
            est.onStop();
    }

private:
    MidiClockEstimator est;
};

} // namespace mu_link
