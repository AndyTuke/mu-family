#pragma once

#include "MidiClockEstimator.h"
#include <juce_audio_devices/juce_audio_devices.h>

// MidiClockInput — the thin JUCE adapter between a live MIDI input and MidiClockEstimator
// (L7). Registered on the AudioDeviceManager as a MIDI-input callback; it parses the realtime
// bytes (clock / start / continue / stop / song position) and forwards them to the pure estimator, stamping
// each pulse with the message's capture timestamp (arrival time if it has none). All the logic lives in the estimator (which is unit-tested headless); this
// class is just the wiring, so it stays in mu-link where the audio modules are available.
namespace mu_link
{

class MidiClockInput : public juce::MidiInputCallback
{
public:
    MidiClockEstimator&       estimator()       noexcept { return est; }
    const MidiClockEstimator& estimator() const noexcept { return est; }

    // The time to stamp a pulse with, in seconds: the driver's capture time the message carries
    // (JUCE gives it on the getMillisecondCounterHiRes clock), so OS scheduling jitter in this
    // callback doesn't reach the tempo estimate. A message with no timestamp falls back to
    // `arrivalSeconds`.
    static double pulseTimestamp(const juce::MidiMessage& m, double arrivalSeconds) noexcept
    {
        const double t = m.getTimeStamp();
        return t > 0.0 ? t : arrivalSeconds;
    }

    void handleIncomingMidiMessage(juce::MidiInput*, const juce::MidiMessage& m) override
    {
        if (m.isMidiClock())
            est.onClockPulse(pulseTimestamp(m, juce::Time::getMillisecondCounterHiRes() * 0.001));
        else if (m.isMidiStart())
            est.onStart();
        else if (m.isMidiContinue())
            est.onContinue();
        else if (m.isMidiStop())
            est.onStop();
        else if (m.isSongPositionPointer())
            est.onSongPosition(m.getSongPositionPointerMidiBeat());
    }

private:
    MidiClockEstimator est;
};

} // namespace mu_link
