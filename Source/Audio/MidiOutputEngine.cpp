#include "MidiOutputEngine.h"

void MidiOutputEngine::prepare(double sr, int)
{
    sampleRate           = sr;
    noteDurationSamples  = (int)(sr * 0.020);
}

void MidiOutputEngine::trigger(juce::MidiBuffer& midiOut, int sampleOffset,
                                int midiNote, int midiChannel, float velocity)
{
    if (activeNote >= 0)
    {
        midiOut.addEvent(juce::MidiMessage::noteOff(activeChannel, activeNote), sampleOffset);
        activeNote = -1;
    }

    const auto vel = (juce::uint8)juce::jlimit(0, 127, (int)(velocity * 127.0f));
    midiOut.addEvent(juce::MidiMessage::noteOn(midiChannel, midiNote, vel), sampleOffset);
    activeNote       = midiNote;
    activeChannel    = midiChannel;
    noteOffCountdown = noteDurationSamples;
}

void MidiOutputEngine::processBlock(juce::MidiBuffer& midiOut, int blockSize)
{
    if (activeNote < 0 || noteOffCountdown < 0) return;

    noteOffCountdown -= blockSize;
    if (noteOffCountdown <= 0)
    {
        const int offset = juce::jmax(0, blockSize + noteOffCountdown);
        midiOut.addEvent(juce::MidiMessage::noteOff(activeChannel, activeNote), offset);
        activeNote       = -1;
        noteOffCountdown = -1;
    }
}
