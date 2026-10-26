// Layer presets in a mu-Tronic slot (docs/mu-tronic/design.md 2.1.11 point 8), one implementation in ProcessorBase for every
// type: a saved file round-trips through a slot of its type, files written by the real mu-Toni and mu-On load, an Empty
// slot takes the file's type, a slot of another type refuses and changes nothing, the four .muTrack engines land only
// in their own slot, and a load while playing waits for the slot's wrap whatever the block size.

#include "Plugin/PluginProcessor.h"
#include "Sequencer/Track.h"             // mu-On's layer library: the layer a Track slot holds
#include "Tests/GoldenRender.h"          // mu-core: MockHost
#include "Tests/ProcessorSeamHelpers.h"

#include <cmath>

namespace
{
    using mu_core::LayerTypeId;

    struct PresetProcessor : mu_tronic::PluginProcessor
    {
        using mu_tronic::PluginProcessor::drainDeferredWork;
        using mu_tronic::PluginProcessor::typedLayerPresetDir;
        using mu_tronic::PluginProcessor::typedLayerPresetExtension;
    };

    float getPlain(juce::AudioProcessorValueTreeState& apvts, const juce::String& id)
    {
        auto* p = apvts.getParameter(id);
        return p->convertFrom0to1(p->getValue());
    }

    mu_on::Track& trackIn(ProcessorBase& p, int slot) { return static_cast<mu_on::Track&>(*p.getLayer(slot)); }

    // The rows a type's preset is checked through, each set away from its default.
    struct Probe { LayerTypeId type; const char* row; float value; const char* row2; float value2; };
    const Probe kProbes[] = { { LayerTypeId::Arp,   "cut",   2200.0f, "gate",   80.0f },
                              { LayerTypeId::Kick,  "swing", 0.7f,    "accent", 0.3f },
                              { LayerTypeId::Bass,  "swing", 0.6f,    "accent", 0.35f },
                              { LayerTypeId::Hat,   "swing", 0.5f,    "accent", 0.4f },
                              { LayerTypeId::Snare, "swing", 0.4f,    "accent", 0.45f } };

    juce::String rowId(LayerTypeId type, const char* row, int slot)
    {
        return juce::String(type == LayerTypeId::Arp ? "a" : "t") + juce::String(slot) + "_" + row;
    }

    // The wrap of a Kick's row: 16 sixteenths is 4 beats, which at 120 bpm and 48 kHz is 96,000 samples.
    constexpr double kBeatSamples = 24000.0;

    // Plays a MockHost transport through a processor, one block at a time. The processor is prepared for `prepared`
    // frames and fed blocks of `size` (larger than prepared is an oversize block); the transport starts at `startBeat`.
    struct Player
    {
        Player(mu_tronic::PluginProcessor& proc, int prepared, double startBeat = 0.0, int size = 0)
            : p(proc), blockSize(size > 0 ? size : prepared), buf(2, blockSize)
        {
            host.bpm = 120.0;
            host.ppq = startBeat;
            p.setPlayHead(&host);
            p.setRateAndBufferSizeDetails(48000.0, prepared);
            p.prepareToPlay(48000.0, prepared);
        }
        ~Player() { p.setPlayHead(nullptr); p.releaseResources(); }

        void block()
        {
            juce::MidiBuffer midi;
            host.timeInSamples = (juce::int64) index * blockSize;
            buf.clear();
            p.processBlock(buf, midi);
            host.ppq += (double) blockSize * host.bpm / (60.0 * 48000.0);
            ++index;
        }

        mu_tronic::PluginProcessor& p;
        int blockSize, index = 0;
        mu_golden::MockHost host;
        juce::AudioBuffer<float> buf;
    };

    juce::String stepsOf(ProcessorBase& p, int slot)
    {
        juce::String on;
        for (int s = 0; s < 16; ++s) on << (trackIn(p, slot).row.isOn(s) ? (trackIn(p, slot).row.isAccent(s) ? "A" : "x") : ".");
        return on;
    }
}

struct MuTronicLayerPresetTests : juce::UnitTest
{
    MuTronicLayerPresetTests() : juce::UnitTest("mu-Tronic layer presets in a slot", "mu-tronic-presets") {}

