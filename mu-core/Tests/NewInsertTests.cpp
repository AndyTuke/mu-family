#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include "Audio/AlgorithmNames.h"
#include "Audio/InsertProcessor.h"
#include <cmath>
#include <random>

// The Chorus, Flanger, Phaser, Pitch Delay and Tremolo inserts (algorithms 17..21): finite and bounded output at every corner
// of their four knobs, an untouched signal when the mix (or depth) is zero, the pitch shift lands on the right note, and the
// tremolo follows the tempo when synced.
struct NewInsertTests : juce::UnitTest
{
    NewInsertTests() : juce::UnitTest("New inserts: chorus, flanger, phaser, pitch delay, tremolo", "mu-core") {}

    static constexpr double kRate = 48000.0;
    static constexpr int kBlock = 256;
    enum { Chorus = 17, Flanger = 18, Phaser = 19, PitchDelay = 20, Tremolo = 21 };

    static VoiceParams make(int algo, float s0, float s1, float s2, float s3)
    {
        VoiceParams p;
        p.insertAlgo = algo;
        p.insertParam[0] = s0; p.insertParam[1] = s1; p.insertParam[2] = s2; p.insertParam[3] = s3;
        return p;
    }

    // Runs `seconds` of the input through a fresh processor and returns the left channel.
    template <class Gen>
    static std::vector<float> run(const VoiceParams& p, double seconds, Gen input, double bpm = 120.0)
    {
        InsertProcessor proc;
        proc.prepare(kRate, kBlock);
        proc.setTempo(bpm);
        const int total = (int) (seconds * kRate);
        std::vector<float> out;
        out.reserve((size_t) total);
        juce::AudioBuffer<float> buf(2, kBlock);
        for (int pos = 0; pos < total; pos += kBlock)
        {
            const int n = juce::jmin(kBlock, total - pos);
            for (int i = 0; i < n; ++i)
                for (int c = 0; c < 2; ++c) buf.setSample(c, i, input(pos + i));
            proc.process(buf, n, 2, p);
            for (int i = 0; i < n; ++i) out.push_back(buf.getSample(0, i));
        }
        return out;
    }

