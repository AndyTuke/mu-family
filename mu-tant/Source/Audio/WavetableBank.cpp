#include "WavetableBank.h"
#include <juce_dsp/juce_dsp.h>
#include <cmath>
#include <cstring>
#include <cstdint>
#include <algorithm>

namespace mu_tant
{

static constexpr double kPi    = 3.14159265358979323846;
static constexpr double kTwoPi = 6.283185307179586;

// Serum/Vital embed the single-cycle frame length in a RIFF "clm " sub-chunk,
// an ASCII string of the form "<!>2048 10000000 wavetable (www.xferrecords.com)".
// Walk the WAV's chunk list for "clm " and return the integer after "<!>", or 0
// if the chunk is absent / unparsable. Lets us slice imports authored at frame
// sizes other than 2048 correctly instead of assuming the default.
static int parseSerumFrameSize(const void* data, size_t numBytes) noexcept
{
    const auto* bytes = static_cast<const uint8_t*>(data);
    if (numBytes < 12 || std::memcmp(bytes, "RIFF", 4) != 0 || std::memcmp(bytes + 8, "WAVE", 4) != 0)
        return 0;

    size_t pos = 12;   // first chunk after the RIFF/WAVE header
    while (pos + 8 <= numBytes)
    {
        const uint32_t sz = (uint32_t) bytes[pos + 4]        | ((uint32_t) bytes[pos + 5] << 8)
                          | ((uint32_t) bytes[pos + 6] << 16) | ((uint32_t) bytes[pos + 7] << 24);
        const size_t body = pos + 8;
        if (std::memcmp(bytes + pos, "clm ", 4) == 0)
        {
            const size_t avail = std::min((size_t) sz, numBytes - body);
            juce::String s (juce::CharPointer_UTF8 ((const char*) (bytes + body)),
                            juce::CharPointer_UTF8 ((const char*) (bytes + body + avail)));
            const int marker = s.indexOf("<!>");
            return (marker >= 0 ? s.substring(marker + 3) : s).trimStart().getIntValue();
        }
        pos = body + sz + (sz & 1u);   // RIFF chunks are word-aligned (pad odd sizes)
    }
    return 0;
}

WavetableBank::WavetableBank()
{
    formatManager.registerBasicFormats();
}

const juce::StringArray& WavetableBank::factoryTableNames()
{
    static const juce::StringArray names {
        "Basic Shapes", "Saw Morph", "Square PWM", "FM Stack", "Formant", "Additive"
    };
    return names;
}

const juce::String& WavetableBank::tableName(int t) const noexcept
{
    static const juce::String empty;
    return (t >= 0 && t < (int) tables.size()) ? tables[(size_t) t].name : empty;
}

int WavetableBank::numFrames(int t) const noexcept
{
    return (t >= 0 && t < (int) tables.size()) ? tables[(size_t) t].frames : 0;
}

// ── Procedural factory synthesis ───────────────────────────────────────────────
// One full-res frame for a factory table at morph position `m`. Additive where it
// keeps the frame naturally band-limited; the FFT mip pass cleans up the rest.
void WavetableBank::synthFactoryFrame(Factory kind, float m, float* out, int size) noexcept
{
    const int maxH = std::min(size / 2 - 1, 256);   // top harmonic synthesised
    m = juce::jlimit(0.0f, 1.0f, m);

    switch (kind)
    {
        case Factory::SawMorph:        // sine → band-limited saw as harmonics fade in
            for (int i = 0; i < size; ++i)
            {
                const double x = (double) i / size;
                double acc = std::sin(kTwoPi * x);
                for (int k = 2; k <= maxH; ++k)
                    acc += ((double) m / k) * std::sin(kTwoPi * k * x);
                out[i] = (float) acc;
            }
            break;

        case Factory::SquarePwm:       // band-limited pulse, duty 0.5 → 0.1
        {
            const double duty = 0.5 - 0.4 * (double) m;
            for (int i = 0; i < size; ++i)
            {
                const double x = (double) i / size;
                double acc = 0.0;
                for (int k = 1; k <= maxH; ++k)
                    acc += (2.0 / (k * kPi)) * std::sin(k * kPi * duty) * std::cos(kTwoPi * k * x);
                out[i] = (float) acc;
            }
            break;
        }

        case Factory::FmStack:         // 2-op FM, index 0 → 8
        {
            const double ratio = 2.0, index = 8.0 * (double) m;
            for (int i = 0; i < size; ++i)
            {
                const double x = (double) i / size;
                out[i] = (float) std::sin(kTwoPi * x + index * std::sin(kTwoPi * ratio * x));
            }
            break;
        }

        case Factory::Formant:         // gaussian harmonic peak sweeping up the series
        {
            const double centre = 2.0 + 30.0 * (double) m, width = 6.0;
            for (int i = 0; i < size; ++i)
            {
                const double x = (double) i / size;
                double acc = 0.0;
                for (int k = 1; k <= maxH; ++k)
                {
                    const double d = ((double) k - centre) / width;
                    acc += std::exp(-0.5 * d * d) * std::sin(kTwoPi * k * x);
                }
                out[i] = (float) acc;
            }
            break;
        }

        case Factory::Additive:        // moving harmonic emphasis (comb-like)
        {
            const double peak = 1.0 + 15.0 * (double) m;
            for (int i = 0; i < size; ++i)
            {
                const double x = (double) i / size;
                double acc = 0.0;
                for (int k = 1; k <= maxH; ++k)
                    acc += (1.0 / (1.0 + std::abs((double) k - peak))) * std::sin(kTwoPi * k * x);
                out[i] = (float) acc;
            }
            break;
        }

        case Factory::BasicShapes:     // sine → triangle → saw → square
        default:
        {
            auto sineS = [&](double x) { return std::sin(kTwoPi * x); };
            auto triS  = [&](double x) { double a = 0; for (int k = 1; k <= maxH; k += 2) { const int n = (k - 1) / 2; a += ((n % 2 == 0) ? 1.0 : -1.0) * std::sin(kTwoPi * k * x) / (double) (k * k); } return a * (8.0 / (kPi * kPi)); };
            auto sawS  = [&](double x) { double a = 0; for (int k = 1; k <= maxH; ++k) a += std::sin(kTwoPi * k * x) / (double) k; return a * (2.0 / kPi); };
            auto sqS   = [&](double x) { double a = 0; for (int k = 1; k <= maxH; k += 2) a += std::sin(kTwoPi * k * x) / (double) k; return a * (4.0 / kPi); };

            const double seg = (double) m * 3.0;
            int s0 = (int) seg; double f = seg - s0;
            if (s0 >= 3) { s0 = 2; f = 1.0; }
            auto eval = [&](int s, double x) { return s == 0 ? sineS(x) : s == 1 ? triS(x) : s == 2 ? sawS(x) : sqS(x); };
            for (int i = 0; i < size; ++i)
            {
                const double x = (double) i / size;
                const double v0 = eval(s0, x), v1 = eval(s0 + 1, x);
                out[i] = (float) (v0 + f * (v1 - v0));
            }
            break;
        }
    }

    // Normalise the frame to unit peak so morph / mip changes don't jump level.
    float pk = 0.0f;
    for (int i = 0; i < size; ++i) pk = std::max(pk, std::abs(out[i]));
    if (pk > 1.0e-6f) { const float g = 1.0f / pk; for (int i = 0; i < size; ++i) out[i] *= g; }
}

// ── Mip building ────────────────────────────────────────────────────────────────
// For each frame: FFT once, then per mip level zero the bins above that octave's
// harmonic ceiling, inverse FFT, and decimate to the level's (smaller) table size.
Wavetable WavetableBank::buildTableData(const std::vector<float>& fullRes, int frameCount,
                                        int frameSize, const juce::String& name) const
{
    if (frameCount <= 0 || frameSize < kMinMipSize) return {};   // frames == 0 → failure

    Wavetable wt;
    wt.name   = name;
    wt.frames = frameCount;
    for (int s = frameSize; s >= kMinMipSize; s /= 2) wt.mipSize.push_back(s);
    const int numMips = wt.numMips();
    wt.mip.resize((size_t) numMips);
    for (int L = 0; L < numMips; ++L)
        wt.mip[(size_t) L].assign((size_t) frameCount * (size_t) wt.mipSize[(size_t) L], 0.0f);

    const int order = (int) std::lround(std::log2((double) frameSize));
    juce::dsp::FFT fft(order);
    std::vector<float> fwd((size_t) frameSize * 2, 0.0f);
    std::vector<float> inv((size_t) frameSize * 2, 0.0f);

    for (int f = 0; f < frameCount; ++f)
    {
        const float* src = fullRes.data() + (size_t) f * frameSize;

        // Level 0 = full resolution, stored as-is.
        std::copy(src, src + frameSize, wt.mip[0].data() + (size_t) f * frameSize);

        std::fill(fwd.begin(), fwd.end(), 0.0f);
        std::copy(src, src + frameSize, fwd.begin());
        fft.performRealOnlyForwardTransform(fwd.data(), true);

        for (int L = 1; L < numMips; ++L)
        {
            const int keep = std::min(wt.mipSize[(size_t) L] / 2, frameSize / 2);
            std::fill(inv.begin(), inv.end(), 0.0f);
            for (int k = 0; k <= keep; ++k) { inv[(size_t) (2 * k)] = fwd[(size_t) (2 * k)]; inv[(size_t) (2 * k + 1)] = fwd[(size_t) (2 * k + 1)]; }
            fft.performRealOnlyInverseTransform(inv.data());

            const int sz   = wt.mipSize[(size_t) L];
            const int step = frameSize / sz;
            float* dst = wt.mip[(size_t) L].data() + (size_t) f * sz;
            for (int i = 0; i < sz; ++i) dst[i] = inv[(size_t) (i * step)];
        }
    }

    // Global normalise by the full-res peak (preserves relative frame levels).
    float pk = 0.0f;
    for (float v : wt.mip[0]) pk = std::max(pk, std::abs(v));
    if (pk > 1.0e-6f) { const float g = 1.0f / pk; for (auto& lvl : wt.mip) for (float& v : lvl) v *= g; }

    return wt;
}

// Append path used by the factory / ctor build (no audio thread running).
void WavetableBank::buildTable(const std::vector<float>& fullRes, int frameCount,
                               int frameSize, const juce::String& name)
{
    auto wt = buildTableData(fullRes, frameCount, frameSize, name);
    if (wt.frames > 0) tables.push_back(std::move(wt));
}

// ── WAV ingestion ─────────────────────────────────────────────────────────────
// Decode a mono Serum/Vital WAV into a Wavetable WITHOUT touching the bank (pure
// aside from the format reader) — safe to run off any lock.
Wavetable WavetableBank::decodeWavData(const void* wavData, size_t numBytes, const juce::String& name)
{
    auto stream = std::make_unique<juce::MemoryInputStream>(wavData, numBytes, false);
    std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(std::move(stream)));
    if (reader == nullptr) return {};

