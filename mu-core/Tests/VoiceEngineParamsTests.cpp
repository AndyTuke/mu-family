// VoiceEngine::setParams (message thread) against applyPendingParams (audio thread, via process):
// the audio thread must never wait for the pending-params lock, and a block that finds it held keeps
// the old params and applies the new ones on the next block.

#include <juce_audio_basics/juce_audio_basics.h>
#include "Audio/VoiceEngine.h"

#include <atomic>
#include <chrono>
#include <thread>

namespace
{
    // State the test seam hook reads: the engine to process from inside the held lock, and what it saw.
    struct HookState
    {
        VoiceEngine*        engine = nullptr;
        juce::AudioBuffer<float>* block = nullptr;
        float               appliedDuringHold = -1.0f;
        std::atomic<int>    holdMs { 0 };
        bool                processInHook = false;
    };
    HookState hookState;

    // Runs on the thread inside setParams, with the pending-params lock held.
    void holdHook()
    {
        if (hookState.processInHook && hookState.engine != nullptr)
        {
            // The same thread plays "audio" here: a blocking lock would deadlock; a try-lock returns.
            hookState.engine->process(*hookState.block, hookState.block->getNumSamples());
            hookState.appliedDuringHold = hookState.engine->getAppliedParams().filterCutoff;
        }
        if (const int ms = hookState.holdMs.load()) juce::Thread::sleep(ms);
    }

    struct HookInstaller
    {
        HookInstaller()  { VoiceEngine::setParamsHoldHook.store(&holdHook); }
        ~HookInstaller() { VoiceEngine::setParamsHoldHook.store(nullptr); hookState.engine = nullptr; hookState.block = nullptr; hookState.processInHook = false; hookState.holdMs.store(0); }
    };
}

struct VoiceEngineParamsTests : juce::UnitTest
{
    VoiceEngineParamsTests() : juce::UnitTest("VoiceEngine pending params never block the audio thread", "mu-core") {}

    void runTest() override
    {
        constexpr int kBlock = 256;

        beginTest("a block that finds the lock held keeps the old params, then applies the new ones next block");
        {
            VoiceEngine ve;
            ve.prepareToPlay(48000.0, kBlock);
            juce::AudioBuffer<float> block(2, kBlock);
            block.clear();

            VoiceParams first;  first.filterCutoff = 1000.0f;
            ve.setParams(first);
            ve.process(block, kBlock);
            expectEquals(ve.getAppliedParams().filterCutoff, 1000.0f);

            HookInstaller hook;
            hookState.engine = &ve;
            hookState.block = &block;
            hookState.processInHook = true;

            VoiceParams second; second.filterCutoff = 2500.0f;
            ve.setParams(second);   // the hook runs a block inside the held lock

            expectEquals(hookState.appliedDuringHold, 1000.0f);   // the try failed; old params kept; no deadlock
            hookState.processInHook = false;
            ve.process(block, kBlock);
            expectEquals(ve.getAppliedParams().filterCutoff, 2500.0f);   // the flag was re-raised, nothing lost
        }

        beginTest("stress: a setParams loop holding the lock for 30 ms never stalls the audio thread, and the last value lands");
        {
            VoiceEngine ve;
            ve.prepareToPlay(48000.0, kBlock);
            juce::AudioBuffer<float> block(2, kBlock);
            block.clear();

            HookInstaller hook;
            hookState.holdMs.store(30);

            std::atomic<bool> done { false };
            constexpr int kSets = 25;
            std::thread messageThread([&]
            {
                for (int i = 1; i <= kSets; ++i)
                {
                    VoiceParams p; p.filterCutoff = 1000.0f + (float) i;
                    ve.setParams(p);
                }
                done.store(true);
            });

            double maxMs = 0.0;
            int blocks = 0;
            while (! done.load())
            {
                const auto t0 = std::chrono::steady_clock::now();
                ve.process(block, kBlock);
                const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
                maxMs = juce::jmax(maxMs, ms);
                ++blocks;
                juce::Thread::sleep(1);   // block-rate pacing so the loop does not hog the lock's cache line
            }
            messageThread.join();
            ve.process(block, kBlock);

            logMessage("audio blocks: " + juce::String(blocks) + ", max block time: " + juce::String(maxMs, 3) + " ms");
            expect(blocks > 100, "the audio loop ran throughout (" + juce::String(blocks) + " blocks)");
            expect(maxMs < 15.0, "an audio block waited for the lock: " + juce::String(maxMs, 3) + " ms");
            expectEquals(ve.getAppliedParams().filterCutoff, 1000.0f + (float) kSets);
        }
    }
};

static VoiceEngineParamsTests voiceEngineParamsTests;
