// Tests on the REAL mu-Clid PluginProcessor, built headless (no editor, host or audio device):
// construction under a temp storage root, rhythm-slot edits against processBlock on a worker
// thread, and the loop-boundary hot-swap commit against processBlock.

#include "Plugin/PluginProcessor.h"
#include "Tests/ProcessorSeamHelpers.h"

#include <cstdlib>
#include <new>
#include <random>
#include <thread>

// Allocation counter: counts operator new calls made while the calling thread has counting switched on.
namespace alloc_count {
inline thread_local bool counting = false;
inline thread_local int  count    = 0;
}

void* operator new(std::size_t size)
{
    if (alloc_count::counting) ++alloc_count::count;
    if (void* p = std::malloc(size > 0 ? size : 1)) return p;
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }

// The product class names its editor in a virtual, defined in PluginEditor.cpp, which this headless
// target does not link (see "Headless product processors" in design-plugin-family.md).
juce::AudioProcessorEditor* mu_clid::PluginProcessor::createEditor() { return nullptr; }

namespace
{
    using namespace mu_seam_test;

    // The real processor with the deferred-work drain exposed (the audio thread asks for the swap
    // commit; on a host the message-thread poll runs it).
    struct ExposedPluginProcessor : mu_clid::PluginProcessor
    {
        using mu_clid::PluginProcessor::drainDeferredWork;
    };

    // A bare processor with a few plain parameters: the cost of the host's own notification path with
    // none of our listeners attached, to subtract from what the real processor does.
    struct BareProcessor : juce::AudioProcessor
    {
        BareProcessor()
            : juce::AudioProcessor(BusesProperties()),
              apvts(*this, nullptr, juce::Identifier("Bare"), makeLayout())
        {}

        static juce::AudioProcessorValueTreeState::ParameterLayout makeLayout()
        {
            juce::AudioProcessorValueTreeState::ParameterLayout layout;
            for (int i = 0; i < 6; ++i)
                layout.add(std::make_unique<juce::AudioParameterFloat>("p" + juce::String(i), "p" + juce::String(i),
                                                                       juce::NormalisableRange<float>(0.0f, 100.0f), 0.0f));
            return layout;
        }

        const juce::String getName() const override { return "bare"; }
        void prepareToPlay(double, int) override {}
        void releaseResources() override {}
        void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override {}
        juce::AudioProcessorEditor* createEditor() override { return nullptr; }
        bool hasEditor() const override { return false; }
        bool acceptsMidi() const override { return false; }
        bool producesMidi() const override { return false; }
        double getTailLengthSeconds() const override { return 0.0; }
        int getNumPrograms() override { return 1; }
        int getCurrentProgram() override { return 0; }
        void setCurrentProgram(int) override {}
        const juce::String getProgramName(int) override { return {}; }
        void changeProgramName(int, const juce::String&) override {}
        void getStateInformation(juce::MemoryBlock&) override {}
        void setStateInformation(const void*, int) override {}

        juce::AudioProcessorValueTreeState apvts;
    };

    // A stamp of the real per-user folders, to prove the temp root kept a test out of them.
    juce::int64 realFolderStamp()
    {
        juce::int64 stamp = 0;
        const auto docs = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
                              .getChildFile("TDP").getChildFile("muClid");
        for (const auto& f : docs.findChildFiles(juce::File::findFiles, true))
            stamp += f.getLastModificationTime().toMilliseconds() + f.getSize();
        return stamp;
    }
}

struct MuClidProcessorSeamTests : juce::UnitTest
{
    MuClidProcessorSeamTests() : juce::UnitTest("mu-Clid real PluginProcessor", "mu-clid-processor") {}

    void runTest() override
    {
        constructsAndOwnsItsRhythm();
        rhythmEditsVersusProcessBlock();
        swapCommitVersusProcessBlock();
        parameterChangedFromAudioThread();
    }

    void constructsAndOwnsItsRhythm()
    {
        beginTest("The real processor constructs under a temp root with one rhythm, and renders a block");
        RealFolderWatch realFolder(realFolderStamp, "mu-Clid.exe");
        {
            TempStorage storage;
            ExposedPluginProcessor p;
            expectEquals(p.getNumRhythms(), 1);
            expect(p.getLayer(0) == &p.getRhythm(0), "getLayer(0) is rhythm 0");
            expect(p.getLayer(1) == nullptr, "no layer above the count");
            expectEquals(p.getContentDir().getFullPathName(),
                         storage.root.getChildFile("content").getChildFile("muClid").getFullPathName());

            p.prepareToPlay(48000.0, 256);
            juce::AudioBuffer<float> buf(2, 256);
            juce::MidiBuffer midi;
            p.processBlock(buf, midi);
            expect(bufferIsSane(buf), "output is finite");
            p.releaseResources();
        }
        realFolder.expectUnchanged(*this, "nothing under the real muClid content folder changed");
    }

