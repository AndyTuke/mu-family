#pragma once

#include <array>
#include <cmath>
#include <cstdlib>
#include <juce_core/juce_core.h>   // midiNoteName returns a juce::String

// The family scale table — one copy for every product (mu-Tant's scale-quantised pitch,
// mu-Toni's arp root palette + diatonic snap). Dropdown order is the table order.
//
// A `tone` is a continuous scale-degree position: floor(tone) selects a degree and the
// fraction glides between adjacent degrees in MIDI space (perceptually even, logarithmic in
// frequency). Step modulators land on whole tones; smooth ones glide through the fractions.
namespace mu_audio
{

struct Scale
{
    const char*         name;
    std::array<int, 12> offsets;   // semitone offsets within one octave
    int                 count;     // degrees per octave (<= 12)
};

inline constexpr std::array<Scale, 12> kScales = {{
    { "Major",            {{0,2,4,5,7,9,11,0,0,0,0,0}}, 7 },
    { "Minor",            {{0,2,3,5,7,8,10,0,0,0,0,0}}, 7 },
    { "Dorian",           {{0,2,3,5,7,9,10,0,0,0,0,0}}, 7 },
    { "Phrygian",         {{0,1,3,5,7,8,10,0,0,0,0,0}}, 7 },
    { "Lydian",           {{0,2,4,6,7,9,11,0,0,0,0,0}}, 7 },
    { "Mixolydian",       {{0,2,4,5,7,9,10,0,0,0,0,0}}, 7 },
    { "Locrian",          {{0,1,3,5,6,8,10,0,0,0,0,0}}, 7 },
    { "Harmonic Minor",   {{0,2,3,5,7,8,11,0,0,0,0,0}}, 7 },
    { "Pentatonic Major", {{0,2,4,7,9,0,0,0,0,0,0,0}},  5 },
    { "Pentatonic Minor", {{0,3,5,7,10,0,0,0,0,0,0,0}}, 5 },
    { "Blues",            {{0,3,5,6,7,10,0,0,0,0,0,0}}, 6 },
    { "Chromatic",        {{0,1,2,3,4,5,6,7,8,9,10,11}}, 12 },
}};

inline constexpr int kNumScales = (int) kScales.size();

inline int clampScaleIndex(int i) noexcept
{
    return i < 0 ? 0 : (i >= kNumScales ? kNumScales - 1 : i);
}

// Semitone offset of scale degree `t` (can exceed one octave: degree 8 in a 7-note scale =
// degree 1 an octave up).
inline float scaleSemitone(const Scale& s, int t) noexcept
{
    const int n   = s.count;
    int       idx = t % n;
    int       oct = t / n;
    if (idx < 0) { idx += n; --oct; }   // floor-division for negative tones
    return (float) s.offsets[(size_t) idx] + (float) oct * 12.0f;
}

// Continuous tone -> MIDI note. `root` 0..11 (C..B), `octave` 0..8, `fine` cents.
inline float toneToMidi(int scaleIndex, int root, int octave, float tone, float fineCents) noexcept
{
    const Scale& s  = kScales[(size_t) clampScaleIndex(scaleIndex)];
    const int    t0 = (int) std::floor(tone);
    const float  fr = tone - (float) t0;
    const float  semi = scaleSemitone(s, t0) + fr * (scaleSemitone(s, t0 + 1) - scaleSemitone(s, t0));
    return (float) root + 12.0f * (float) octave + semi + fineCents * 0.01f;
}

// Snap a semitone offset above the root to the nearest scale tone (diatonic snap). The
// octave is preserved; only the pitch within the octave snaps.
inline int snapToScale(int semitoneAboveRoot, int scaleIndex) noexcept
{
    const Scale& s = kScales[(size_t) clampScaleIndex(scaleIndex)];
    int oct    = semitoneAboveRoot / 12;
    int within = semitoneAboveRoot % 12;
    if (within < 0) { within += 12; --oct; }

    // Nearest scale tone, also considering the root an octave up (the nearest may wrap).
    int best = s.offsets[0];
    int bestDist = 128;
    for (int i = 0; i < s.count; ++i)
    {
        const int d = std::abs(within - s.offsets[(size_t) i]);
        if (d < bestDist) { bestDist = d; best = s.offsets[(size_t) i]; }
    }
    if (std::abs(within - 12) < bestDist) best = 12;

    return oct * 12 + best;
}

// Snap a (fractional) MIDI note to the nearest note of the scale on `root` (0..11, C..B), keeping its octave.
// Used where a continuous pitch knob or modulator must land on scale notes (mu-On's Kick and Bass tune).
inline float snapMidiToScale(float midi, int root, int scaleIndex) noexcept
{
    const Scale& s = kScales[(size_t) clampScaleIndex(scaleIndex)];
    const float rel  = midi - (float) root;
    const float oct  = std::floor(rel / 12.0f);
    const float in   = rel - oct * 12.0f;   // 0..12 above the root within this octave
    float best = (float) s.offsets[0], bestDist = std::abs(in - best);
    for (int i = 1; i < s.count; ++i)
    {
        const float d = std::abs(in - (float) s.offsets[(size_t) i]);
        if (d < bestDist) { bestDist = d; best = (float) s.offsets[(size_t) i]; }
    }
    if (std::abs(in - 12.0f) < bestDist) best = 12.0f;   // the root an octave up can be nearer than any degree
    return (float) root + oct * 12.0f + best;
}

// Note name of a MIDI note ("C2", "F#3"; middle C = C4 = 60), and whether it belongs to the scale on `root`.
inline juce::String midiNoteName(int midi)
{
    static const char* const kNames[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    const int pc = ((midi % 12) + 12) % 12;
    return juce::String(kNames[pc]) + juce::String((midi - pc) / 12 - 1);
}

inline bool midiInScale(int midi, int root, int scaleIndex) noexcept
{
    const Scale& s = kScales[(size_t) clampScaleIndex(scaleIndex)];
    const int within = (((midi - root) % 12) + 12) % 12;
    for (int i = 0; i < s.count; ++i)
        if (s.offsets[(size_t) i] == within) return true;
    return false;
}

inline float freqToMidi(float hz) noexcept
{
    return 69.0f + 12.0f * std::log2(juce::jmax(1.0e-3f, hz) / 440.0f);
}

inline float midiToFreq(float midi) noexcept
{
    return 440.0f * std::pow(2.0f, (midi - 69.0f) / 12.0f);
}

} // namespace mu_audio
