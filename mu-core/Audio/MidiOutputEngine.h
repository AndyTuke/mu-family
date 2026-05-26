#pragma once
#include <juce_audio_basics/juce_audio_basics.h>

// Generates MIDI note-on / note-off for one rhythm slot in MIDI output mode.
// Trigger when a rhythm fires; note-off is emitted after a fixed 20ms duration.
class MidiOutputEngine
{
public:
    void prepare(double sampleRate, int samplesPerBlock);

    void trigger(juce::MidiBuffer& midiOut, int sampleOffset,
                 int midiNote = 36, int midiChannel = 1, float velocity = 1.0f);

    void processBlock(juce::MidiBuffer& midiOut, int blockSize);

private:
    double sampleRate            = 44100.0;
    int    noteDurationSamples   = 882;   // 20ms at 44100
    int    noteOffCountdown      = -1;
    int    activeNote            = -1;
    int    activeChannel         = 1;
};
