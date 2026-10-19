// Staged hot-swaps against the paths that apply state at once, on the REAL mu-Clid PluginProcessor
// (headless, no audio thread): the stager's take-back of a staged full preset, a failed preset load
// publishing no name, and host state restore / a stopped load dropping whatever was staged while the
// transport played. The transport state is set directly (sequencerPlaying), so every case is deterministic.

#include "Plugin/PluginProcessor.h"
#include "Plugin/HotSwapStager.h"
#include "Plugin/PresetIO.h"
#include "Tests/ProcessorSeamHelpers.h"

namespace
{
    using namespace mu_seam_test;

    // A fresh processor under a temp storage root with a per-rhythm preset and a full preset on disk,
    // both written while stopped, so tests can stage them while "playing".
    struct Fixture
    {
        TempStorage storage;
        mu_clid::PluginProcessor p;
        juce::File rhythmFile, fullFile;

        Fixture()
        {
            setParam(p.apvts, "r0_hitsA", 5.0f);
            p.saveLayerPreset(0, "cancelA");
            rhythmFile = p.getRhythmsDir().getChildFile("cancelA.muRhythm");
            fullFile = storage.root.getChildFile("cancelFull.muClid");
            p.saveFullPresetTo(fullFile);
        }

        // Stage a per-rhythm swap and/or a full preset as if loaded while the transport plays.
        void stageWhilePlaying(bool rhythm, bool full)
        {
            p.sequencerPlaying.store(true);
            if (rhythm) p.stageRhythmPreset(0, rhythmFile);
            if (full)   p.loadPreset(fullFile);
            p.sequencerPlaying.store(false);
        }
    };

    // The processor's own session, as a host would hand it back.
    juce::MemoryBlock sessionState(mu_clid::PluginProcessor& p)
    {
        juce::MemoryBlock b;
        p.getStateInformation(b);
        return b;
    }

    // A pre-session tree (no MuClidPreset root), which goes through the legacy reader.
    juce::MemoryBlock legacyState()
    {
        juce::ValueTree legacy("LegacyHostState");
        legacy.setProperty("numRhythms", 1, nullptr);
        const auto text = legacy.createXml()->toString();
        return juce::MemoryBlock(text.toRawUTF8(), text.getNumBytesAsUTF8());
    }
}

struct MuClidHotSwapCancelTests : juce::UnitTest
{
    MuClidHotSwapCancelTests() : juce::UnitTest("mu-Clid staged swaps vs apply-at-once paths", "mu-clid-processor") {}

    void runTest() override
    {
        takeBackOfStagedFullPreset();
        failedLoadPublishesNoName();
        hostRestoreCancelsStaged();
        stoppedLoadCancelsStaged();
    }

    // takeStagedFullPreset hands back the staged payload, flagged by a loop boundary or not, and a
    // later commit pass finds nothing to apply.
    void takeBackOfStagedFullPreset()
    {
        using Stager = mu_clid::HotSwapStager;

        for (int flagged = 0; flagged < 2; ++flagged)
        {
            beginTest(juce::String("takeStagedFullPreset returns a staged full preset ")
                      + (flagged ? "after a boundary flagged it" : "before any boundary")
                      + "; processSwaps then commits nothing");
            TempStorage storage;
            mu_clid::PluginProcessor p;
            Stager stager(p);

            const int hitsBefore = p.getRhythm(0).genA.hits;
            const int countBefore = p.getNumRhythms();

            Stager::PreparedFullPreset prepared;
            prepared.numRhythms = 1;
            prepared.rhythms[0].genA.hits = hitsBefore + 3;   // would be visible on rhythm 0 if it were committed
            prepared.tree = juce::ValueTree("TakeBackMarker");
            stager.stageFullPreset(std::move(prepared));
            expect(stager.hasPendingFullPreset(), "the payload is staged");

            if (flagged)
            {
                // A wrap on the master loop and on every rhythm's own loop: whichever the timing rule uses.
                const bool needsCommit = stager.checkBoundaries(1, true, 0xff);
                expect(needsCommit, "the boundary flagged the staged payload");
            }

            Stager::PreparedFullPreset back;
            expect(stager.takeStagedFullPreset(back), "the payload is taken back");
            expectEquals(back.tree.getType().toString(), juce::String("TakeBackMarker"), "and it is the payload that was staged");
            expectEquals(back.numRhythms, 1);
            expect(! stager.hasPendingFullPreset(), "nothing is staged afterwards");
            expect(! stager.takeStagedFullPreset(back), "a second take finds nothing");

            stager.processSwaps();
            expect(! stager.hasPendingFullPreset(), "processSwaps has nothing to commit");
            expectEquals(p.getNumRhythms(), countBefore, "the rhythm count is untouched");
            expectEquals(p.getRhythm(0).genA.hits, hitsBefore, "rhythm 0 is untouched");
        }
    }