    void runTest() override
    {
        everyTypeNamesItsFile();
        roundTripPerType();
        filesFromTheProducts();
        adoptByEmptySlot();
        emptyRowAdopted();
        crossTypeRefused();
        engineFilesLandInTheirOwnSlot();
        unreadableFile();
        stagedWhilePlaying();
        stagedBlockSizeSweep();
        refusedWhilePlaying();
        typeChangeCancelsStage();
    }

    // Saves the slot's layer under `presetName` and returns the file in its type's folder.
    juce::File saveFrom(PresetProcessor& p, int slot, const juce::String& presetName)
    {
        p.saveLayerPreset(slot, presetName);
        return p.typedLayerPresetDir(slot).getChildFile(presetName + "." + p.typedLayerPresetExtension(slot));
    }

    void everyTypeNamesItsFile()
    {
        beginTest("every registered type can be told from a file: a root tag or typeFromPreset");
        mu_seam_test::TempStorage storage;
        PresetProcessor p;
        for (int t = 0; t < p.getLayerTypes().size(); ++t)
        {
            const auto& info = p.getLayerTypes().at(t);
            expect(juce::String(info.presetTag).isNotEmpty() || info.typeFromPreset != nullptr,
                   juce::String(info.name) + " has a presetTag or typeFromPreset");
        }
        p.flushPendingAsyncUpdates();
    }

    void roundTripPerType()
    {
        beginTest("a layer preset saved from a slot of each type loads into a fresh Empty slot and into a filled slot of the type");
        for (const auto& pr : kProbes)
        {
            mu_seam_test::TempStorage storage;
            PresetProcessor p;
            const auto* info = p.getLayerTypes().find(pr.type);
            const juce::String label = info != nullptr ? info->name : "?";
            expect(info != nullptr && juce::String(info->presetTag).isNotEmpty(), label + " names its preset root tag");
            if (info == nullptr) continue;

            expect(p.setLayerType(2, pr.type));
            mu_seam_test::setParam(p.apvts, rowId(pr.type, pr.row, 2), pr.value);
            mu_seam_test::setParam(p.apvts, rowId(pr.type, pr.row2, 2), pr.value2);
            if (pr.type != LayerTypeId::Arp)
                for (int s = 0; s < 16; ++s) { trackIn(p, 2).row.setOn(s, s % 3 == 0); trackIn(p, 2).row.setAccent(s, s == 3); }
            const auto file = saveFrom(p, 2, "RoundTrip");
            expect(file.existsAsFile(), label + " preset file is written to the type's own folder");
            if (const auto xml = juce::XmlDocument::parse(file))
                expectEquals(xml->getTagName(), juce::String(info->presetTag), label + " root tag");

            // Into an Empty slot of a fresh processor: the file's type is adopted and the values come with it.
            mu_seam_test::TempStorage storage2;
            PresetProcessor q;
            juce::String error;
            q.onLoadError = [&error](const juce::String& m) { error = m; };
            int committed = -1;
            q.onLayerPresetCommitted = [&committed](int s) { committed = s; };
            q.loadLayerPreset(5, file);
            expect(q.getLayerTypeId(5) == pr.type, label + " is adopted by an Empty slot");
            expectEquals(error, juce::String());
            expectEquals(committed, 5, "the editor is told");
            expectWithinAbsoluteError(getPlain(q.apvts, rowId(pr.type, pr.row, 5)), pr.value, 0.01f * juce::jmax(1.0f, pr.value));
            expectWithinAbsoluteError(getPlain(q.apvts, rowId(pr.type, pr.row2, 5)), pr.value2, 0.01f * juce::jmax(1.0f, pr.value2));
            if (pr.type != LayerTypeId::Arp)
                for (int s = 0; s < 16; ++s)
                {
                    expect(trackIn(q, 5).row.isOn(s) == (s % 3 == 0), label + " step row travels (step " + juce::String(s) + ")");
                    expect(trackIn(q, 5).row.isAccent(s) == (s == 3), label + " accents travel (step " + juce::String(s) + ")");
                }

            // Into a filled slot of the same type (stopped: it applies at once), whose row and values start elsewhere.
            expect(q.setLayerType(7, pr.type));
            if (pr.type != LayerTypeId::Arp)
                for (int s = 0; s < 16; ++s) { trackIn(q, 7).row.setOn(s, false); trackIn(q, 7).row.setAccent(s, false); }
            expect(getPlain(q.apvts, rowId(pr.type, pr.row, 7)) != pr.value, "the filled slot starts elsewhere");
            q.loadLayerPreset(7, file);
            expectEquals(error, juce::String());
            expectWithinAbsoluteError(getPlain(q.apvts, rowId(pr.type, pr.row, 7)), pr.value, 0.01f * juce::jmax(1.0f, pr.value));
            expectWithinAbsoluteError(getPlain(q.apvts, rowId(pr.type, pr.row2, 7)), pr.value2, 0.01f * juce::jmax(1.0f, pr.value2));
            expect(q.getLayerTypeId(7) == pr.type);
            if (pr.type != LayerTypeId::Arp)
                expectEquals(stepsOf(q, 7), stepsOf(q, 5), label + " step row after a filled-slot load");
            p.flushPendingAsyncUpdates();
            q.flushPendingAsyncUpdates();
        }
    }

