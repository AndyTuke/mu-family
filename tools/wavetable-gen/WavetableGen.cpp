// mu-tant wavetable generator (tools/wavetable-gen).
//
// Synthesises a starter library of 25 distinct wavetables and writes each as a
// Serum/Vital-format mono WAV (kFrames single-cycle frames of kSize samples,
// concatenated). Run once; the output folder is given as argv[1] (defaults to
// the muTant content "Wavetables" folder). The plugin loads these through the
// same WavetableBank loader + FFT mip-mapping as a user-imported .wav.
//
//   mu-tant-wavetable-gen "D:/OneDrive/Documents/TDP/muTant/Wavetables"
//
// Synthesis is additive (band-limited by construction) for everything except the
// FM tables (direct phase-modulation). A sine lookup table keeps it fast.

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>
#include <cmath>
#include <vector>
#include <functional>

namespace
{
constexpr int    kFrames = 64;      // morph frames per table
constexpr int    kSize   = 2048;    // samples per single-cycle frame (Serum standard)
constexpr int    kMaxH   = 256;     // top harmonic synthesised (mip band-limits at playback)
constexpr double kTwoPi  = 6.283185307179586;

std::vector<float> gSin;            // sine LUT: gSin[j] = sin(2π j / kSize)

inline float sinAt(long ki) { return gSin[(std::size_t) (((ki % kSize) + kSize) % kSize)]; }
inline float cosAt(long ki) { return sinAt(ki + kSize / 4); }

void normalise(float* out)
{
    float pk = 0.0f;
    for (int i = 0; i < kSize; ++i) pk = std::max(pk, std::abs(out[i]));
    if (pk > 1.0e-6f) { const float g = 0.99f / pk; for (int i = 0; i < kSize; ++i) out[i] *= g; }
}

// Additive: out[i] = Σ a[k]·sin(2π k x), normalised.
void additive(float* out, const std::vector<double>& a)
{
    const int maxH = (int) a.size() - 1;
    for (int i = 0; i < kSize; ++i)
    {
        double acc = 0.0;
        for (int k = 1; k <= maxH; ++k)
            if (a[(std::size_t) k] != 0.0) acc += a[(std::size_t) k] * sinAt((long) k * i);
        out[i] = (float) acc;
    }
    normalise(out);
}

// Harmonic-amplitude profiles.
double ampSaw   (int k) { return 1.0 / k; }
double ampSquare(int k) { return (k % 2 == 1) ? 1.0 / k : 0.0; }
double ampTri   (int k) { if (k % 2 == 0) return 0.0; const int n = (k - 1) / 2; return ((n % 2 == 0) ? 1.0 : -1.0) / (double) (k * k); }
double gauss    (double k, double centre, double width) { const double d = (k - centre) / width; return std::exp(-0.5 * d * d); }

// Build one full-resolution frame for table `kind` at morph `m` (0..1).
void synthFrame(int kind, float m, float* out)
{
    std::vector<double> a((std::size_t) kMaxH + 1, 0.0);
    auto setSawToSine = [&](double sawAmt) { a[1] = 1.0; for (int k = 2; k <= kMaxH; ++k) a[(std::size_t) k] = sawAmt * ampSaw(k); };

    switch (kind)
    {
        case 0:  // Basic Shapes: sine → triangle → saw → square
        {
            const double seg = (double) m * 3.0; int s0 = (int) seg; double f = seg - s0; if (s0 >= 3) { s0 = 2; f = 1.0; }
            auto sh = [&](int s, int k) { return s == 0 ? (k == 1 ? 1.0 : 0.0) : s == 1 ? ampTri(k) : s == 2 ? ampSaw(k) : ampSquare(k); };
            for (int k = 1; k <= kMaxH; ++k) a[(std::size_t) k] = sh(s0, k) + f * (sh(s0 + 1, k) - sh(s0, k));
            additive(out, a); return;
        }
        case 1:  a[1] = 1.0; additive(out, a); return;                                   // Sine
        case 2:  a[1] = 1.0; for (int k = 3; k <= kMaxH; k += 2) a[(std::size_t) k] = (double) m * ampTri(k); additive(out, a); return;  // Triangle (harmonics fade in)
        case 3:  setSawToSine((double) m); additive(out, a); return;                      // Saw (sine→saw)
        case 4:  a[1] = 1.0; for (int k = 3; k <= kMaxH; k += 2) a[(std::size_t) k] = (double) m / k; additive(out, a); return;          // Square (sine→square)
        case 5:  // Pulse PWM (duty 0.5 → 0.05)
        {
            const double duty = 0.5 - 0.45 * (double) m;
            for (int i = 0; i < kSize; ++i) { double acc = 0.0; for (int k = 1; k <= kMaxH; ++k) acc += (2.0 / (k * juce::MathConstants<double>::pi)) * std::sin(k * juce::MathConstants<double>::pi * duty) * cosAt((long) k * i); out[i] = (float) acc; }
            normalise(out); return;
        }
        case 6: case 7: case 8: case 9:  // FM, ratios 1 / 2 / 3 / 1.5
        {
            const double ratio = (kind == 6) ? 1.0 : (kind == 7) ? 2.0 : (kind == 8) ? 3.0 : 1.5;
            const double index = 8.0 * (double) m;
            for (int i = 0; i < kSize; ++i) { const double x = (double) i / kSize; out[i] = (float) std::sin(kTwoPi * x + index * std::sin(kTwoPi * ratio * x)); }
            normalise(out); return;
        }
        case 10: for (int k = 1; k <= kMaxH; ++k) a[(std::size_t) k] = gauss(k, 3.0, 1.0 + 11.0 * (double) m); additive(out, a); return;  // Formant Low (widening)
        case 11: for (int k = 1; k <= kMaxH; ++k) a[(std::size_t) k] = gauss(k, 2.0 + 38.0 * (double) m, 5.0); additive(out, a); return; // Formant Sweep (rising)
        case 12: for (int k = 1; k <= kMaxH; ++k) a[(std::size_t) k] = gauss(k, 15.0 + 30.0 * (double) m, 8.0); additive(out, a); return;// Formant High
        case 13: { const int top = 1 + (int) (m * 199.0); for (int k = 1; k <= kMaxH; k += 2) if (k <= top) a[(std::size_t) k] = 1.0 / k; additive(out, a); return; } // Odd Harmonics (count grows)
        case 14: { const int top = 2 + (int) (m * 198.0); a[1] = 1.0; for (int k = 2; k <= kMaxH; k += 2) if (k <= top) a[(std::size_t) k] = 1.0 / k; additive(out, a); return; } // Even Harmonics
        case 15: { const double peak = 1.0 + 30.0 * (double) m; for (int k = 1; k <= kMaxH; ++k) a[(std::size_t) k] = 1.0 / (1.0 + std::abs((double) k - peak)); additive(out, a); return; } // Harmonic Peak (moving)
        case 16: { const int dr[] = { 1, 2, 3, 4, 6, 8 }; const double lv[] = { 1.0, 0.8, 0.6, 0.5, 0.4, 0.3 }; for (int j = 0; j < 6; ++j) a[(std::size_t) dr[j]] = lv[j] * (j < 2 ? 1.0 : (0.3 + 0.7 * (double) m)); additive(out, a); return; } // Organ (drawbars fade in)
        case 17: { const int h[] = { 1, 3, 5, 7, 9, 11 }; for (int j = 0; j < 6; ++j) a[(std::size_t) h[j]] = (1.0 / h[j]) * (j < 2 ? 1.0 : (double) m); additive(out, a); return; } // Hollow (clarinet-ish odd)
        case 18: { const int h[] = { 1, 3, 5, 9, 13, 19 }; for (int j = 0; j < 6; ++j) a[(std::size_t) h[j]] = (1.0 / (j + 1)) * (1.0 - 0.5 * (double) m * j / 6.0); additive(out, a); return; } // Bell (sparse inharmonic-ish)
        case 19: a[1] = 1.0; for (int k = 2; k <= kMaxH; ++k) a[(std::size_t) k] = (1.0 - (double) m) / k; additive(out, a); return;     // Saw → Sine
        case 20: a[1] = 1.0; for (int k = 3; k <= kMaxH; k += 2) a[(std::size_t) k] = (1.0 - (double) m) / k; additive(out, a); return;  // Square → Sine
        case 21: { const int top = 1 + (int) (m * 16.0); for (int k = 1; k <= top && k <= kMaxH; ++k) a[(std::size_t) k] = 1.0 / k; additive(out, a); return; } // Harmonic Stack (1→17)
        case 22: { const double f1 = 7.0 + (3.0 - 7.0) * (double) m, f2 = 12.0 + (25.0 - 12.0) * (double) m; for (int k = 1; k <= kMaxH; ++k) a[(std::size_t) k] = (gauss(k, f1, 2.0) + 0.7 * gauss(k, f2, 3.0)) * ampSaw(k) * k; additive(out, a); return; } // Vocal Ah→Ee
        case 23: { const double c = 1.0 + 30.0 * (double) m; for (int k = 1; k <= kMaxH; ++k) a[(std::size_t) k] = ampSaw(k) * (1.0 + 3.0 * gauss(k, c, 3.0)); additive(out, a); return; } // Sync Saw (sweeping emphasis)
        case 24: default: { const int sp[] = { 1, 2, 3, 5, 7, 11, 13, 17 }; for (int j = 0; j < 8; ++j) a[(std::size_t) sp[j]] = (1.0 / (j + 1)) * (j < 1 ? 1.0 : (double) m); additive(out, a); return; } // Sparse (prime harmonics)
    }
}

const char* const kNames[25] = {
    "01 Basic Shapes", "02 Sine", "03 Triangle", "04 Saw", "05 Square",
    "06 Pulse PWM", "07 FM 1-1", "08 FM 2-1", "09 FM 3-1", "10 FM 1-1.5",
    "11 Formant Low", "12 Formant Sweep", "13 Formant High", "14 Odd Harmonics", "15 Even Harmonics",
    "16 Harmonic Peak", "17 Organ", "18 Hollow", "19 Bell", "20 Saw to Sine",
    "21 Square to Sine", "22 Harmonic Stack", "23 Vocal Ah-Ee", "24 Sync Saw", "25 Sparse"
};
} // namespace

