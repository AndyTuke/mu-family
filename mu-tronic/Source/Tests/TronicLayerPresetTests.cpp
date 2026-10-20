// Layer presets in a mu-Tronic slot (docs/mu-tronic/design.md 2.1.11 point 8), one implementation in ProcessorBase for every
// type: a saved file round-trips through a slot of its type, an Empty slot takes the file's type, a slot of another type
// refuses and changes nothing, the four .muTrack engines land only in their own slot, and a load while playing waits for
// the slot's wrap.

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

    // The row a type's preset is checked through, and a value away from its default.
    struct Probe { LayerTypeId type; const char* row; float value; };
    const Probe kProbes[] = { { LayerTypeId::Arp,   "cut",   2200.0f },
                              { LayerTypeId::Kick,  "swing", 0.7f },
                              { LayerTypeId::Bass,  "swing", 0.6f },
                              { LayerTypeId::Hat,   "swing", 0.5f },
                              { LayerTypeId::Snare, "swing", 0.4f } };

    juce::String paramId(const Probe& pr, int slot)
    {
        return juce::String(pr.type == LayerTypeId::Arp ? "a" : "t") + juce::String(slot) + "_" + pr.row;
    }

    // Plays a MockHost transport through a processor, one block at a time.
    struct Player
    {
        Player(mu_tronic::PluginProcessor& proc, int block) : p(proc), blockSize(block), buf(2, block)
        {
            host.bpm = 120.0;
            p.setPlayHead(&host);
            p.setRateAndBufferSizeDetails(48000.0, block);
            p.prepareToPlay(48000.0, block);
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
}

struct MuTronicLayerPresetTests : juce::UnitTest
{
    MuTronicLayerPresetTests() : juce::UnitTest("mu-Tronic layer presets in a slot", "mu-tronic-presets") {}

    void runTest() override
    {
        roundTripPerType();
        adoptByEmptySlot();
        crossTypeRefused();
        engineFilesLandInTheirOwnSlot();
        unreadableFile();
        stagedWhilePlaying();
    }

    // Saves the slot's layer under `name` and returns the file in its type's folder.
    juce::File saveFrom(PresetProcessor& p, int slot, const juce::String& name)
    {
        p.saveLayerPreset(slot, name);
        return p.typedLayerPresetDir(slot).getChildFile(name + "." + p.typedLayerPresetExtension(slot));
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
            mu_seam_test::setParam(p.apvts, paramId(pr, 2), pr.value);
            if (pr.type != LayerTypeId::Arp)
                for (int s = 0; s < 16; ++s) trackIn(p, 2).row.setOn(s, s % 3 == 0);
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
            expectWithinAbsoluteError(getPlain(q.apvts, paramId(pr, 5)), pr.value, 0.01f * juce::jmax(1.0f, pr.value));
            if (pr.type != LayerTypeId::Arp)
                for (int s = 0; s < 16; ++s)
                    expect(trackIn(q, 5).row.isOn(s) == (s % 3 == 0), label + " step row travels (step " + juce::String(s) + ")");

            // Into a filled slot of the same type (stopped: it applies at once).
            expect(q.setLayerType(7, pr.type));
            expect(getPlain(q.apvts, paramId(pr, 7)) != pr.value, "the filled slot starts elsewhere");
            q.loadLayerPreset(7, file);
            expectEquals(error, juce::String());
            expectWithinAbsoluteError(getPlain(q.apvts, paramId(pr, 7)), pr.value, 0.01f * juce::jmax(1.0f, pr.value));
            expect(q.getLayerTypeId(7) == pr.type);
            p.flushPendingAsyncUpdates();
            q.flushPendingAsyncUpdates();
        }
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
        expectEquals(error, juce::String("This is a Arp preset; this slot is a Kick"));
        expect(p.getLayerTypeId(2) == LayerTypeId::Kick);

        error.clear();
        p.loadLayerPreset(1, snareFile);
        expectEquals(error, juce::String("This is a Snare preset; this slot is a Arp"));
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
        beginTest("a file that is not a layer preset of any offered type is reported and changes nothing");
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
            error.clear();
            p.loadLayerPreset(0, f);
            expect(error.startsWith("Could not read"), f.getFileName() + " is reported");
            p.loadLayerPreset(4, f);   // an Empty slot adopts nothing either
            expect(p.getLayerTypeId(4) == LayerTypeId::Empty);
        }
        expect(p.captureState().isEquivalentTo(before));
        p.flushPendingAsyncUpdates();
    }

    void stagedWhilePlaying()
    {
        beginTest("a load while playing waits for the slot's wrap, then lands and tells the editor");
        mu_seam_test::TempStorage storage;
        PresetProcessor p;
        expect(p.setLayerType(0, LayerTypeId::Kick));
        mu_seam_test::setParam(p.apvts, "t0_swing", 0.8f);
        const auto file = saveFrom(p, 0, "Late");
        mu_seam_test::setParam(p.apvts, "t0_swing", 0.1f);

        int committed = -1;
        p.onLayerPresetCommitted = [&committed](int s) { committed = s; };
        Player player(p, 256);
        for (int b = 0; b < 4; ++b) player.block();
        p.loadLayerPreset(0, file);
        expect(p.hasPendingSwap(0), "staged, not applied");
        expectWithinAbsoluteError(getPlain(p.apvts, "t0_swing"), 0.1f, 0.001f);

        for (int b = 0; b < 1200 && p.hasPendingSwap(0); ++b)
        {
            player.block();
            p.drainDeferredWork();
        }
        expect(! p.hasPendingSwap(0), "it landed at the wrap");
        expectWithinAbsoluteError(getPlain(p.apvts, "t0_swing"), 0.8f, 0.001f);
        expectEquals(committed, 0);
        p.flushPendingAsyncUpdates();
    }
};

static MuTronicLayerPresetTests muTronicLayerPresetTests;
