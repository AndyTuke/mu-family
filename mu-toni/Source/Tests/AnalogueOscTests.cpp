// mu-toni analogue oscillator smoke tests — self-contained (no mu-core link).
// Confirms the osc produces non-silent, bounded output at roughly the set pitch.

#include <juce_core/juce_core.h>
#include "Audio/AnalogueOsc.h"
#include <cmath>

using namespace mu_toni;

class AnalogueOscTest : public juce::UnitTest
{
public:
    AnalogueOscTest() : juce::UnitTest("Analogue oscillator", "mu-toni") {}

    void runTest() override
    {
        const double sr = 44100.0;

        beginTest("Each shape is non-silent and bounded");
        {
            for (int shape = 0; shape < AnalogueOsc::kNumShapes; ++shape)
            {
                AnalogueOsc o;
                o.prepare(sr);
                o.setShape(shape);
                o.setFrequency(220.0f);
                float peak = 0.0f;
                double rms = 0.0;
                const int N = 4410;
                for (int i = 0; i < N; ++i)
                {
                    const float s = o.render();
                    peak = juce::jmax(peak, std::abs(s));
                    rms += (double) s * s;
                }
                rms = std::sqrt(rms / N);
                expect(peak > 0.1f, "shape " + juce::String(shape) + " is audible");
                expect(peak < 1.6f, "shape " + juce::String(shape) + " is bounded");
                expect(rms > 0.05, "shape " + juce::String(shape) + " has energy");
            }
        }

        beginTest("Saw runs at approximately the set frequency");
        {
            AnalogueOsc o;
            o.prepare(sr);
            o.setShape(AnalogueOsc::Saw);
            const float hz = 100.0f;
            o.setFrequency(hz);
            int upward = 0;
            float prev = o.render();
            const int N = (int) sr;   // 1 second
            for (int i = 1; i < N; ++i)
            {
                const float s = o.render();
                if (prev < 0.0f && s >= 0.0f) ++upward;   // rising zero crossing
                prev = s;
            }
            // ~100 cycles/sec → ~100 rising crossings, allow slack for band-limiting.
            expect(upward >= 95 && upward <= 105,
                   "≈100 Hz, got " + juce::String(upward) + " crossings");
        }
    }
};

static AnalogueOscTest analogueOscTest;
