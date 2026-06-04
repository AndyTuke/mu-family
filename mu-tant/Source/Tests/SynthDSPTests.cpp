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
            bank.loadFactoryBank();                // table 0 "Basic Shapes", frame 0 = sine

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
            bank.loadFactoryBank();
            // The last frame (square) should have more harmonic energy than frame 0 (sine):
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
                "square frame (pos 1) should be brighter/rougher than the sine frame (pos 0)");
        }

        beginTest ("factory bank loads the full named table set");
        {
            WavetableBank bank; bank.loadFactoryBank();
            expectEquals (bank.numTables(), WavetableBank::factoryTableNames().size(),
                          "loadFactoryBank should produce one table per factory name");
            for (int t = 0; t < bank.numTables(); ++t)
                expect (bank.numFrames (t) > 0, "table " + juce::String (t) + " has frames");
        }

        beginTest ("selecting a different wavetable changes the output");
        {
            WavetableBank bank; bank.loadFactoryBank();
            auto energy = [&bank] (int table)
            {
                WavetableOscillator o; o.setBank (&bank); o.prepare (48000.0);
                o.setTable (table); o.setFrequency (110.0f); o.setPosition (1.0f);
                double acc = 0.0; for (int i = 0; i < 1024; ++i) acc += std::abs (o.render());
                return acc;
            };
            expect (std::abs (energy (0) - energy (3)) > 1.0e-3,
                    "two different factory tables should not produce identical output");
        }

        beginTest ("mip-mapping keeps high notes finite and bounded");
        {
            WavetableBank bank; bank.loadFactoryBank();
            WavetableOscillator o; o.setBank (&bank); o.prepare (48000.0);
            o.setTable (1); o.setPosition (1.0f); o.setFrequency (8000.0f);   // very high pitch
            float peak = 0.0f; bool finite = true;
            for (int i = 0; i < 4096; ++i) { const float s = o.render(); finite = finite && std::isfinite (s); peak = std::max (peak, std::abs (s)); }
            expect (finite, "high-pitch render must stay finite");
            expect (peak < 4.0f, "high-pitch render must stay bounded (peak=" + juce::String (peak) + ")");
        }

        beginTest ("WAV loader slices a 2048-frame mono table");
        {
            constexpr int frameSize = WavetableBank::kFrameSize, frames = 4;
            juce::AudioBuffer<float> ab (1, frameSize * frames);
            for (int f = 0; f < frames; ++f)
                for (int i = 0; i < frameSize; ++i)
                    ab.setSample (0, f * frameSize + i,
                                  std::sin (juce::MathConstants<float>::twoPi * (float) i / (float) frameSize));
            juce::MemoryBlock mb;
            {
                juce::WavAudioFormat wav;
                auto* mos = new juce::MemoryOutputStream (mb, false);   // writer takes ownership
                JUCE_BEGIN_IGNORE_WARNINGS_MSVC (4996)
                std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (mos, 48000.0, 1, 16, {}, 0));
                JUCE_END_IGNORE_WARNINGS_MSVC
                if (w != nullptr) w->writeFromAudioSampleBuffer (ab, 0, ab.getNumSamples());
            }
            WavetableBank bank;
            const int idx = bank.addFromWav (mb.getData(), mb.getSize(), "test");
            expect (idx >= 0, "addFromWav should succeed on a valid mono WAV");
            expectEquals (bank.numFrames (idx), frames, "should slice into the correct frame count");
        }
    }
};

static SynthDSPTest synthDSPTest;