    // Files the real mu-Toni and mu-On wrote with their own save code (tests/presets/mu-tronic-slot).
    void filesFromTheProducts()
    {
        beginTest("a .muArp written by mu-Toni and a .muTrack written by mu-On load into mu-Tronic slots");
        const juce::File dir(MU_TRONIC_FIXTURE_DIR);
        const auto arp = dir.getChildFile("REAL_arp.muArp");
        const auto kick = dir.getChildFile("REAL_kick.muTrack");
        expect(arp.existsAsFile() && kick.existsAsFile(), "the fixture files are in the repository");

        mu_seam_test::TempStorage storage;
        PresetProcessor p;
        juce::String error;
        p.onLoadError = [&error](const juce::String& m) { error = m; };

        p.loadLayerPreset(3, arp);
        expectEquals(error, juce::String());
        expect(p.getLayerTypeId(3) == LayerTypeId::Arp, "mu-Toni's file makes an Arp");
        expectWithinAbsoluteError(getPlain(p.apvts, "a3_cut"), 2200.0f, 22.0f);

        p.loadLayerPreset(4, kick);
        expectEquals(error, juce::String());
        expect(p.getLayerTypeId(4) == LayerTypeId::Kick, "mu-On's file makes a Kick by its engine row");
        expectWithinAbsoluteError(getPlain(p.apvts, "t4_swing"), 0.4f, 0.01f);
        expectWithinAbsoluteError(getPlain(p.apvts, "t4_accent"), 0.6f, 0.01f);
        expectEquals(stepsOf(p, 4), juce::String("x....A....x....x"), "the step row and its accent");
        expectEquals((int) std::lround(getPlain(p.apvts, "t4_eng")), 0);

        // Into filled slots of the same types, and refused by the others.
        expect(p.setLayerType(6, LayerTypeId::Kick));
        p.loadLayerPreset(6, kick);
        expectEquals(error, juce::String());
        expectEquals(stepsOf(p, 6), stepsOf(p, 4));
        expect(p.setLayerType(7, LayerTypeId::Arp));
        p.loadLayerPreset(7, arp);
        expectEquals(error, juce::String());
        expectWithinAbsoluteError(getPlain(p.apvts, "a7_cut"), 2200.0f, 22.0f);
        expect(p.setLayerType(8, LayerTypeId::Snare));
        p.loadLayerPreset(8, kick);
        expectEquals(error, juce::String("This is a Kick preset; this slot is a Snare"));
        p.loadLayerPreset(6, arp);
        expectEquals(error, juce::String("This is an Arp preset; this slot is a Kick"));
        p.flushPendingAsyncUpdates();
    }

    void adoptByEmptySlot()
    {
        beginTest("an Empty slot takes the type of a .muTrack file by its engine row, a name stays the slot's");
        mu_seam_test::TempStorage storage;
        PresetProcessor p;
        expect(p.setLayerType(1, LayerTypeId::Hat));
        const auto file = saveFrom(p, 1, "Ticker");
        p.setLayerName(3, "Mine");   // an Empty slot's name is its own and a preset does not rename it
        p.loadLayerPreset(3, file);
        expect(p.getLayerTypeId(3) == LayerTypeId::Hat);
        expect(trackIn(p, 3).engineSetting() == mu_on::EngineType::Hat && (int) std::lround(getPlain(p.apvts, "t3_eng")) == 2);
        expectEquals(p.getLayerName(3), juce::String("Mine"));
        p.flushPendingAsyncUpdates();
    }

