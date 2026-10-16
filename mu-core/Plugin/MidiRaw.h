#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

// Note-message tests on the raw bytes of a MidiBuffer event. Audio-thread code uses these instead
// of metadata.getMessage(): building a juce::MidiMessage allocates for messages over 8 bytes
// (SysEx), which must never happen inside processBlock. Same meaning as MidiMessage::isNoteOn()
// (velocity 0 is not a Note On) and isNoteOff() (a Note On with velocity 0 counts as a Note Off).
namespace mu_midi
{
inline bool isNoteOn (const juce::MidiMessageMetadata& meta) noexcept
{
    return meta.numBytes >= 3 && (meta.data[0] & 0xf0) == 0x90 && meta.data[2] != 0;
}

inline bool isNoteOff (const juce::MidiMessageMetadata& meta) noexcept
{
    if (meta.numBytes < 3) return false;
    const int status = meta.data[0] & 0xf0;
    return status == 0x80 || (status == 0x90 && meta.data[2] == 0);
}

// Note number of a Note On / Note Off event (call only after isNoteOn / isNoteOff returned true).
inline int noteNumber (const juce::MidiMessageMetadata& meta) noexcept
{
    return meta.data[1] & 0x7f;
}
} // namespace mu_midi
