// Tests on the REAL mu-Clid PluginProcessor, built headless (no editor, host or audio device):
// construction under a temp storage root, rhythm-slot edits against processBlock on a worker
// thread, and the loop-boundary hot-swap commit against processBlock.

#include "Plugin/PluginProcessor.h"
#include "Tests/ProcessorSeamHelpers.h"

#include <random>

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
};

static MuClidProcessorSeamTests muClidProcessorSeamTests;