    // The default groove is given at creation, before the file's rows go in: a saved empty row must stay empty.
    void emptyRowAdopted()
    {
        beginTest("a preset with an empty step row adopted by an Empty slot leaves the row empty (the groove does not replace it)");
        mu_seam_test::TempStorage storage;
        PresetProcessor p;
        expect(p.setLayerType(0, LayerTypeId::Kick));
        for (int s = 0; s < 16; ++s) { trackIn(p, 0).row.setOn(s, false); trackIn(p, 0).row.setAccent(s, false); }
        const auto file = saveFrom(p, 0, "Silent");
        p.loadLayerPreset(5, file);
        expect(p.getLayerTypeId(5) == LayerTypeId::Kick);
        expectEquals(stepsOf(p, 5), juce::String("................"));
        p.flushPendingAsyncUpdates();
    }

    void crossTypeRefused()
    {
        beginTest("a preset of another type is refused for a filled slot, with a message, and nothing changes");
        mu_seam_test::TempStorage storage;
        PresetProcessor p;
        expect(p.setLayerType(0, LayerTypeId::Snare));
        mu_seam_test::setParam(p.apvts, "t0_swing", 0.3f);
        const auto snareFile = saveFrom(p, 0, "Crack");
        expect(p.setLayerType(1, LayerTypeId::Arp));
        mu_seam_test::setParam(p.apvts, "a1_cut", 3300.0f);
        const auto arpFile = saveFrom(p, 1, "Pluck");
        expect(p.setLayerType(2, LayerTypeId::Kick));
        mu_seam_test::setParam(p.apvts, "t2_swing", 0.9f);

        juce::String error;
        p.onLoadError = [&error](const juce::String& m) { error = m; };
        int committed = 0;
        p.onLayerPresetCommitted = [&committed](int) { ++committed; };
        const auto before = p.captureState();

        p.loadLayerPreset(2, snareFile);
        expectEquals(error, juce::String("This is a Snare preset; this slot is a Kick"));
        expect(p.getLayerTypeId(2) == LayerTypeId::Kick);
        expectWithinAbsoluteError(getPlain(p.apvts, "t2_swing"), 0.9f, 0.001f);
        expect(! p.hasPendingSwap(2));

        error.clear();
        p.loadLayerPreset(2, arpFile);
        expectEquals(error, juce::String("This is an Arp preset; this slot is a Kick"));
        expect(p.getLayerTypeId(2) == LayerTypeId::Kick);

        error.clear();
        p.loadLayerPreset(1, snareFile);
        expectEquals(error, juce::String("This is a Snare preset; this slot is an Arp"));
        expect(p.getLayerTypeId(1) == LayerTypeId::Arp);

        expectEquals(committed, 0, "a refusal tells the editor nothing");
        expect(p.captureState().isEquivalentTo(before), "the whole state is unchanged");
        p.flushPendingAsyncUpdates();
    }

    void engineFilesLandInTheirOwnSlot()
    {
        beginTest(".muTrack files of the four engines land only in the slot of their engine");
        mu_seam_test::TempStorage storage;
        PresetProcessor p;
        const LayerTypeId engines[] = { LayerTypeId::Kick, LayerTypeId::Bass, LayerTypeId::Hat, LayerTypeId::Snare };
        juce::File files[4];
        for (int e = 0; e < 4; ++e)
        {
            expect(p.setLayerType(e, engines[e]));
            mu_seam_test::setParam(p.apvts, "t" + juce::String(e) + "_swing", 0.2f + 0.1f * (float) e);
            files[e] = saveFrom(p, e, juce::String("Eng") + juce::String(e));
        }
        // Each file into each of four other slots, one per engine.
        for (int f = 0; f < 4; ++f)
            for (int s = 0; s < 4; ++s)
            {
                const int slot = 8 + s;
                expect(p.setLayerType(slot, engines[s]));
                mu_seam_test::setParam(p.apvts, "t" + juce::String(slot) + "_swing", 0.0f);
                juce::String error;
                p.onLoadError = [&error](const juce::String& m) { error = m; };
                p.loadLayerPreset(slot, files[f]);
                const bool own = f == s;
                expect(error.isEmpty() == own, juce::String("file ") + juce::String(f) + " into slot of engine " + juce::String(s));
                expectWithinAbsoluteError(getPlain(p.apvts, "t" + juce::String(slot) + "_swing"), own ? 0.2f + 0.1f * (float) f : 0.0f, 0.001f);
                expect(p.getLayerTypeId(slot) == engines[s], "a refused or accepted file never changes the type");
            }
        p.onLoadError = nullptr;
        p.flushPendingAsyncUpdates();
    }

