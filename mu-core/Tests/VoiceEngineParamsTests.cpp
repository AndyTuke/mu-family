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
            // A loop blocked on the lock would manage about one block per set (kSets); the 1 ms pacing sleep is
            // ~15 ms at the default Windows timer resolution (about 50 blocks) and ~1 ms when another app raised it.
            expect(blocks > 2 * kSets, "the audio loop ran throughout (" + juce::String(blocks) + " blocks)");
            expect(maxMs < 15.0, "an audio block waited for the lock: " + juce::String(maxMs, 3) + " ms");
            expectEquals(ve.getAppliedParams().filterCutoff, 1000.0f + (float) kSets);
        }

        beginTest("setParams on other threads racing process() with no hold: the last value written always lands");
        {
            // Unhindered writers against a free-running audio loop, many rounds, so the try-lock miss and the
            // flag re-raise happen at random points. After the writers stop one more block must apply the last value.
            for (int round = 0; round < 40; ++round)
            {
                VoiceEngine ve;
                ve.prepareToPlay(48000.0, kBlock);
                juce::AudioBuffer<float> block(2, kBlock);
                block.clear();

                constexpr int kWrites = 2000;
                std::atomic<bool> go { false };
                std::atomic<int> writersDone { 0 };
                auto writer = [&](float base)
                {
                    while (! go.load()) std::this_thread::yield();
                    for (int i = 1; i <= kWrites; ++i)
                    {
                        VoiceParams p; p.filterCutoff = base + (float) i;
                        ve.setParams(p);
                    }
                    writersDone.fetch_add(1);
                };

                // One writer in even rounds, two in odd rounds (then either writer's final value may win).
                const int numWriters = 1 + (round & 1);
                std::thread w1(writer, 1000.0f);
                std::thread w2;
                if (numWriters == 2) w2 = std::thread(writer, 5000.0f);
                go.store(true);
                while (writersDone.load() < numWriters)
                    ve.process(block, kBlock);
                w1.join();
                if (w2.joinable()) w2.join();
                ve.process(block, kBlock);

                const float got = ve.getAppliedParams().filterCutoff;
                const float lastA = 1000.0f + (float) kWrites, lastB = 5000.0f + (float) kWrites;
                const bool ok = numWriters == 1 ? got == lastA : (got == lastA || got == lastB);
                if (! ok) { expect(false, "round " + juce::String(round) + ": applied " + juce::String(got) + " is not a final written value"); break; }
            }
            expect(true);
        }
    }
};

static VoiceEngineParamsTests voiceEngineParamsTests;

//==============================================================================
// A block whose sample-buffer try-lock misses (a sample load on the message thread holds it) is silent, but it
// keeps time: the three envelopes and a retired engine's drain countdown advance by the block, as a rendered
// block's do. So a retired engine drains after the same number of process() calls, contended or not.
namespace
{
    // A 64-frame click written to a temporary WAV, so the engine has a sample that ends inside the first block.
    juce::File writeClick(const juce::File& dir)
    {
        auto f = dir.getChildFile("click.wav");
        f.deleteFile();
        juce::AudioBuffer<float> click(1, 64);
        for (int i = 0; i < 64; ++i) click.setSample(0, i, 0.5f);
        {
            juce::WavAudioFormat wav;
            std::unique_ptr<juce::OutputStream> out = f.createOutputStream();
            if (out == nullptr) return {};
            auto writer = wav.createWriterFor(out, juce::AudioFormatWriterOptions{}.withSampleRate(48000.0)
                                                                                   .withNumChannels(1)
                                                                                   .withBitsPerSample(16));
            if (writer == nullptr) return {};
            writer->writeFromAudioSampleBuffer(click, 0, 64);
        }   // the writer is destroyed here, which flushes the file
        return f;
    }

    // process() calls from the retire until the engine reports drained; every other call contended when asked.
    int callsToDrain(const juce::File& click, bool contendEveryOther)
    {
        constexpr int kBlock = 256;
        VoiceEngine ve;
        ve.prepareToPlay(48000.0, kBlock);
        ve.loadFile(click);
        VoiceParams p;
        p.ampEnvRel = 4.0f;   // longer than the 2 s drain budget, so the drain waits on the release: contended blocks must move it on too
        ve.setParams(p);
        juce::AudioBuffer<float> block(2, kBlock);
        ve.trigger(false, false);
        block.clear();
        ve.process(block, kBlock);   // the click plays out in this block
        ve.markRetired();
        int calls = 0;
        while (! ve.isFullyDrained() && calls < 100000)
        {
            VoiceEngine::forceBufferContention.store(contendEveryOther && (calls % 2) == 0);
            block.clear();
            ve.process(block, kBlock);
            VoiceEngine::forceBufferContention.store(false);
            ++calls;
        }
        return calls;
    }
}

struct VoiceEngineContendedBlockTests : juce::UnitTest
{
    VoiceEngineContendedBlockTests() : juce::UnitTest("VoiceEngine contended block keeps time", "mu-core") {}

    void runTest() override
    {
        beginTest("a retired engine drains after the same number of process() calls when half of them are contended");
        const juce::File dir = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("mu-core-voice-contention");
        dir.createDirectory();
        const auto click = writeClick(dir);
        const int plain = callsToDrain(click, false);
        const int contended = callsToDrain(click, true);
        logMessage("drained after " + juce::String(plain) + " calls, and " + juce::String(contended) + " with every other call contended");
        expect(plain > 2 && plain < 100000, "the engine drains");
        expectEquals(contended, plain);
        dir.deleteRecursively();
    }
};

static VoiceEngineContendedBlockTests voiceEngineContendedBlockTests;
