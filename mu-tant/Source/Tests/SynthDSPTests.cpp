// mu-tant first-stab DSP tests: scale-quantised pitch + the wavetable oscillator.
// Pure mu-tant code (no mu-core link) — verifies the synth core objectively:
// the pitch math lands on the right notes, and the oscillator actually produces
// a non-silent tone at the frequency it was set to.

#include <juce_core/juce_core.h>
#include <vector>
#include <cmath>
#include "Audio/Scales.h"
#include "Audio/WavetableBank.h"
#include "Audio/WavetableOscillator.h"

class SynthDSPTest : public juce::UnitTest
{
public:
    SynthDSPTest() : juce::UnitTest ("mu-tant synth DSP", "mu-tant") {}

    void runTest() override
    {
        using namespace mu_tant;

        beginTest ("scale-quantised pitch lands on the right MIDI notes");
        {
            // Major (idx 0), root C (0), octave 4. Degree 0 = C, 1 = D (+2), 7 = octave up.
            expectWithinAbsoluteError (toneToMidi (0, 0, 4, 0.0f, 0.0f), 48.0f, 1e-4f, "C4 degree 0");
            expectWithinAbsoluteError (toneToMidi (0, 0, 4, 1.0f, 0.0f), 50.0f, 1e-4f, "degree 1 = D (+2 st)");
            expectWithinAbsoluteError (toneToMidi (0, 0, 4, 7.0f, 0.0f), 60.0f, 1e-4f, "degree 7 = octave up");
            expectWithinAbsoluteError (toneToMidi (0, 0, 4, 0.0f, 100.0f), 49.0f, 1e-4f, "+100 cents = +1 st");
            // A fractional tone glides between degrees (halfway C->D = +1 st).
            expectWithinAbsoluteError (toneToMidi (0, 0, 4, 0.5f, 0.0f), 49.0f, 1e-4f, "half-degree glide");
        }

        beginTest ("midiToFreq matches equal temperament");
        {
            expectWithinAbsoluteError (midiToFreq (69.0f), 440.0f, 1e-2f, "A4 = 440 Hz");
            expectWithinAbsoluteError (midiToFreq (57.0f), 220.0f, 1e-2f, "A3 = 220 Hz");
        }

        beginTest ("wavetable oscillator produces a non-silent tone at the set frequency");
        {
            constexpr double sr      = 48000.0;
            constexpr float  testHz  = 440.0f;
            constexpr int    nSamp   = (int) sr;   // 1 second

            WavetableBank bank;
            bank.generateBuiltIn();                // frame 0 = sine

            WavetableOscillator osc;
            osc.setBank (&bank);
            osc.prepare (sr);
            osc.setFrequency (testHz);
            osc.setPosition (0.0f);                // pure sine frame

            std::vector<float> buf ((size_t) nSamp);
            for (int i = 0; i < nSamp; ++i) buf[(size_t) i] = osc.render();

            // non-silent
            double sumSq = 0.0;
            for (float s : buf) sumSq += (double) s * s;
            const double rms = std::sqrt (sumSq / (double) nSamp);
            expect (rms > 0.01, "oscillator output must be audibly non-silent (rms=" + juce::String (rms) + ")");

            // frequency via zero-crossing count (2 crossings per cycle for a sine)
            int crossings = 0;
            for (int i = 1; i < nSamp; ++i)
                if ((buf[(size_t) i - 1] <= 0.0f) != (buf[(size_t) i] <= 0.0f))
                    ++crossings;
            const float estHz = (float) crossings / 2.0f / (float) (nSamp / sr);
            expectWithinAbsoluteError (estHz, testHz, 2.0f,
                "zero-crossing pitch estimate should match the set frequency (got " + juce::String (estHz) + " Hz)");
        }

        beginTest ("oscillator position scan changes the timbre (sine vs saw frame)");
        {
            WavetableBank bank;
            bank.generateBuiltIn();
            // The last frame (saw) should have more harmonic energy than frame 0 (sine):
            // compare a crude brightness proxy — sum of |first difference| over one cycle.
            auto roughness = [&bank](float pos)
            {
                WavetableOscillator o; o.setBank (&bank); o.prepare (48000.0);
                o.setFrequency (100.0f); o.setPosition (pos);
                float prev = o.render(), acc = 0.0f;
                for (int i = 0; i < 480; ++i) { float s = o.render(); acc += std::abs (s - prev); prev = s; }
                return acc;
            };
            expect (roughness (1.0f) > roughness (0.0f),
                "saw frame (pos 1) should be brighter/rougher than the sine frame (pos 0)");
        }
    }
};

static SynthDSPTest synthDSPTest;