    void rhythmEditsVersusProcessBlock()
    {
        beginTest("rhythms.add / swap / remove / reset on the message thread vs processBlock on a worker, across block sizes");
        TempStorage storage;
        ExposedPluginProcessor p;
        int count = 0;
        const int* sizes = blockSizes(count);
        for (int k = 0; k < count; ++k)
        {
            const int n = sizes[k];
            p.prepareToPlay(48000.0, n);
            RunningPlayHead host(48000.0, n);   // the sequencer runs, so the edits meet real work
            p.setPlayHead(&host);

            BlockRenderer renderer(p, n);
            renderer.start();

            std::mt19937 rng(777u + (unsigned) k);
            for (int round = 0; round < 4; ++round)
            {
                while (p.getNumRhythms() < mu_limits::kMaxLayers)
                {
                    p.rhythms.add(mu_clid::Rhythm());
                    const int live = p.getNumRhythms();
                    if (live > 1) p.rhythms.swap((int) (rng() % (unsigned) live), (int) (rng() % (unsigned) live));
                    p.rhythms.reset((int) (rng() % (unsigned) live));
                }
                expectEquals(p.getNumRhythms(), (int) mu_limits::kMaxLayers);
                while (p.getNumRhythms() > 1)
                    p.rhythms.remove((int) (rng() % (unsigned) p.getNumRhythms()));
                expectEquals(p.getNumRhythms(), 1);
            }

            expect(renderer.waitForBlocks(2), "the audio thread was not stalled by the edits");
            renderer.stop();
            p.setPlayHead(nullptr);
            expect(! renderer.sawInsaneOutput(), "block size " + juce::String(n) + ": output stayed finite and bounded");
        }
    }

    // Stages a preset while the sequencer runs; the audio thread flags it at a loop boundary and the
    // deferred-work drain commits it (suspend plus rhythmsLock) while processBlock keeps running.
    void swapCommitVersusProcessBlock()
    {
        beginTest("A staged rhythm swap commits at a loop boundary while processBlock runs on a worker, across block sizes");
        TempStorage storage;
        ExposedPluginProcessor p;

        // Two presets that differ in hit count, written while stopped.
        setParam(p.apvts, "r0_hitsA", 3.0f);
        p.saveLayerPreset(0, "swapA");
        setParam(p.apvts, "r0_hitsA", 7.0f);
        p.saveLayerPreset(0, "swapB");
        const juce::File files[2] = { p.getRhythmsDir().getChildFile("swapA.muRhythm"),
                                      p.getRhythmsDir().getChildFile("swapB.muRhythm") };
        const int expectedHits[2] = { 3, 7 };
        expect(files[0].existsAsFile() && files[1].existsAsFile(), "both presets were written under the temp root");

        // Default Timing ("on master loop") with no master loop set: each rhythm commits at its own loop wrap.

        const int sizes[] = { 64, 480, 1024 };
        for (int n : sizes)
        {
            p.prepareToPlay(48000.0, n);
            RunningPlayHead host(48000.0, n);   // a plugin-mode processor plays when its host does
            p.setPlayHead(&host);

            BlockRenderer renderer(p, n);
            renderer.start();

            // The staging path only applies while the audio thread has published "playing".
            const auto playDeadline = juce::Time::getMillisecondCounter() + 20000;
            while (! p.sequencerPlaying.load() && juce::Time::getMillisecondCounter() < playDeadline)
                juce::Thread::sleep(1);
            expect(p.sequencerPlaying.load(), "block size " + juce::String(n) + ": the sequencer reports playing");

            for (int round = 0; round < 6; ++round)
            {
                const int which = round % 2;
                p.stageRhythmPreset(0, files[which]);
                const bool staged = p.hasPendingSwap(0);

                // Commit when the audio thread has flagged the boundary; bounded wait.
                const auto deadline = juce::Time::getMillisecondCounter() + 10000;
                while (p.hasPendingSwap(0) && juce::Time::getMillisecondCounter() < deadline)
                {
                    p.drainDeferredWork();
                    juce::Thread::sleep(1);
                }
                expect(! p.hasPendingSwap(0), "block size " + juce::String(n) + " round " + juce::String(round)
                                              + ": the staged swap " + (staged ? "committed" : "was applied at once"));
                expectEquals(p.getRhythm(0).genA.hits, expectedHits[which],
                             "block size " + juce::String(n) + " round " + juce::String(round) + ": the swapped-in rhythm is live");
            }

            renderer.stop();
            p.setPlayHead(nullptr);
            expect(! renderer.sawInsaneOutput(), "block size " + juce::String(n) + ": output stayed finite and bounded");
        }
    }