    const int total = (int) reader->lengthInSamples;
    const int ch    = (int) reader->numChannels;

    // Prefer the frame size declared in Serum's "clm " chunk; accept it only if it's
    // a power of two in range (FFT mip-mapping needs power-of-two), else use 2048.
    int frameSize = parseSerumFrameSize(wavData, numBytes);
    if (frameSize < kMinMipSize || frameSize > 8192 || (frameSize & (frameSize - 1)) != 0)
        frameSize = kFrameSize;

    if (total < frameSize || ch < 1) return {};

    juce::AudioBuffer<float> buf(ch, total);
    reader->read(&buf, 0, total, 0, true, ch > 1);

    const int usable = (total / frameSize) * frameSize;   // whole frames only
    std::vector<float> mono((size_t) usable);
    for (int i = 0; i < usable; ++i)
    {
        float s = 0.0f;
        for (int c = 0; c < ch; ++c) s += buf.getSample(c, i);
        mono[(size_t) i] = s / (float) ch;
    }

    return buildTableData(mono, usable / frameSize, frameSize, name);
}

// Append path: decode (under the caller's lock) then push. Kept for the immediate /
// stopped load + tests. The hot-swap preload uses decodeFile + appendTable instead,
// to keep the decode off the lock.
int WavetableBank::addFromWav(const void* wavData, size_t numBytes, const juce::String& name)
{
    auto wt = decodeWavData(wavData, numBytes, name);
    if (wt.frames <= 0) return -1;
    tables.push_back(std::move(wt));
    return numTables() - 1;
}

