#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>

#include <memory>

// Decodes a loop file for a layer that plays a whole loop (docs/mu-tilate/design.md, "Loop format and cap"): float32,
// in the file's own channel count and sample rate, never resampled, never truncated. Blocking, so a caller runs it on a
// worker thread, never the audio thread; the result is immutable and shared.
namespace mu_core
{

struct LoadedLoop
{
    std::shared_ptr<const juce::AudioBuffer<float>> audio;   // null when the load failed
    double       sampleRate = 0.0;
    juce::String name;      // the file's name without extension
    juce::String error;     // empty on success; otherwise a one-line message for the status bar

    bool ok() const noexcept { return audio != nullptr; }
};

// A copy of `src` resampled from `fromRate` to `toRate` (windowed-sinc, every channel), for a layer whose engine needs the loop
// at the host rate (a time-stretcher). Allocates: a worker or message thread only. The copy is exactly
// ceil(length * toRate / fromRate) samples long.
std::shared_ptr<const juce::AudioBuffer<float>> resampleLoop(const juce::AudioBuffer<float>& src, double fromRate, double toRate);

// Reads `file`. The size check uses the reader's length x channels x 4 bytes against `maxBytes` before anything is
// decoded; a file over it is rejected with "Loop too long: N s, limit M s for this file".
LoadedLoop loadLoop(const juce::File& file, long long maxBytes);

} // namespace mu_core
