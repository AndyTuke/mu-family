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
#include <functional>
#include <random>
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

    // Sets an environment variable for its lifetime (empty value = unset) and restores the previous value.
    // A processor reads MU_DETERMINISTIC_NOISE in its constructor, so scope this around the construction.
    struct ScopedEnvVar
    {
        ScopedEnvVar(const char* varName, const char* value) : name(varName)
        {
            const auto old = juce::SystemStats::getEnvironmentVariable(name, {});
            hadOld = old.isNotEmpty();
            oldValue = old;
            set(value);
        }
        ~ScopedEnvVar() { set(hadOld ? oldValue.toRawUTF8() : ""); }

    private:
        void set(const char* value)
        {
           #if JUCE_WINDOWS
            _putenv_s(name, value);
           #else
            if (value[0] == 0) unsetenv(name); else setenv(name, value, 1);
           #endif
        }
        const char* name;
        bool hadOld = false;
        juce::String oldValue;
    };

    // True when a process with this executable name is running (Windows only; elsewhere false). Used to
    // tell a write by a real instance of the product from one the test made.
    inline bool processIsRunning(const juce::String& exeName)
    {
       #if JUCE_WINDOWS
        juce::ChildProcess listing;
        if (! listing.start("tasklist /FI \"IMAGENAME eq " + exeName + "\" /NH")) return false;
        return listing.readAllProcessOutput().containsIgnoreCase(exeName);
       #else
        juce::ignoreUnused(exeName);
        return false;
       #endif
    }

    // Proves a test kept out of the real per-user folders: a stamp of them taken before and after.
    // A real instance of the product (or a DAW hosting it) writes to the same folders, so a change is
    // only held against the test when nothing else explains it: no real instance is running, the
    // folder was quiet before the test, and it has stopped changing since.
    class RealFolderWatch
    {
    public:
        // `stamp` sums the modification times and sizes of the files to watch; `standaloneExe` is the
        // product's standalone executable name (e.g. "mu-Tant.exe").
        RealFolderWatch(std::function<juce::int64()> stampFn, juce::String standaloneExe)
            : stamp(std::move(stampFn)), exe(std::move(standaloneExe))
        {
            before = stamp();
            juce::Thread::sleep(200);
            quietBefore = (stamp() == before);
            otherRunning = processIsRunning(exe);
        }

        void expectUnchanged(juce::UnitTest& t, const juce::String& message)
        {
            const auto after = stamp();
            if (after == before) { t.expect(true); return; }

            juce::Thread::sleep(200);
            const bool stillMoving = (stamp() != after);
            if (otherRunning || ! quietBefore || stillMoving)
                t.logMessage("real folder changed during the test, but " + exe + " or a host is writing there "
                             "(running: " + juce::String(otherRunning ? "yes" : "no") + ", quiet before: "
                             + juce::String(quietBefore ? "yes" : "no") + ", still moving: "
                             + juce::String(stillMoving ? "yes" : "no") + "); not held against the test");
            else
                t.expect(false, message);
        }

    private:
        std::function<juce::int64()> stamp;
        juce::String exe;
        juce::int64 before = 0;
        bool quietBefore = true, otherRunning = false;
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

    // A host playhead that is always playing and moves on by one block each time it is read (the
    // processor reads it once per block), so a plugin-mode processor sees a running host transport.
    struct RunningPlayHead : juce::AudioPlayHead
    {
        RunningPlayHead(double sampleRateHz, int blockSize, double bpm = 120.0)
            : ppqPerBlock(bpm / 60.0 * (double) blockSize / sampleRateHz) {}

        juce::Optional<PositionInfo> getPosition() const override
        {
            PositionInfo info;
            info.setIsPlaying(true);
            info.setBpm(120.0);
            info.setPpqPosition(ppq);
            info.setTimeSignature(TimeSignature { 4, 4 });
            ppq += ppqPerBlock;
            return info;
        }

        double ppqPerBlock;
        mutable double ppq = 0.0;
    };

    // The block sizes every threaded test sweeps: tiny, odd, typical and large.
    inline const int* blockSizes(int& count)
    {
        static const int sizes[] = { 1, 37, 64, 480, 512, 1024 };
        count = (int) (sizeof(sizes) / sizeof(sizes[0]));
        return sizes;
    }

    // The structural-edit stress shared by every product with editable layers: the test thread grows
    // the processor to kMaxLayers with random swaps, shrinks it back removing random layers, over
    // several rounds, while a worker calls processBlock, at each block size. `openLayer(i)` makes
    // layer i sound while the transport is stopped (a product's gate or trigger bypass); `checkLayers`
    // asserts the product's storage invariants whenever the edits are quiet. Starts from one layer.
    inline void runStructuralEditStress(juce::UnitTest& t, ProcessorBase& p,
                                        const std::function<void(int)>& openLayer,
                                        const std::function<void(const juce::String&)>& checkLayers)
    {
        t.beginTest("addLayer / removeLayer / swapLayers on the message thread vs processBlock on a worker, across block sizes");
        int count = 0;
        const int* sizes = blockSizes(count);
        for (int k = 0; k < count; ++k)
        {
            const int n = sizes[k];
            p.prepareToPlay(48000.0, n);
            openLayer(0);

            BlockRenderer renderer(p, n);
            renderer.start();
            t.expect(renderer.waitForAudible(), "the worker is rendering before the edits start");

            std::mt19937 rng(12345u + (unsigned) k);
            const int rounds = 6;
            for (int r = 0; r < rounds; ++r)
            {
                // Grow to the maximum, shuffling as we go.
                while (p.getNumLayers() < mu_limits::kMaxLayers)
                {
                    const int idx = p.addLayer();
                    t.expect(idx >= 0, "addLayer succeeds below the limit");
                    if (idx < 0) break;
                    openLayer(idx);
                    const int live = p.getNumLayers();
                    if (live > 1) p.swapLayers((int) (rng() % (unsigned) live), (int) (rng() % (unsigned) live));
                }
                checkLayers("grown, block size " + juce::String(n));

                // Shrink back to one, removing from random positions.
                while (p.getNumLayers() > 1)
                {
                    const bool removed = p.removeLayer((int) (rng() % (unsigned) p.getNumLayers()));
                    t.expect(removed, "removeLayer succeeds above one layer");
                    if (! removed) break;
                    const int live = p.getNumLayers();
                    if (live > 1) p.swapLayers(0, live - 1);
                }
                checkLayers("shrunk, block size " + juce::String(n));
                t.expect(! p.removeLayer(0), "the last layer cannot be removed");
            }

            t.expect(renderer.waitForBlocks(2) && renderer.audibleBlocksRendered() > 0, "the audio thread was not stalled by the edits");
            renderer.stop();
            t.expect(! renderer.sawInsaneOutput(), "block size " + juce::String(n) + ": output stayed finite and bounded");
            t.expect(! p.layerEditInFlight.load(), "no edit left the in-flight flag set");
        }
    }
}