// Decode a file into a tagged Wavetable WITHOUT touching the bank — safe off-lock.
Wavetable WavetableBank::decodeFile(const juce::File& file)
{
    juce::MemoryBlock mb;
    if (! file.loadFileAsData(mb)) return {};
    auto wt = decodeWavData(mb.getData(), mb.getSize(), file.getFileNameWithoutExtension());
    if (wt.frames > 0) wt.sourcePath = file.getFullPathName();   // tag for dedup + persistence
    return wt;
}

// Install a pre-decoded table. Caller holds the bank lock (voicesLock) for the brief
// push; returns the new index, or -1 for an empty/failed decode.
int WavetableBank::appendTable(Wavetable&& wt)
{
    if (wt.frames <= 0) return -1;
    tables.push_back(std::move(wt));
    return numTables() - 1;
}

int WavetableBank::addFromFile(const juce::File& file)
{
    auto wt = decodeFile(file);
    if (wt.frames <= 0) return -1;
    return appendTable(std::move(wt));
}

int WavetableBank::findByPath(const juce::String& absolutePath) const noexcept
{
    if (absolutePath.isEmpty()) return -1;
    for (int i = 0; i < (int) tables.size(); ++i)
        if (tables[(size_t) i].sourcePath == absolutePath) return i;
    return -1;
}

