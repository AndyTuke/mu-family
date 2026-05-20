// Insert algorithm DSP smoke tests.
//
// Verify two regression-prone properties:
//   1. KarplusStrongInsert output level responds to insertOutput (fixed in #489).
//   2. KarplusStrongInsert and VocoderInsert process() run without crash or NaN.
//
// These are not golden-master tests — exact DSP output is not asserted.
// The goals are: (a) no crash, (b) no NaN/Inf in the output buffer, (c) the
// output gain knob demonstrably affects output level.

#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <cmath>
#include "../Audio/FX/Insert/KarplusStrongInsert.h"
#include "../Audio/FX/Insert/VocoderInsert.h"
#include "../Audio/VoiceParams.h"

static float rmsOf(const juce::AudioBuffer<float>& buf, int ns)
{
    float sum = 0.0f;
    const float* data = buf.getReadPointer(0);
    for (int i = 0; i < ns; ++i) sum += data[i] * data[i];
    return std::sqrt(sum / (float)juce::jmax(1, ns));
}

static bool hasNaN(const juce::AudioBuffer<float>& buf, int nCh, int ns)
{
    for (int ch = 0; ch < nCh; ++ch)
    {
        const float* data = buf.getReadPointer(ch);
        for (int i = 0; i < ns; ++i)
            if (!std::isfinite(data[i])) return true;
    }
    return false;
}

class InsertDSPSmokeTest : public juce::UnitTest
{
public:
    InsertDSPSmokeTest() : juce::UnitTest ("Insert DSP smoke", "DSP") {}

    void runTest() override
    {
        constexpr double kSR = 48000.0;
        constexpr int    kNs = 512;

        // ── KarplusStrongInsert ───────────────────────────────────────────────
        beginTest ("Karplus: no crash, no NaN");
        {
            KarplusStrongInsert k;
            k.prepare(kSR, kNs);

            juce::AudioBuffer<float> buf(2, kNs);
            buf.clear();
            // Impulsive excitation on first sample
            buf.setSample(0, 0, 1.0f);
            buf.setSample(1, 0, 1.0f);

            VoiceParams p;
            p.insertDrive  = 0.0f;   // note C
            p.insertBits   = 1.0f;   // octave 1
            p.insertDither = 80.0f;  // feedback 80%
            p.insertTone   = 5000.0f;
            p.insertOutput = 0.0f;

            float gr = 0.0f;
            k.process(buf, kNs, 2, p, gr);
            expect (!hasNaN(buf, 2, kNs), "Karplus produced NaN/Inf");
        }

        beginTest ("Karplus: -60 dB output is significantly quieter than 0 dB output");
        {
            // Run two identical processes, differing only in insertOutput.
            // Due to the self-sustaining feedback, we get a non-zero output both times.
            auto runKarplus = [&](float outputDb) -> float
            {
                KarplusStrongInsert k;
                k.prepare(kSR, kNs);
                juce::AudioBuffer<float> buf(1, kNs);
                buf.clear();
                buf.setSample(0, 0, 0.5f);   // excite with 0.5

                VoiceParams p;
                p.insertDrive  = 0.0f;
                p.insertBits   = 2.0f;
                p.insertDither = 90.0f;
                p.insertTone   = 10000.0f;
                p.insertOutput = outputDb;

                float gr = 0.0f;
                k.process(buf, kNs, 1, p, gr);
                return rmsOf(buf, kNs);
            };

            const float rms0dB  = runKarplus(0.0f);
            const float rms60dB = runKarplus(-60.0f);

            // -60 dB ≈ amplitude factor 0.001; the 0 dB version should be
            // at least 100× louder in RMS.
            expect (rms0dB > 1e-6f, "Karplus at 0 dB produced no output — feedback may be broken");
            expect (rms0dB > rms60dB * 50.0f,
                    "Karplus -60 dB should be >> quieter than 0 dB (0dB RMS="
                    + juce::String(rms0dB) + ", -60dB RMS=" + juce::String(rms60dB) + ")");
        }

        // ── VocoderInsert (mono) ─────────────────────────────────────────────
        beginTest ("Vocoder mono: no crash, no NaN on impulse");
        {
            VocoderInsert v(false);   // mono
            v.prepare(kSR, kNs);

            juce::AudioBuffer<float> buf(2, kNs);
            buf.clear();
            buf.setSample(0, 0, 1.0f);
            buf.setSample(1, 0, 1.0f);

            VoiceParams p;
            p.insertBits   = 3.0f;   // note D (index 1→2 → but insertBits maps to note idx)
            p.insertDither = 0.0f;   // octave 0
            p.insertOutput = -20.0f;

            float gr = 0.0f;
            v.process(buf, kNs, 2, p, gr);
            expect (!hasNaN(buf, 2, kNs), "Vocoder produced NaN/Inf");
        }

        // ── VocoderInsert (stereo) ────────────────────────────────────────────
        beginTest ("VocoderSt: no crash, no NaN on impulse");
        {
            VocoderInsert v(true);   // stereo
            v.prepare(kSR, kNs);

            juce::AudioBuffer<float> buf(2, kNs);
            buf.clear();
            buf.setSample(0, 0, 1.0f);
            buf.setSample(1, 0, 1.0f);

            VoiceParams p;
            p.insertBits   = 3.0f;
            p.insertDither = 0.0f;
            p.insertOutput = -20.0f;

            float gr = 0.0f;
            v.process(buf, kNs, 2, p, gr);
            expect (!hasNaN(buf, 2, kNs), "VocoderSt produced NaN/Inf");
        }
    }
};

static InsertDSPSmokeTest insertDSPSmokeTest;
