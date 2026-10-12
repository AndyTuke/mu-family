// Tests on the REAL mu-Clid PluginProcessor, built headless (no editor, host or audio device):
// construction under a temp storage root, rhythm-slot edits against processBlock on a worker
// thread, and the loop-boundary hot-swap commit against processBlock.

#include "Plugin/PluginProcessor.h"
#include "Tests/ProcessorSeamHelpers.h"
#include "Audio/InsertSlotConfig.h"

#include <cstdlib>
#include <new>
#include <random>
#include <thread>
#include <vector>

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
        rhythmEditsKeepApvtsInStep();
        removeShiftsRetiredTails();
        removeReseedsShiftedEngines();
    }

    // Reads a parameter's plain value.
    static float plain(juce::AudioProcessorValueTreeState& apvts, const char* id)
    {
        auto* raw = apvts.getRawParameterValue(id);
        return raw != nullptr ? raw->load() : -1.0f;
    }

    // Removing or resetting a rhythm changes the Rhythm structs; APVTS must follow, or a later
    // APVTS-driven resync (forceSyncRhythmFromApvts) would bring the old values back.
    void rhythmEditsKeepApvtsInStep()
    {
        beginTest("remove shifts the later rhythms' parameters down with them; reset restores the defaults in APVTS");
        TempStorage storage;
        ExposedPluginProcessor p;
        p.prepareToPlay(48000.0, 256);
        p.rhythms.add(mu_clid::Rhythm());
        p.rhythms.add(mu_clid::Rhythm());
        expectEquals(p.getNumRhythms(), 3);

        setParam(p.apvts, "r0_stepsA", 9.0f);
        setParam(p.apvts, "r1_stepsA", 12.0f);
        setParam(p.apvts, "r2_stepsA", 7.0f);
        setParam(p.apvts, "ch1_lvl", 0.25f);
        expectEquals(p.getRhythm(1).genA.steps, 12);

        p.rhythms.remove(0);
        expectEquals(p.getNumRhythms(), 2);
        expectEquals(p.getRhythm(0).genA.steps, 12);
        expectEquals(p.getRhythm(1).genA.steps, 7);
        expectEquals((int) plain(p.apvts, "r0_stepsA"), 12);
        expectEquals((int) plain(p.apvts, "r1_stepsA"), 7);
        expect(std::abs(plain(p.apvts, "ch0_lvl") - 0.25f) < 1.0e-3f, "the strip's level moved down with its rhythm");

        // A resync from APVTS leaves the shifted rhythm as it is.
        p.forceSyncRhythmFromApvts(0);
        expectEquals(p.getRhythm(0).genA.steps, 12);

        const int defaultSteps = mu_clid::Rhythm().genA.steps;
        const int defaultFilter = (int) mu_clid::Rhythm().voiceParams.filterType;
        setParam(p.apvts, "r0_fltType", 3.0f);
        expectEquals((int) p.getRhythm(0).voiceParams.filterType, 3);
        p.rhythms.reset(0);
        expectEquals(p.getRhythm(0).genA.steps, defaultSteps);
        expectEquals((int) plain(p.apvts, "r0_stepsA"), defaultSteps);
        expectEquals((int) plain(p.apvts, "r0_fltType"), defaultFilter);
        p.forceSyncRhythmFromApvts(0);
        expectEquals((int) p.getRhythm(0).voiceParams.filterType, defaultFilter);
    }

    // A retired engine's tail renders into the channel of the slot that holds it, so remove() must shift
    // the retired engines (and their drained flags) down with their slot, and drop the removed slot's.
    void removeShiftsRetiredTails()
    {
        beginTest("remove shifts each slot's retired tails and drained flags down with it, and drops the removed slot's");
        TempStorage storage;
        ExposedPluginProcessor p;
        p.prepareToPlay(48000.0, 256);
        p.rhythms.add(mu_clid::Rhythm());
        p.rhythms.add(mu_clid::Rhythm());
        expectEquals(p.getNumRhythms(), 3);

        // One tail on each slot (two on slot 1), the drained flag raised on slot 2's.
        auto put = [&](int slot, int i) { p.retiredVoiceEngines[(size_t) slot][(size_t) i] = std::make_unique<VoiceEngine>(); return p.retiredVoiceEngines[(size_t) slot][(size_t) i].get(); };
        auto* t0   = put(0, 0);
        auto* t1a  = put(1, 0);
        auto* t1b  = put(1, 1);
        auto* t2   = put(2, 0);
        p.retiredReadyForCleanup[2][0].store(true);
        juce::ignoreUnused(t0);

        p.rhythms.remove(0);
        expectEquals(p.getNumRhythms(), 2);

        expect(p.retiredVoiceEngines[0][0].get() == t1a && p.retiredVoiceEngines[0][1].get() == t1b,
               "slot 1's tails now sit on slot 0 (the channel their rhythm moved to)");
        expect(p.retiredVoiceEngines[1][0].get() == t2 && p.retiredVoiceEngines[1][1] == nullptr,
               "slot 2's tail now sits on slot 1");
        expect(p.retiredVoiceEngines[2][0] == nullptr && p.retiredVoiceEngines[2][1] == nullptr,
               "the vacated top slot holds no tail");
        expect(! p.retiredReadyForCleanup[0][0].load() && p.retiredReadyForCleanup[1][0].load() && ! p.retiredReadyForCleanup[2][0].load(),
               "the drained flag moved with its engine");

        // Removing the last slot drops its tails and leaves the others alone.
        p.rhythms.remove(1);
        expect(p.retiredVoiceEngines[0][0].get() == t1a, "the lower slot keeps its tail");
        expect(p.retiredVoiceEngines[1][0] == nullptr && ! p.retiredReadyForCleanup[1][0].load(), "the removed last slot holds no tail and no flag");
    }

    // The noise inserts seed from the slot index, so a rhythm shifted down by remove() must sound as a
    // fresh engine at its new index would.
    void removeReseedsShiftedEngines()
    {
        beginTest("remove gives a shifted engine the same noise seed as a fresh engine at its new index");
        TempStorage storage;
        ExposedPluginProcessor p;
        p.prepareToPlay(48000.0, 256);
        p.rhythms.add(mu_clid::Rhythm());
        p.rhythms.add(mu_clid::Rhythm());

        // Full dither on a 2-bit crusher: with a DC input on the threshold the noise alone picks the output.
        VoiceParams vp;
        vp.insertAlgo = 4;
        auto setSlot = [&](int slot, float actual) { vp.insertParam[slot] = mu_ui::actualToNorm(actual, vp.insertAlgo, slot); };
        setSlot(0, 2.0f);
        setSlot(1, 48000.0f);
        setSlot(2, 100.0f);

        constexpr int kLen = 4096;
        auto render = [&](InsertProcessor& ip)
        {
            juce::AudioBuffer<float> buf(2, kLen);
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < kLen; ++i) buf.setSample(ch, i, 0.25f);
            ip.process(buf, kLen, 2, vp);
            return std::vector<float>(buf.getReadPointer(0), buf.getReadPointer(0) + kLen);
        };
        auto freshAt = [&](int slot)
        {
            InsertProcessor ip;
            ip.prepare(48000.0, kLen);
            ip.setSlotIndex(slot);
            return render(ip);
        };

        // The engine at slot 2 moves to slot 1 when slot 0 goes; prepare it for the block size first.
        p.voiceEngines[2]->prepareToPlay(48000.0, kLen);
        p.rhythms.remove(0);
        expectEquals(p.getNumRhythms(), 2);

        const auto shifted = render(p.voiceEngines[1]->insertProc);
        expect(shifted == freshAt(1), "the shifted engine renders like a fresh engine at its new index");
        expect(shifted != freshAt(2), "and not like one at its old index");
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
            // The id strings are built here, outside the counted regions, so only the host's
            // notification path and our listener are measured.
            const juce::String ids[]       = { "r0_stepsA", "r0_hitsA", "r0_fltType", "r0_fltCut", "r0_drvChar", "mstrLoop" };
            const juce::String bareIds[]   = { "p0", "p1", "p2", "p3", "p4", "p5" };
            const float first[] = { 9.0f, 5.0f, 3.0f, 4000.0f, 2.0f, 2.0f };
            int bareAllocs = 0, coreAllocs = 0;
            std::thread audio([&]
            {
                setParam(bare.apvts, "p5", 1.0f);
                setParam(p.apvts, ids[5], 1.0f);   // the same one-off warm-up for the real processor
                setParam(p.apvts, ids[0], 11.0f);
                alloc_count::count = 0; alloc_count::counting = true;
                for (int i = 0; i < 6; ++i) setParam(bare.apvts, bareIds[i], 10.0f + (float) i);
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
            expect(coreAllocs == bareAllocs, "our listener allocated: " + juce::String(coreAllocs) + " vs " + juce::String(bareAllocs));
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
