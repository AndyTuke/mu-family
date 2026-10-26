// The Chord engine: which notes a step's chord plays (root degree, chord, inversion, spread in a key and scale), the
// engine's gate and release, and the step row's chord data and its save.

#include "Audio/ChordEngine.h"
#include "Audio/ChordVoicing.h"
#include "Sequencer/StepRow.h"
#include <juce_core/juce_core.h>

namespace
{
    using namespace mu_on;
    constexpr double kSr = 48000.0;

    ChordHit hit(int chord, int inv = 0, int spread = 0, int degree = 0, int octave = 0) { return { chord, inv, spread, degree, octave }; }

    class ChordTest : public juce::UnitTest
    {
    public:
        ChordTest() : juce::UnitTest("mu-on Chord voicing, engine and step data", "Engine") {}

        void expectNotes(const ChordNotes& n, std::initializer_list<int> want, const juce::String& what)
        {
            bool same = n.count == (int) want.size();
            int i = 0;
            for (int w : want) same = same && i < 6 && n.midi[(size_t) i++] == w;
            juce::String got;
            for (int k = 0; k < n.count; ++k) got += juce::String(n.midi[(size_t) k]) + " ";
            expect(same, what + " (got " + got + ")");
        }

        void runTest() override
        {
            const int cMajor = 0, minor = 1, chromatic = 11;
            beginTest("The notes of a chord on a scale degree");
            {
                expectNotes(voiceChord(0, cMajor, hit(0)), { 48, 52, 55, 59 }, "C major, degree 1, Scale 7th: Cmaj7");
                expectNotes(voiceChord(0, cMajor, hit(0, 0, 0, 1)), { 50, 53, 57, 60 }, "C major, degree 2, Scale 7th: Dm7");
                expectNotes(voiceChord(9, minor, hit(0, 0, 0, 2)), { 60, 64, 67, 71 }, "A minor, degree 3, Scale 7th: Cmaj7");
                expectNotes(voiceChord(0, chromatic, hit(0, 0, 0, 3)), { 51, 54, 58, 61 }, "chromatic: Scale 7th is a minor 7th on the chosen note");
                expectNotes(voiceChord(0, cMajor, hit(1, 0, 0, 0, 1)), { 60, 64, 67 }, "an octave up");
                expectNotes(voiceChord(0, cMajor, hit(6)), { 48, 51, 55, 58 }, "Min7 is fixed whatever the key");
            }

            beginTest("Inversion lifts the lowest tone, Spread opens the voicing");
            {
                expectNotes(voiceChord(0, cMajor, hit(6, 1)), { 51, 55, 58, 60 }, "first inversion");
                expectNotes(voiceChord(0, cMajor, hit(6, 2)), { 55, 58, 60, 63 }, "second inversion");
                expectNotes(voiceChord(0, cMajor, hit(1, 0, 2)), { 48, 64, 79 }, "spread 2 on a triad: an octave between every tone");
                expectNotes(voiceChord(0, cMajor, hit(1, 0, 1)), { 48, 52, 67 }, "spread 1: only the top tone lifts");
                expectEquals(voiceChord(0, cMajor, hit(11)).count, 6, "an eleventh has six tones");
            }

            beginTest("The root's name follows the key and scale");
            {
                expectEquals(chordRootName(9, minor, 2), juce::String("C"));
                expectEquals(chordRootName(2, cMajor, 0), juce::String("D"));
                expectEquals(chordRootName(0, chromatic, 1), juce::String("C#"));
            }

            beginTest("The engine sounds a chord for its gate, then releases");
            {
                ChordEngine eng;
                eng.prepare(kSr, 256);
                eng.setParams(0, 8000.0f, 0.1f, 0.0f, 0.0f, 100.0f, 1.0f, 200.0f, 1.0f, 50.0f, 0.5f, 0.0f, 250.0f, 0, chromatic);
                eng.trigger(1.0f, 0, hit(6), 4800);   // held 100 ms
                juce::AudioBuffer<float> out(2, 256);
                auto rms = [&]
                {
                    out.clear(); eng.render(out, 256);
                    double sum = 0.0;
                    for (int i = 0; i < 256; ++i) sum += (double) out.getSample(0, i) * out.getSample(0, i);
                    return (float) std::sqrt(sum / 256.0);
                };
                float early = 0.0f;
                for (int b = 0; b < 8; ++b) early = rms();
                expect(early > 0.05f, "the chord sounds while held (" + juce::String(early, 3) + ")");
                float late = 1.0f;
                for (int b = 0; b < 120; ++b) late = rms();
                expect(late < 0.001f, "and is silent after its release");
            }

            beginTest("A step keeps its chord data and saves only what is not default");
            {
                StepRow row;
                expectEquals(row.stepChord(3), kDefaultChord);
                expect(! row.serialise(0).hasProperty("chord"), "defaults are not written");
                row.setStepChord(3, 8); row.setStepInversion(3, 2); row.setStepSpread(3, 3); row.setStepChord(4, 99);
                expectEquals(row.stepChord(4), kNumChords - 1, "clamped");
                StepRow loaded;
                loaded.deserialise(row.serialise(0));
                expectEquals(loaded.stepChord(3), 8);
                expectEquals(loaded.stepInversion(3), 2);
                expectEquals(loaded.stepSpread(3), 3);
                expectEquals(loaded.stepChord(0), kDefaultChord);
                row.setOn(3, true); row.setOn(3, false);
                expectEquals(row.stepChord(3), 8, "a deleted step keeps its chord");
            }
        }
    };

    static ChordTest chordTest;
}
