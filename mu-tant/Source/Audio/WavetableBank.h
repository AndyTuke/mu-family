#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>
#include <vector>

// mu-tant wavetable storage (design-voice.md "Wavetable oscillator").
//
// The internal model is the Serum / Vital standard: each table is a set of
// single-cycle frames of 2048 samples. Tables enter the bank through ONE path —
// a mono WAV decode (`addFromWav` / `addFromFile`) — whether they are the
// embedded factory bank or a user-imported Serum/Vital file.
//
// Anti-aliasing is mip-mapped per octave: each frame is band-limited with an FFT
// and stored at successively smaller table sizes (2048 → 8). At runtime the
// oscillator reads the mip level whose harmonic content fits below Nyquist for
// the current pitch, so high notes never alias — no realtime FFT.
namespace mu_tant
{

// One wavetable: `frames` morph frames, each held at multiple band-limited mip
// resolutions. mip[0] is the full 2048-sample frame; each higher level halves
// the table size and harmonic content.
struct Wavetable
{
    juce::String                    name;
    juce::String                    sourcePath;   // empty for factory tables; file path for user imports
    int                             frames = 0;
    std::vector<int>                mipSize;   // samples per frame at each level
    std::vector<std::vector<float>> mip;       // mip[level] : frames*mipSize[level], row-major

    int  numMips() const noexcept { return (int) mipSize.size(); }
    bool valid()   const noexcept { return frames > 0 && ! mip.empty(); }
};

class WavetableBank
{
public:
    WavetableBank();

    static constexpr int kFrameSize = 2048;   // Serum / Vital single-cycle frame length

    // Fixed factory table names — the single source of truth for the APVTS
    // parameter range and the UI dropdown. Order matches the embedded .wav assets.
    static const juce::StringArray& factoryTableNames();

    // Load the embedded factory bank (one .wav per factoryTableNames() entry).
    // Falls back to procedurally-synthesised tables if the assets are unavailable,
    // so the engine always has the full named set.
    void loadFactoryBank();

    // Decode a mono Serum/Vital WAV (single-cycle frames concatenated), build mips,
    // append as a named table. Frame size comes from the Serum "clm " chunk when
    // present (power-of-two), else defaults to kFrameSize. Returns the new table
    // index, or -1 on failure.
    int  addFromWav(const void* wavData, size_t numBytes, const juce::String& name);
    int  addFromFile(const juce::File& file);

    // User-import helpers: dedup by absolute path so re-loading the same file (or two
    // voices using it) reuses one bank slot. findByPath returns the index or -1.
    int  findByPath(const juce::String& absolutePath) const noexcept;
    int  addOrLoadFile(const juce::File& file);

    // ── Two-phase load for a real-time-safe hot-swap preload (#888) ─────────────
    // decodeFile does ALL the slow work (file read + WAV decode + FFT mip build)
    // into a returned Wavetable WITHOUT touching the bank, so it runs off any lock;
    // appendTable then installs a pre-decoded table (caller holds the bank lock only
    // for the brief push). The hot-swap stage decodes off-lock then appends under a
    // microsecond lock — instead of decoding under the lock, which silenced the audio
    // render for the whole decode (the residual ~1-in-10 swap pause). Returns a table
    // with frames == 0 on failure.
    Wavetable decodeFile(const juce::File& file);
    int       appendTable(Wavetable&& wt);   // returns the new index (caller locks)

    int  numTables() const noexcept { return (int) tables.size(); }
    const juce::String& tableName(int t) const noexcept;
    int  numFrames(int t) const noexcept;

    // Anti-aliased sample: choose the mip level from `inc` (cycles/sample), then
    // read frame `frame` at normalised `phase01` (wrapped, linearly interpolated).
    float frameSample(int t, double inc, int frame, float phase01) const noexcept;

    // ── Procedural factory synthesis (shared with tools/wavetable-gen) ──────────
    // Factory table kinds, indexed to match factoryTableNames().
    enum class Factory { BasicShapes, SawMorph, SquarePwm, FmStack, Formant, Additive };

    // Fill one full-resolution (kFrameSize) frame for a factory table at morph
    // position `morph01` (0..1 across the table's frames). Pure, allocation-free,
    // shared by the generator tool so the bank and the .wav assets stay identical.
    static void synthFactoryFrame(Factory kind, float morph01, float* out, int size) noexcept;

private:
    static constexpr int kMinMipSize     = 8;    // smallest band-limited table
    static constexpr int kFactoryFrames  = 64;   // morph frames per factory table

    std::vector<Wavetable>   tables;
    juce::AudioFormatManager formatManager;

    // Build all mip levels for a table from `frameCount` full-res frames of
    // `frameSize` samples (row-major) and normalise. buildTableData returns the
    // table without touching the bank (pure — safe off any lock); buildTable
    // appends it (factory / ctor path, no audio thread). decodeWavData decodes a
    // mono WAV into a table (no bank access).
    Wavetable buildTableData(const std::vector<float>& fullRes, int frameCount, int frameSize,
                             const juce::String& name) const;
    void      buildTable(const std::vector<float>& fullRes, int frameCount, int frameSize,
                         const juce::String& name);
    Wavetable decodeWavData(const void* wavData, size_t numBytes, const juce::String& name);

    int mipForInc(const Wavetable& wt, double inc) const noexcept;

    // Synthesise a factory table procedurally (fallback / no-asset path).
    void appendProceduralFactory(Factory kind, const juce::String& name);
};

} // namespace mu_tant