int WavetableBank::addOrLoadFile(const juce::File& file)
{
    const int existing = findByPath(file.getFullPathName());
    if (existing >= 0) return existing;
    return addFromFile(file);
}

void WavetableBank::appendProceduralFactory(Factory kind, const juce::String& name)
{
    std::vector<float> full((size_t) kFactoryFrames * kFrameSize);
    for (int f = 0; f < kFactoryFrames; ++f)
    {
        const float m = (kFactoryFrames > 1) ? (float) f / (float) (kFactoryFrames - 1) : 0.0f;
        synthFactoryFrame(kind, m, full.data() + (size_t) f * kFrameSize, kFrameSize);
    }
    buildTable(full, kFactoryFrames, kFrameSize, name);
}

void WavetableBank::loadFactoryBank()
{
    // Phase 1: synthesise the factory bank procedurally so the engine has the full
    // named set. Phase 2 swaps this for loading the embedded Serum/Vital .wav assets
    // (authored by tools/wavetable-gen) through addFromWav — same internal model.
    tables.clear();
    const auto& names = factoryTableNames();
    const Factory kinds[] = { Factory::BasicShapes, Factory::SawMorph, Factory::SquarePwm,
                              Factory::FmStack, Factory::Formant, Factory::Additive };
    const int n = std::min((int) names.size(), (int) std::size(kinds));
    for (int i = 0; i < n; ++i)
        appendProceduralFactory(kinds[(size_t) i], names[i]);
}

// ── Runtime sampling ──────────────────────────────────────────────────────────
int WavetableBank::mipForInc(const Wavetable& wt, double inc) const noexcept
{
    if (inc <= 0.0) return 0;
    const int allowed = (int) std::floor(0.5 / inc);   // harmonics below Nyquist
    for (int L = 0; L < wt.numMips(); ++L)
        if (wt.mipSize[(size_t) L] / 2 <= allowed) return L;
    return wt.numMips() - 1;
}

float WavetableBank::frameSample(int t, double inc, int frame, float phase01) const noexcept
{
    if (t < 0 || t >= (int) tables.size()) return 0.0f;
    const Wavetable& wt = tables[(size_t) t];
    if (! wt.valid()) return 0.0f;

    const int L  = mipForInc(wt, inc);
    const int sz = wt.mipSize[(size_t) L];
    frame = juce::jlimit(0, wt.frames - 1, frame);

    phase01 -= std::floor(phase01);
    const float pos  = phase01 * (float) sz;
    int         i0   = (int) pos;
    if (i0 >= sz) i0 = sz - 1;
    const int   i1   = (i0 + 1) % sz;
    const float frac = pos - (float) i0;

    const float* fr = wt.mip[(size_t) L].data() + (size_t) frame * sz;
    return fr[i0] + frac * (fr[i1] - fr[i0]);
}

} // namespace mu_tant