int main(int argc, char* argv[])
{
    juce::File outDir = (argc > 1)
        ? juce::File(juce::String::fromUTF8(argv[1]))
        : juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
              .getChildFile("TDP").getChildFile("muTant").getChildFile("Wavetables");
    outDir.createDirectory();

    gSin.resize((std::size_t) kSize);
    for (int j = 0; j < kSize; ++j) gSin[(std::size_t) j] = (float) std::sin(kTwoPi * j / kSize);

    juce::WavAudioFormat wav;
    juce::AudioBuffer<float> table(1, kFrames * kSize);

    for (int t = 0; t < 25; ++t)
    {
        for (int f = 0; f < kFrames; ++f)
        {
            const float m = (kFrames > 1) ? (float) f / (float) (kFrames - 1) : 0.0f;
            synthFrame(t, m, table.getWritePointer(0) + (juce::int64) f * kSize);
        }

        const juce::File outFile = outDir.getChildFile(juce::String(kNames[t]) + ".wav");
        outFile.deleteFile();
        auto fileStream = std::make_unique<juce::FileOutputStream>(outFile);
        if (! fileStream->openedOk()) { std::fprintf(stderr, "cannot open %s\n", outFile.getFullPathName().toRawUTF8()); continue; }
        // createWriterFor takes ownership of the stream on success (nulls the unique_ptr).
        std::unique_ptr<juce::OutputStream> stream = std::move(fileStream);
        auto writer = wav.createWriterFor(stream, juce::AudioFormatWriterOptions{}
                                                      .withSampleRate(44100.0)
                                                      .withNumChannels(1)
                                                      .withBitsPerSample(16));
        if (writer != nullptr) writer->writeFromAudioSampleBuffer(table, 0, table.getNumSamples());
        std::printf("wrote %s (%d frames x %d)\n", outFile.getFileName().toRawUTF8(), kFrames, kSize);
    }

    std::printf("Done: 25 wavetables -> %s\n", outDir.getFullPathName().toRawUTF8());
    return 0;
}
