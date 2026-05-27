#pragma once

#include <vector>

// mu-tant wavetable storage (design-voice.md "Wavetable oscillator").
//
// FIRST STAB: a single procedurally-generated morph table (sine -> band-limited
// saw across the frames) so the oscillator has something to read before the real
// Serum/Vital-format bank + the custom bank-gen tool exist. The oscillator DSP is
// identical regardless of where the table content comes from.
//
// DEFERRED vs the design: per-octave mip-mapping (the design computes 10 mip
// levels via FFT at bank-load). Here the saw is band-limited to a fixed harmonic
// count, which is alias-reasonable for a drone at moderate pitches but will alias
// on very high notes — replace with mip-mapped tables when the real bank lands.
namespace mu_tant
{

class WavetableBank
{
public:
    // Generate the built-in morph table. tableSize = samples per single-cycle
    // frame; numFrames = morph steps (frame 0 = sine, last frame = saw).
    void generateBuiltIn(int numFrames = 64, int tableSize = 2048, int maxHarmonics = 64);

    int numFrames() const noexcept { return frames; }
    int tableSize() const noexcept { return size; }

    // Sample at morph frame `f` (clamped) and normalised phase `phase01` (wrapped),
    // linearly interpolated within the frame.
    float frameSample(int f, float phase01) const noexcept;

private:
    std::vector<float> data;   // frames * size, row-major by frame
    int frames = 0;
    int size   = 0;
};

} // namespace mu_tant