    void unreadableFile()
    {
        beginTest("a file that is not a layer preset of any offered type is reported and changes nothing; a missing file is silent");
        mu_seam_test::TempStorage storage;
        PresetProcessor p;
        expect(p.setLayerType(0, LayerTypeId::Kick));
        p.getContentDir().createDirectory();
        const auto junk = p.getContentDir().getChildFile("junk.muTrack");
        junk.replaceWithText("<NotAPreset/>");
        const auto foreign = p.getContentDir().getChildFile("other.muRhythm");
        foreign.replaceWithText("<MuClidLayer/>");   // a type this build does not offer
        juce::String error;
        p.onLoadError = [&error](const juce::String& m) { error = m; };
        const auto before = p.captureState();
        for (const auto& f : { junk, foreign })
        {
            const auto expected = "Could not read \"" + f.getFileName() + "\"";
            error.clear();
            p.loadLayerPreset(0, f);
            expectEquals(error, expected, f.getFileName() + " into a filled slot");
            error.clear();
            p.loadLayerPreset(4, f);   // an Empty slot adopts nothing either
            expectEquals(error, expected, f.getFileName() + " into an Empty slot");
            expect(p.getLayerTypeId(4) == LayerTypeId::Empty);
        }
        error.clear();
        p.loadLayerPreset(0, p.getContentDir().getChildFile("nothing-here.muTrack"));
        p.loadLayerPreset(4, p.getContentDir().getChildFile("nothing-here.muTrack"));
        expectEquals(error, juce::String(), "a file that does not exist returns silently, as the products do");
        expect(p.getLayerTypeId(4) == LayerTypeId::Empty);
        expect(p.captureState().isEquivalentTo(before));
        p.flushPendingAsyncUpdates();
    }

    // From the start of the transport: the Kick's row is 4 beats, so nothing lands before sample 96,000.
    void stagedWhilePlaying()
    {
        beginTest("a load while playing waits for the slot's wrap (not before it), then lands once and tells the editor");
        constexpr int kBlock = 256;
        mu_seam_test::TempStorage storage;
        PresetProcessor p;
        expect(p.setLayerType(0, LayerTypeId::Kick));
        mu_seam_test::setParam(p.apvts, "t0_swing", 0.8f);
        const auto file = saveFrom(p, 0, "Late");
        mu_seam_test::setParam(p.apvts, "t0_swing", 0.1f);

        int committed = -1, commits = 0;
        p.onLayerPresetCommitted = [&](int s) { committed = s; ++commits; };
        Player player(p, kBlock);
        for (int b = 0; b < 4; ++b) player.block();
        p.loadLayerPreset(0, file);
        expect(p.hasPendingSwap(0), "staged, not applied");

        long played = 4L * kBlock, landedAt = -1;
        for (int b = 0; b < 1200 && landedAt < 0; ++b)
        {
            player.block();
            played += kBlock;
            p.drainDeferredWork();
            if (! p.hasPendingSwap(0)) { landedAt = played; break; }
            expectWithinAbsoluteError(getPlain(p.apvts, "t0_swing"), 0.1f, 0.001f, "unchanged before the wrap");
            expectEquals(committed, -1, "no callback before the wrap");
        }
        expect(landedAt >= 4 * (long) kBeatSamples, "it landed at or after the wrap, sample " + juce::String(landedAt));
        expect(landedAt <= 4 * (long) kBeatSamples + kBlock, "and in the block that held it");
        expectWithinAbsoluteError(getPlain(p.apvts, "t0_swing"), 0.8f, 0.001f);
        expectEquals(committed, 0);
        expectEquals(commits, 1, "told once");
        p.flushPendingAsyncUpdates();
    }

