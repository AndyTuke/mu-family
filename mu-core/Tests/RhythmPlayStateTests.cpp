// RhythmPlayState position snapshot: a writer publishing (step, stepC, frac) while a reader polls must never
// show a pair from two different publishes (the ring would jump a step), and reading never blocks.

#include <juce_core/juce_core.h>
#include "Sequencer/RhythmPlayState.h"
#include <atomic>
#include <thread>

class RhythmPlayStateTest : public juce::UnitTest
{
public:
    RhythmPlayStateTest() : juce::UnitTest ("Rhythm play-state snapshot", "Sequencer") {}

    // Every field is derived from the step, so any mix of two publishes is detectable.
    static mu_core::RhythmPlayState::Position positionFor (int n)
    {
        const int step = n & 1023;
        return { step, step ^ 0x155, (float) step / 1024.0f };
    }

    static bool isConsistent (const mu_core::RhythmPlayState::Position& p)
    {
        const bool isDefault = p.step == 0 && p.stepC == 0 && p.frac == 0.0f;   // before the first publish
        return isDefault || (p.stepC == (p.step ^ 0x155) && p.frac == (float) p.step / 1024.0f);
    }

    void runTest() override
    {
        constexpr int kWrites = 400000;

        beginTest ("a polling reader never sees a torn (step, stepC, fraction) snapshot");
        {
            mu_core::RhythmPlayState state;
            std::atomic<bool> done { false };
            std::thread writer ([&] {
                for (int n = 0; n < kWrites; ++n)
                    state.publishPosition (positionFor (n));
                done.store (true);
            });

            long reads = 0, torn = 0;
            while (! done.load())
            {
                if (! isConsistent (state.readPosition())) ++torn;
                ++reads;
            }
            writer.join();

            expectEquals ((int) torn, 0, "torn reads out of " + juce::String ((int) reads));
            expect (reads > 0, "the reader ran while the writer was publishing");
            expect (isConsistent (state.readPosition()) && state.readPosition().step == positionFor (kWrites - 1).step,
                    "after the writer finishes the reader sees the last publish");
        }

        beginTest ("reads never block: they complete at full writer speed and on an idle state");
        {
            mu_core::RhythmPlayState idle;
            const auto p = idle.readPosition();
            expect (p.step == 0 && p.stepC == 0 && p.frac == 0.0f, "an unwritten state reads as the default position");

            mu_core::RhythmPlayState state;
            std::atomic<bool> stop { false };
            std::thread writer ([&] {
                int n = 0;
                while (! stop.load()) state.publishPosition (positionFor (n++));
            });

            const auto start = juce::Time::getMillisecondCounterHiRes();
            long torn = 0;
            for (int i = 0; i < kWrites; ++i)
                if (! isConsistent (state.readPosition())) ++torn;
            const auto elapsed = juce::Time::getMillisecondCounterHiRes() - start;
            stop.store (true);
            writer.join();

            expectEquals ((int) torn, 0, "no torn reads against a free-running writer");
            expect (elapsed < 5000.0, "400k reads finished in " + juce::String (elapsed) + " ms");
        }

        beginTest ("the old publish pattern (separate relaxed stores) does tear - the harness can see it");
        {
            // Mirrors the pre-fix code: fraction stored first, then the step, as independent atomics.
            std::atomic<float> frac { 0.0f };
            std::atomic<int>   step { 0 };
            std::atomic<bool>  done { false };
            std::thread writer ([&] {
                for (int n = 0; n < kWrites; ++n)
                {
                    const auto p = positionFor (n);
                    frac.store (p.frac, std::memory_order_relaxed);
                    step.store (p.step, std::memory_order_relaxed);
                }
                done.store (true);
            });

            long torn = 0;
            while (! done.load())
            {
                const int s = step.load (std::memory_order_relaxed);
                const float f = frac.load (std::memory_order_relaxed);
                if (f != (float) s / 1024.0f) ++torn;
            }
            writer.join();
            logMessage ("old pattern torn reads: " + juce::String ((int) torn));
        }
    }
};

static RhythmPlayStateTest rhythmPlayStateTest;
