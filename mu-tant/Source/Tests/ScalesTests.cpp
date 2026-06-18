// mu-tant scale-quantisation tests - Scales.h is pure + header-only, so this
// pins the tone->MIDI->frequency maths that the synth pitch path depends on.

#include <juce_core/juce_core.h>
#include "Audio/Scales.h"

class ScalesTest : public juce::UnitTest
{
public:
    ScalesTest() : juce::UnitTest("mu-tant scales", "mu-tant") {}

    void runTest() override
    {
        using namespace mu_tant;

        beginTest("clampScaleIndex bounds to [0, kNumScales)");
        {
            expectEquals(clampScaleIndex(-5), 0);
            expectEquals(clampScaleIndex(0), 0);
            expectEquals(clampScaleIndex(kNumScales - 1), kNumScales - 1);
            expectEquals(clampScaleIndex(99999), kNumScales - 1);
        }

        beginTest("every scale's degree 0 is the root (offset 0) + count in 1..12");
        {
            for (int s = 0; s < kNumScales; ++s)
            {
                expectEquals(kScales[(size_t) s].offsets[0], 0, juce::String(kScales[(size_t) s].name));
                expect(kScales[(size_t) s].count >= 1 && kScales[(size_t) s].count <= 12);
            }
        }

        beginTest("scaleSemitone wraps octaves past the degree count");
        {
            const auto& maj = kScales[0];   // Major, 7 degrees: 0 2 4 5 7 9 11
            expectWithinAbsoluteError(scaleSemitone(maj, 0),  0.0f,  0.001f);
            expectWithinAbsoluteError(scaleSemitone(maj, 6), 11.0f,  0.001f);
            expectWithinAbsoluteError(scaleSemitone(maj, 7), 12.0f,  0.001f);   // degree 7 = octave up
            expectWithinAbsoluteError(scaleSemitone(maj, 8), 14.0f,  0.001f);   // degree 8 = 2nd, oct up
        }

        beginTest("scaleSemitone floor-divides negative tones");
        {
            const auto& maj = kScales[0];
            expectWithinAbsoluteError(scaleSemitone(maj, -1), -1.0f,  0.001f);  // degree 6 an octave down
            expectWithinAbsoluteError(scaleSemitone(maj, -7), -12.0f, 0.001f);
        }

        beginTest("toneToMidi: integer tone lands exactly on the scale degree");
        {
            expectWithinAbsoluteError(toneToMidi(0, 0, 0, 0.0f, 0.0f), 0.0f, 0.001f);
            expectWithinAbsoluteError(toneToMidi(0, 0, 0, 1.0f, 0.0f), 2.0f, 0.001f);  // major 2nd
            expectWithinAbsoluteError(toneToMidi(0, 0, 0, 2.0f, 0.0f), 4.0f, 0.001f);  // major 3rd
        }

        beginTest("toneToMidi: fractional tone glides monotonically between degrees");
        {
            const float a   = toneToMidi(0, 0, 0, 0.0f, 0.0f);
            const float mid = toneToMidi(0, 0, 0, 0.5f, 0.0f);
            const float b   = toneToMidi(0, 0, 0, 1.0f, 0.0f);
            expect(mid > a && mid < b, "midpoint sits between adjacent degrees");
        }

        beginTest("toneToMidi: root / octave / fine offsets apply additively");
        {
            expectWithinAbsoluteError(toneToMidi(0, 5, 0, 0.0f, 0.0f),  5.0f, 0.001f);  // root +5
            expectWithinAbsoluteError(toneToMidi(0, 0, 2, 0.0f, 0.0f), 24.0f, 0.001f);  // +2 octaves
            expectWithinAbsoluteError(toneToMidi(0, 0, 0, 0.0f, 50.0f), 0.5f, 0.001f);  // +50 cents
        }

        beginTest("midiToFreq: A4 = 440 Hz, octave doubling");
        {
            expectWithinAbsoluteError(midiToFreq(69.0f), 440.0f, 0.01f);
            expectWithinAbsoluteError(midiToFreq(81.0f), 880.0f, 0.01f);
            expectWithinAbsoluteError(midiToFreq(57.0f), 220.0f, 0.01f);
        }

        beginTest("chromatic scale: degree N == N semitones");
        {
            const auto& chr = kScales[11];
            expectEquals(chr.count, 12);
            for (int t = 0; t < 12; ++t)
                expectWithinAbsoluteError(scaleSemitone(chr, t), (float) t, 0.001f);
        }
    }
};

static ScalesTest scalesTest;