    void runTest() override
    {
        beginTest("the slot tables give each new insert four labelled knobs, and a Mix knob where the effect is wet/dry");
        {
            expectEquals(mu_ui::kInsertAlgoCount, 22);
            for (int a = Chorus; a <= Tremolo; ++a)
            {
                for (int s = 0; s < mu_ui::kInsertSlotCount; ++s)
                    expect(mu_ui::kInsertAlgoSlots[a][s].label != nullptr, juce::String(mu_audio::kInsertAlgorithmNames[a]) + " slot " + juce::String(s) + " has a label");
                if (a != Tremolo)   // Tremolo's Depth is its mix
                {
                    bool hasMix = false;
                    for (int s = 0; s < mu_ui::kInsertSlotCount; ++s)
                        hasMix = hasMix || juce::String(mu_ui::kInsertAlgoSlots[a][s].label) == "Mix";
                    expect(hasMix, juce::String(mu_audio::kInsertAlgorithmNames[a]) + " has a Mix knob");
                }
            }
            expect(mu_ui::insertHasSync(PitchDelay) && mu_ui::insertHasSync(Tremolo), "Pitch Delay and Tremolo can sync to tempo");
        }

        beginTest("every corner of the four knobs gives finite, bounded output, with and without Sync");
        {
            std::mt19937 rng(7);
            std::uniform_real_distribution<float> noise(-0.8f, 0.8f);
            const float corners[2] = { 0.0f, 1.0f };
            for (int a = Chorus; a <= Tremolo; ++a)
                for (int mask = 0; mask < 16; ++mask)
                    for (int sync = 0; sync < 2; ++sync)
                    {
                        auto p = make(a, corners[mask & 1], corners[(mask >> 1) & 1], corners[(mask >> 2) & 1], corners[(mask >> 3) & 1]);
                        p.insertSync = sync != 0;
                        auto out = run(p, 1.5, [&](int i) { return i % 4000 < 3000 ? noise(rng) : 0.5f * std::sin(0.05f * (float) i); });
                        bool ok = true; float peak = 0.0f;
                        for (float v : out) { ok = ok && std::isfinite(v); peak = std::max(peak, std::abs(v)); }
                        expect(ok && peak < 8.0f, juce::String(mu_audio::kInsertAlgorithmNames[a]) + " corner " + juce::String(mask)
                               + (sync ? " synced" : "") + " peak " + juce::String(peak));
                    }
        }

        beginTest("Pitch Delay across the whole Pitch range stays inside its buffer and finite (a read position that rounded up to the size once indexed one past the end)");
        {
            std::mt19937 rng(11);
            std::uniform_real_distribution<float> noise(-0.6f, 0.6f);
            for (int step = 0; step <= 32; ++step)
            {
                auto p = make(PitchDelay, (float) step / 32.0f, 0.1f, 0.6f, 1.0f);
                auto out = run(p, 1.0, [&](int) { return noise(rng); });
                bool ok = true;
                for (float v : out) ok = ok && std::isfinite(v);
                expect(ok, "Pitch Delay stays finite at Pitch step " + juce::String(step));
            }
        }

        beginTest("with Mix at zero the Chorus, Flanger, Phaser and Pitch Delay leave the signal untouched; Tremolo at zero depth too");
        {
            auto sine = [](int i) { return 0.4f * std::sin(0.07f * (float) i); };
            for (int a : { (int) Chorus, (int) Flanger, (int) Phaser, (int) PitchDelay })
            {
                auto p = make(a, 0.5f, 0.5f, 0.5f, 0.0f);   // slot 3 is Mix for all four (see the table)
                auto out = run(p, 0.5, sine);
                float worst = 0.0f;
                for (size_t i = 0; i < out.size(); ++i) worst = std::max(worst, std::abs(out[i] - sine((int) i)));
                expect(worst < 1.0e-4f, juce::String(mu_audio::kInsertAlgorithmNames[a]) + " dry at Mix 0, worst error " + juce::String(worst));
            }
            auto p = make(Tremolo, 0.5f, 0.0f, 0.0f, 0.0f);
            auto out = run(p, 0.5, sine);
            float worst = 0.0f;
            for (size_t i = 0; i < out.size(); ++i) worst = std::max(worst, std::abs(out[i] - sine((int) i)));
            expect(worst < 1.0e-5f, "Tremolo dry at depth 0, worst error " + juce::String(worst));
        }

        beginTest("Pitch Delay at +12 semitones moves a 440 Hz tone to about 880 Hz");
        {
            // Pitch +12 is the top of slot 0; Time the shortest; no feedback; Mix full.
            auto p = make(PitchDelay, 1.0f, 0.0f, 0.0f, 1.0f);
            auto out = run(p, 1.5, [](int i) { return 0.5f * std::sin(juce::MathConstants<float>::twoPi * 440.0f * (float) i / (float) kRate); });
            int crossings = 0;
            const size_t from = (size_t) (0.75 * kRate);
            for (size_t i = from + 1; i < out.size(); ++i)
                if (out[i - 1] <= 0.0f && out[i] > 0.0f) ++crossings;
            const double seconds = (double) (out.size() - from) / kRate;
            const double hz = (double) crossings / seconds;
            expect(hz > 780.0 && hz < 980.0, "measured " + juce::String(hz, 1) + " Hz");
        }

        beginTest("Tremolo with Sync follows the tempo: four sixteenths at 120 bpm is a 2 Hz pulse");
        {
            // Rate slot 0 holds the step: step 4 sits in band 3 of 16, its middle is (3 + 0.5) / 16.
            auto p = make(Tremolo, mu_ui::insertSyncStepToNorm(4), 1.0f, 0.0f, 0.0f);
            p.insertSync = true;
            auto out = run(p, 4.0, [](int) { return 0.5f; }, 120.0);
            int rises = 0;
            for (size_t i = 1; i < out.size(); ++i)
                if (out[i - 1] < 0.25f && out[i] >= 0.25f) ++rises;
            expect(rises >= 7 && rises <= 9, "pulses in 4 s: " + juce::String(rises));
        }

        beginTest("a square Tremolo at full depth reaches silence between pulses");
        {
            auto p = make(Tremolo, 0.5f, 1.0f, 1.0f, 0.0f);   // Shape = Square (the top of its 0..2 range)
            auto out = run(p, 2.0, [](int) { return 0.5f; });
            float lowest = 1.0f;
            for (size_t i = (size_t) (0.5 * kRate); i < out.size(); ++i) lowest = std::min(lowest, std::abs(out[i]));
            expect(lowest < 0.01f, "lowest level " + juce::String(lowest));
        }
    }
};

static NewInsertTests newInsertTests;