    // Automation can call parameterChanged on the audio thread. It must then allocate nothing, leave
    // the Rhythm alone and queue the change; the message-thread deferred-work drain applies it.
    void parameterChangedFromAudioThread()
    {
        beginTest("parameterChanged on another thread allocates nothing; the deferred-work drain applies the queued values");
        {
            TempStorage storage;
            ExposedPluginProcessor p;
            p.prepareToPlay(48000.0, 256);
            BareProcessor bare;

            const int stepsBefore = p.getRhythm(0).genA.steps;
            const int hitsBefore  = p.getRhythm(0).genA.hits;
            const int lengthBefore = p.sequencer.getPatternLength(0);
            expectEquals(stepsBefore, 16);

            // The set calls differ from the current values so each one fires the listener. Warm-up
            // first, uncounted, so one-off statics in the host's notification path do not count.
            const char* ids[]   = { "r0_stepsA", "r0_hitsA", "r0_fltType", "r0_fltCut", "r0_drvChar", "mstrLoop" };
            const float first[] = { 9.0f, 5.0f, 3.0f, 4000.0f, 2.0f, 2.0f };
            int bareAllocs = 0, coreAllocs = 0;
            std::thread audio([&]
            {
                setParam(bare.apvts, "p5", 1.0f);
                alloc_count::count = 0; alloc_count::counting = true;
                for (int i = 0; i < 6; ++i) setParam(bare.apvts, "p" + juce::String(i), 10.0f + (float) i);
                alloc_count::counting = false; bareAllocs = alloc_count::count;

                alloc_count::count = 0; alloc_count::counting = true;
                for (int i = 0; i < 6; ++i) setParam(p.apvts, ids[i], first[i]);
                alloc_count::counting = false; coreAllocs = alloc_count::count;
            });
            audio.join();

            // The listener only queued the rhythm values; the loop length is a lone atomic and is live.
            expectEquals(p.sequencer.getMasterLoopSteps(), 32);
            expectEquals(p.getRhythm(0).genA.steps, stepsBefore);
            expectEquals(p.getRhythm(0).genA.hits, hitsBefore);
            expectEquals(p.sequencer.getPatternLength(0), lengthBefore);

            expect(p.drainDeferredWork(), "the listener asked for the deferred-work pass");
            expectEquals(p.getRhythm(0).genA.steps, 9);
            expectEquals(p.getRhythm(0).genA.hits, 5);
            expectEquals((int) p.getRhythm(0).voiceParams.filterType, 3);
            RunningPlayHead host(48000.0, 256);   // the audio thread only picks up a rebuilt pattern while playing
            p.setPlayHead(&host);
            juce::AudioBuffer<float> block(2, 256);
            juce::MidiBuffer midi;
            for (int i = 0; i < 4; ++i) p.processBlock(block, midi);
            p.setPlayHead(nullptr);
            expect(p.sequencer.getPatternLength(0) != lengthBefore, "the pattern was rebuilt on the message thread");

            // The host's own notification work is the same with or without our listener; ours adds none.
            expect(coreAllocs <= bareAllocs, "our listener allocated: " + juce::String(coreAllocs) + " vs " + juce::String(bareAllocs));
        }

        beginTest("Queued values coalesce: the last value set from another thread is the one applied");
        {
            TempStorage storage;
            ExposedPluginProcessor p;
            p.prepareToPlay(48000.0, 256);

            std::atomic<bool> done { false };
            std::thread audio([&]
            {
                for (int i = 0; i < 4000; ++i)
                {
                    setParam(p.apvts, "r0_hitsA", (float) (1 + i % 4));
                    setParam(p.apvts, "r0_stepsA", (float) (8 + i % 8));
                }
                setParam(p.apvts, "r0_hitsA", 3.0f);
                setParam(p.apvts, "r0_stepsA", 11.0f);
                done.store(true);
            });
            while (! done.load())
            {
                p.drainDeferredWork();   // the message thread's poll, racing the sets
                juce::Thread::sleep(1);
            }
            audio.join();
            p.drainDeferredWork();
            expectEquals(p.getRhythm(0).genA.steps, 11);
            expectEquals(p.getRhythm(0).genA.hits, 3);
            expect(p.getRhythm(0).genA.hits <= p.getRhythm(0).genA.steps, "the rhythm stayed consistent");
        }
    }
};

static MuClidProcessorSeamTests muClidProcessorSeamTests;