    // The wrap is 2,400 samples after the load; blocks of one frame up to an oversize block find it.
    void stagedBlockSizeSweep()
    {
        beginTest("a staged load lands at the wrap whatever the block size, including an oversize block");
        struct Case { int prepared, size; };
        for (const auto c : { Case { 1, 1 }, Case { 64, 64 }, Case { 256, 256 }, Case { 1024, 1024 }, Case { 256, 2048 } })
        {
            const juce::String label = "prepared " + juce::String(c.prepared) + ", block " + juce::String(c.size);
            mu_seam_test::TempStorage storage;
            PresetProcessor p;
            expect(p.setLayerType(0, LayerTypeId::Kick));
            mu_seam_test::setParam(p.apvts, "t0_swing", 0.8f);
            const auto file = saveFrom(p, 0, "Sweep");
            mu_seam_test::setParam(p.apvts, "t0_swing", 0.1f);

            // The transport starts so the wrap is 2,400 samples after the two blocks played before the load.
            const long wrap = 2L * c.size + 2400;
            Player player(p, c.prepared, 4.0 - (double) wrap / kBeatSamples, c.size);
            long played = 0;
            for (int b = 0; b < 2; ++b) { player.block(); played += c.size; }
            p.loadLayerPreset(0, file);
            expect(p.hasPendingSwap(0), label + ": staged");

            long landedAt = -1;
            bool earlyChange = false;
            for (int b = 0; b < 20000 && landedAt < 0; ++b)
            {
                player.block();
                played += c.size;
                p.drainDeferredWork();
                if (! p.hasPendingSwap(0)) landedAt = played;
                else if (std::abs(getPlain(p.apvts, "t0_swing") - 0.1f) > 0.001f) earlyChange = true;
            }
            expect(! earlyChange, label + ": unchanged before the wrap");
            expect(landedAt >= wrap, label + ": not before the wrap (" + juce::String(landedAt) + ")");
            expect(landedAt <= wrap + c.size, label + ": in the block that held it (" + juce::String(landedAt) + ")");
            expectWithinAbsoluteError(getPlain(p.apvts, "t0_swing"), 0.8f, 0.001f);
            p.flushPendingAsyncUpdates();
        }
    }

    void refusedWhilePlaying()
    {
        beginTest("a refused preset while playing stages nothing and changes nothing");
        mu_seam_test::TempStorage storage;
        PresetProcessor p;
        expect(p.setLayerType(0, LayerTypeId::Snare));
        const auto snareFile = saveFrom(p, 0, "Crack");
        expect(p.setLayerType(1, LayerTypeId::Kick));
        mu_seam_test::setParam(p.apvts, "t1_swing", 0.9f);
        juce::String error;
        int committed = 0;
        p.onLoadError = [&error](const juce::String& m) { error = m; };
        p.onLayerPresetCommitted = [&committed](int) { ++committed; };
        Player player(p, 256, 3.9);
        for (int b = 0; b < 4; ++b) player.block();
        p.loadLayerPreset(1, snareFile);
        expectEquals(error, juce::String("This is a Snare preset; this slot is a Kick"));
        expect(! p.hasPendingSwap(1), "nothing staged");
        for (int b = 0; b < 40; ++b) { player.block(); p.drainDeferredWork(); }
        expectWithinAbsoluteError(getPlain(p.apvts, "t1_swing"), 0.9f, 0.001f);
        expectEquals(committed, 0);
        p.flushPendingAsyncUpdates();
    }

    // A type change cancels the slot's stage, so a stage of the old type can never land on the new one; and if a stage
    // of another type were left, the applier's guard would skip it.
    void typeChangeCancelsStage()
    {
        beginTest("a Kick preset staged, then the slot becomes a Bass before the wrap: the stage is cancelled and nothing lands");
        mu_seam_test::TempStorage storage;
        PresetProcessor p;
        expect(p.setLayerType(0, LayerTypeId::Kick));
        mu_seam_test::setParam(p.apvts, "t0_swing", 0.8f);
        const auto file = saveFrom(p, 0, "Doomed");
        int committed = -1;
        p.onLayerPresetCommitted = [&committed](int s) { committed = s; };

        Player player(p, 256, 3.9);
        for (int b = 0; b < 2; ++b) player.block();
        p.loadLayerPreset(0, file);
        expect(p.hasPendingSwap(0));
        expect(p.setLayerType(0, LayerTypeId::Bass));
        expect(! p.hasPendingSwap(0), "the change cancelled the stage");
        mu_seam_test::setParam(p.apvts, "t0_swing", 0.15f);
        for (int b = 0; b < 40; ++b) { player.block(); p.drainDeferredWork(); }   // well past the wrap at 2,400 samples
        expect(p.getLayerTypeId(0) == LayerTypeId::Bass);
        expectWithinAbsoluteError(getPlain(p.apvts, "t0_swing"), 0.15f, 0.001f, "the Bass's rows were not overwritten");
        expectEquals(committed, -1, "no apply and no callback");
        p.flushPendingAsyncUpdates();
    }
};

static MuTronicLayerPresetTests muTronicLayerPresetTests;
