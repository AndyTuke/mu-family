#pragma once

// Helpers for unit tests that build a real product PluginProcessor headless (the seam in
// docs/design-plugin-family.md "Headless product processors"): a temp storage root, parameter
// setters, and a worker thread that calls processBlock while the test thread edits the processor.

#include "Plugin/ProcessorBase.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <thread>

namespace mu_seam_test
{
    // A unique temp folder that is also the ScopedStorageRoot, so a processor built while it is
    // alive reads and writes only under it. Declare it before the processor.
    struct TempStorage
    {
        TempStorage()
            : root(juce::File::getSpecialLocation(juce::File::tempDirectory)
                       .getChildFile("mu-seam-test-" + juce::String::toHexString(juce::Random().nextInt64()))),
              scope(root)
        {
            root.createDirectory();
        }
        ~TempStorage() { root.deleteRecursively(); }

        juce::File root;
        ProcessorBase::ScopedStorageRoot scope;
    };

    // Sets a parameter to a plain (un-normalised) value through the host-facing path.
    inline void setParam(juce::AudioProcessorValueTreeState& apvts, const juce::String& id, float plain)
    {
        if (auto* p = apvts.getParameter(id)) p->setValueNotifyingHost(p->convertTo0to1(plain));
    }

    inline float getParam(juce::AudioProcessorValueTreeState& apvts, const juce::String& id)
    {
        auto* p = apvts.getParameter(id);
        return p != nullptr ? p->convertFrom0to1(p->getValue()) : 0.0f;
    }

    // True when every sample is finite and within a sane bound.
    inline bool bufferIsSane(const juce::AudioBuffer<float>& b, float bound = 100.0f)
    {
        for (int c = 0; c < b.getNumChannels(); ++c)
            for (int i = 0; i < b.getNumSamples(); ++i)
            {
                const float s = b.getSample(c, i);
                if (! std::isfinite(s) || std::abs(s) > bound) return false;
            }
        return true;
    }

    // Calls processBlock in a loop on a worker thread until stop() is called, counting blocks and
    // flagging any non-finite or out-of-range output. stop() waits a bounded time for the worker
    // to finish; a worker that never returns means a deadlock in the code under test, so the
    // process exits rather than hang the test run.
    class BlockRenderer
    {
    public:
        BlockRenderer(juce::AudioProcessor& proc, int blockSize, int numChannels = 2)
            : processor(proc), size(blockSize), channels(numChannels) {}
        ~BlockRenderer() { stop(); }

        void start()
        {
            worker = std::thread([this]
            {
                juce::AudioBuffer<float> buf(channels, size);
                juce::MidiBuffer midi;
                while (! stopFlag.load(std::memory_order_acquire))
                {
                    buf.clear();
                    midi.clear();
                    processor.processBlock(buf, midi);
                    if (! bufferIsSane(buf)) insane.store(true, std::memory_order_relaxed);
                    if (buf.getMagnitude(0, size) > 1.0e-4f) audible.fetch_add(1, std::memory_order_relaxed);
                    blocks.fetch_add(1, std::memory_order_relaxed);
                }
                done.store(true, std::memory_order_release);
            });
        }

        void stop(double timeoutSeconds = 60.0)
        {
            if (! worker.joinable()) return;
            stopFlag.store(true, std::memory_order_release);
            const auto deadline = std::chrono::steady_clock::now()
                                  + std::chrono::milliseconds((int) (timeoutSeconds * 1000.0));
            while (! done.load(std::memory_order_acquire))
            {
                if (std::chrono::steady_clock::now() > deadline)
                {
                    std::fprintf(stderr, "BlockRenderer: processBlock did not return within %.0f s (deadlock?)\n",
                                 timeoutSeconds);
                    std::fflush(stderr);
                    std::_Exit(3);
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            worker.join();
        }

        // Blocks the worker has rendered so far (a stall shows as no growth).
        int  blocksRendered() const { return blocks.load(std::memory_order_relaxed); }
        bool sawInsaneOutput() const { return insane.load(std::memory_order_relaxed); }
        // Blocks that actually sounded (a skipped block is silent), so a worker that always loses its lock shows.
        int  audibleBlocksRendered() const { return audible.load(std::memory_order_relaxed); }

        // Waits (bounded) until the worker has rendered an audible block, i.e. it is really running.
        bool waitForAudible(double timeoutSeconds = 30.0) const
        {
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds((int) (timeoutSeconds * 1000.0));
            while (audibleBlocksRendered() < 1)
            {
                if (std::chrono::steady_clock::now() > deadline) return false;
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            return true;
        }

        // Waits (bounded) until the worker has rendered at least `n` more blocks than now.
        bool waitForBlocks(int n, double timeoutSeconds = 30.0) const
        {
            const int target = blocksRendered() + n;
            const auto deadline = std::chrono::steady_clock::now()
                                  + std::chrono::milliseconds((int) (timeoutSeconds * 1000.0));
            while (blocksRendered() < target)
            {
                if (std::chrono::steady_clock::now() > deadline) return false;
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            return true;
        }

    private:
        juce::AudioProcessor& processor;
        const int size, channels;
        std::thread worker;
        std::atomic<bool> stopFlag { false }, done { false }, insane { false };
        std::atomic<int>  blocks { 0 }, audible { 0 };
    };

    // The block sizes every threaded test sweeps: tiny, odd, typical and large.
    inline const int* blockSizes(int& count)
    {
        static const int sizes[] = { 1, 37, 64, 480, 512, 1024 };
        count = (int) (sizeof(sizes) / sizeof(sizes[0]));
        return sizes;
    }
}