    // A file that does not parse loads nothing: PresetIO says so, and the processor does not announce
    // the file's name as the current preset (a good file does).
    void failedLoadPublishesNoName()
    {
        beginTest("loadPreset on an unparseable file returns false, reports the error and publishes no preset name");
        TempStorage storage;
        mu_clid::PluginProcessor p;

        const auto garbage = storage.root.getChildFile("garbage.muClid");
        garbage.replaceWithText("this is << not xml at all");

        juce::StringArray names, errors;
        p.onPresetNameChanged = [&](const juce::String& n) { names.add(n); };
        p.onLoadError = [&](const juce::String& e) { errors.add(e); };

        const int hitsBefore = p.getRhythm(0).genA.hits;
        mu_clid::PresetIO io(p);
        expect(! io.loadPreset(garbage), "PresetIO::loadPreset returns false");
        expectEquals(errors.size(), 1, "the load error callback ran once");

        p.loadPreset(garbage);
        expectEquals(names.size(), 0, "no preset name was published");
        expectEquals(p.getRhythm(0).genA.hits, hitsBefore, "nothing was loaded");

        // Control: a readable full preset does publish its name.
        const auto good = storage.root.getChildFile("goodOne.muClid");
        expect(p.saveFullPresetTo(good), "a full preset was written");
        expect(io.loadPreset(good), "PresetIO::loadPreset returns true for a readable file");
        p.loadPreset(good);
        expectEquals(names.size(), 1, "the name is published on success");
        expectEquals(names.isEmpty() ? juce::String() : names[0], juce::String("goodOne"));
    }

    // A host restore applies at once, in either state format, so it must drop a staged per-rhythm swap
    // and a staged full preset.
    void hostRestoreCancelsStaged()
    {
        for (int legacy = 0; legacy < 2; ++legacy)
        {
            for (int what = 0; what < 2; ++what)
            {
                const bool rhythm = what == 0;
                beginTest(juce::String("setStateInformation with a ") + (legacy ? "legacy-format tree" : "MuClidPreset session")
                          + " cancels a staged " + (rhythm ? "per-rhythm swap" : "full preset"));
                Fixture f;
                f.stageWhilePlaying(rhythm, ! rhythm);
                if (rhythm) expect(f.p.hasPendingSwap(0), "a rhythm swap is staged beforehand");
                else        expect(f.p.hasPendingFullPreset(), "a full preset is staged beforehand");

                const auto state = legacy ? legacyState() : sessionState(f.p);
                f.p.setStateInformation(state.getData(), (int) state.getSize());

                expect(! f.p.hasPendingSwap(0), "no rhythm swap is pending");
                expect(! f.p.hasPendingFullPreset(), "no full preset is pending");
            }
        }
    }

    // A load while stopped applies at once and clears every earlier staged swap.
    void stoppedLoadCancelsStaged()
    {
        beginTest("loadPreset while stopped cancels a staged per-rhythm swap and a staged full preset");
        Fixture f;
        f.stageWhilePlaying(true, false);
        expect(f.p.hasPendingSwap(0), "a rhythm swap is staged beforehand");
        f.p.loadPreset(f.fullFile);
        expect(! f.p.hasPendingSwap(0), "the rhythm swap is gone");
        expect(! f.p.hasPendingFullPreset(), "and nothing is staged");

        f.stageWhilePlaying(false, true);
        expect(f.p.hasPendingFullPreset(), "a full preset is staged beforehand");
        f.p.loadPreset(f.fullFile);
        expect(! f.p.hasPendingFullPreset(), "the staged full preset is gone after a stopped load");
        expect(! f.p.hasPendingSwap(0), "and no swap is pending");
    }
};

static MuClidHotSwapCancelTests muClidHotSwapCancelTests;
