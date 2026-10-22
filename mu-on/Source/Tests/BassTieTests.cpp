// The Bass voice's gate, release and tied steps, and the step row's tie flag: a gated note releases after its hold and
// an ungated one is held; a tied step carries the note on instead of retriggering; a tie needs two steps that are on,
// ends when either goes off, and survives a save.

#include "Audio/BassEngine.h"
#include "Sequencer/StepRow.h"
#include <juce_core/juce_core.h>

namespace
{
    using Buf = juce::AudioBuffer<float>;
    constexpr double kSr = 48000.0;

    float rmsOver(mu_on::BassEngine& bass, int from, int to, int block = 256)
    {
        // Renders [from, to) samples in blocks and returns the rms of the last block, for a level after a given time.
        Buf out(2, block);
        float rms = 0.0f;
        for (int at = from; at < to; at += block)
        {
            out.clear();
            bass.render(out, block);
            double sum = 0.0;
            for (int i = 0; i < block; ++i) sum += (double) out.getSample(0, i) * out.getSample(0, i);
            rms = (float) std::sqrt(sum / block);
        }
        return rms;
    }

    class BassTieTest : public juce::UnitTest
    {
    public:
        BassTieTest() : juce::UnitTest("mu-on Bass gate, release and tied steps", "Engine") {}

        void runTest() override
        {
            beginTest("A gated note releases after its hold; an ungated one is held");
            {
                mu_on::BassEngine gated, held;
                for (auto* b : { &gated, &held })
                {
                    b->prepare(kSr, 256);
                    b->setParams(55.0f, 0, 0.0f, 600.0f, 0.2f, 0.0f, 100.0f, 1.0f, 200.0f, 1.0f, 0.0f, 50.0f, 0.0f);
                }
                gated.trigger(1.0f, 0, false, 4800);   // held 100 ms, then 50 ms release
                held.trigger(1.0f, 0, false, 0);
                const float gatedNow = rmsOver(gated, 0, 2400), heldNow = rmsOver(held, 0, 2400);
                expect(gatedNow > 0.2f && heldNow > 0.2f, "both sound while the gate is open");
                const float gatedLater = rmsOver(gated, 2400, 24000), heldLater = rmsOver(held, 2400, 24000);
                expect(gatedLater < 0.001f, "the gated note has released to silence");
                expect(heldLater > 0.2f, "the ungated note is still held");
            }

            beginTest("A tied step carries the note on instead of retriggering it");
            {
                mu_on::BassEngine tied, retriggered;
                for (auto* b : { &tied, &retriggered })
                {
                    b->prepare(kSr, 256);
                    b->setParams(55.0f, 0, 0.0f, 600.0f, 0.2f, 0.0f, 100.0f, 150.0f, 200.0f, 1.0f, 0.0f, 300.0f, 0.0f);   // a slow attack
                    b->trigger(1.0f, 0, false, 0);
                    rmsOver(*b, 0, 3840);   // 80 ms into the attack
                }
                tied.trigger(1.0f, 0, true, 0);
                retriggered.trigger(1.0f, 0, false, 0);
                const float a = rmsOver(tied, 0, 256), b = rmsOver(retriggered, 0, 256);
                expect(a > 0.1f, "the tied note keeps its level");
                expect(b < a * 0.5f, "a retriggered note starts its attack over");
            }

            beginTest("A tied step glides to the new pitch");
            {
                mu_on::BassEngine bass;
                bass.prepare(kSr, 256);
                bass.setParams(55.0f, 0, 0.0f, 600.0f, 0.2f, 0.0f, 100.0f, 1.0f, 200.0f, 1.0f, 0.0f, 300.0f, 100.0f);
                bass.trigger(1.0f, 0, false, 0);
                rmsOver(bass, 0, 4800);
                bass.setParams(110.0f, 0, 0.0f, 600.0f, 0.2f, 0.0f, 100.0f, 1.0f, 200.0f, 1.0f, 0.0f, 300.0f, 100.0f);
                bass.trigger(1.0f, 0, true, 0);
                Buf out(2, 256);
                out.clear();
                bass.render(out, 256);
                int crossings = 0;
                for (int i = 1; i < 256; ++i) if (out.getSample(0, i - 1) <= 0.0f && out.getSample(0, i) > 0.0f) ++crossings;
                const double hz = crossings * kSr / 256.0;
                expect(hz < 100.0, "the first 5 ms of the slide is still below the new pitch (" + juce::String(hz, 1) + " Hz)");
            }

            beginTest("A step's octave and note transpose the Bass from the Tune, and are saved");
            {
                auto hzAt = [](float semitones)
                {
                    mu_on::BassEngine bass;
                    bass.prepare(kSr, 256);
                    bass.setParams(55.0f, 0, 0.0f, 600.0f, 0.2f, 0.0f, 100.0f, 1.0f, 200.0f, 1.0f, 0.0f, 300.0f, 0.0f);
                    bass.trigger(1.0f, 0, false, 0, semitones);
                    Buf out(2, 24000);
                    out.clear();
                    for (int at = 0; at < 24000; at += 256) { Buf v(out.getArrayOfWritePointers(), 2, at, juce::jmin(256, 24000 - at)); bass.render(v, v.getNumSamples()); }
                    int crossings = 0;
                    for (int i = 1; i < 24000; ++i) if (out.getSample(0, i - 1) <= 0.0f && out.getSample(0, i) > 0.0f) ++crossings;
                    return crossings * kSr / 24000.0;
                };
                expectWithinAbsoluteError(hzAt(12.0f) / hzAt(0.0f), 2.0, 0.1);
                expectWithinAbsoluteError(hzAt(-12.0f) / hzAt(0.0f), 0.5, 0.05);

                mu_on::StepRow row;
                row.setStepOctave(3, 1); row.setStepNote(3, -5); row.setStepOctave(4, 9);
                expectEquals(row.stepSemitones(3), 7);
                expectEquals(row.stepOctave(4), 2);
                mu_on::StepRow loaded;
                loaded.deserialise(row.serialise(0));
                expectEquals(loaded.stepSemitones(3), 7);
            }

            beginTest("A tie needs two steps that are on, ends when either goes off, and is saved");
            {
                mu_on::StepRow row;
                row.setOn(4, true); row.setOn(5, true); row.setTied(5, true);
                expect(row.tiedFrom(5), "tied");
                expect(! row.tiedFrom(4), "the first step of a tie is not tied from anything");
                const auto saved = row.serialise(0);
                mu_on::StepRow loaded;
                loaded.deserialise(saved);
                expect(loaded.tiedFrom(5), "the tie survives a save and load");
                row.setOn(4, false);
                expect(! row.tiedFrom(5), "a tie ends when the step before goes off");
                row.setOn(4, true);
                expect(! row.tiedFrom(5), "and does not come back when it is switched on again");
                row.setTied(1, true);
                expect(! row.tiedFrom(1), "a tie onto an off step does nothing");
            }
        }
    };

    static BassTieTest bassTieTest;
}
