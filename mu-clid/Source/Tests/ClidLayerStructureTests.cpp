// mu-Clid's structural layer edits on the REAL PluginProcessor (ProcessorBase::addLayer / removeLayer /
// swapLayers / resetLayer plus mu-Clid's hooks): everything a slot owns must follow its rhythm, or stay
// where the slot's audio state belongs. Slots are tagged (steps, level, colour, sample path, engine, tail,
// sidechain source) so each moved piece can be traced to the slot it must land on.

#include "Plugin/PluginProcessor.h"
#include "Tests/ProcessorSeamHelpers.h"

#include <array>
#include <vector>

namespace
{
    using namespace mu_seam_test;

    struct StructureProcessor : mu_clid::PluginProcessor
    {
        using mu_clid::PluginProcessor::drainDeferredWork;
    };

    constexpr int kMax = mu_limits::kMaxLayers;

    // What each slot held before an edit, to compare with where it ends up.
    struct Snapshot
    {
        std::array<int, kMax>          steps {};
        std::array<int, kMax>          colour {};
        std::array<float, kMax>        level {};
        std::array<juce::String, kMax> samplePath;
        std::array<VoiceEngine*, kMax> engine {};
        std::array<VoiceEngine*, kMax> tail {};
        std::array<Layer*, kMax>       layer {};
        std::array<int, kMax>          scSrc {};   // the engine strip's sidechain source (-1 off, else a layer)
    };

    float plain(juce::AudioProcessorValueTreeState& apvts, const juce::String& id) { return getParam(apvts, id); }
}

struct ClidLayerStructureTests : juce::UnitTest
{
    ClidLayerStructureTests() : juce::UnitTest("mu-Clid layer structure", "mu-clid-processor") {}

    // Fills the processor to `kMax` rhythms and tags every slot i: steps 8 + i, level 0.1 * (i + 1), a sample
    // path, a retired tail, and strip 6 ducking from layer 3.
    static void fillAndTag(StructureProcessor& p)
    {
        while (p.getNumRhythms() < kMax) p.addLayer();
        for (int i = 0; i < kMax; ++i)
        {
            setParam(p.apvts, "r" + juce::String(i) + "_stepsA", (float) (8 + i));
            setParam(p.apvts, "ch" + juce::String(i) + "_lvl", 0.1f * (float) (i + 1));
            p.samples.setPath(i, "slot" + juce::String(i) + ".wav");
            p.getLayer(i)->colourIndex = (i * 3) % kMax;
            auto& tail = p.retiredVoiceEngines[(size_t) i][0];
            tail = std::make_unique<VoiceEngine>();
        }
        setParam(p.apvts, "ch6_scSrc", 4.0f);   // APVTS: 0 off, n = layer n - 1, so layer 3
        p.mixerEngine.channels[6].sidechainSource.store(3);
    }

    static Snapshot take(StructureProcessor& p)
    {
        Snapshot s;
        for (int i = 0; i < kMax; ++i)
        {
            s.steps[(size_t) i]      = p.layerAs<mu_clid::Rhythm>(i).genA.steps;
            s.colour[(size_t) i]     = p.layerAs<mu_clid::Rhythm>(i).colourIndex;
            s.level[(size_t) i]      = p.mixerEngine.channels[i].level.load();
            s.samplePath[(size_t) i] = p.samples.path(i);
            s.engine[(size_t) i]     = p.voiceEngines[(size_t) i].get();
            s.tail[(size_t) i]       = p.retiredVoiceEngines[(size_t) i][0].get();
            s.layer[(size_t) i]      = &p.layerAs<mu_clid::Rhythm>(i);
            s.scSrc[(size_t) i]      = p.mixerEngine.channels[i].sidechainSource.load();
        }
        return s;
    }

    void runTest() override
    {
        fillAndRemoveMiddle();
        swapEnds();
        resetKeepsIdentity();
        lastLayerStays();
        removedSlotsOwedNoteOff();
        editsCancelPendingSwaps();
        addedSlotIsClean();
    }

    void fillAndRemoveMiddle()
    {
        beginTest("fill to eight, then remove the middle: every slot's data follows its rhythm");
        TempStorage storage;
        StructureProcessor p;
        p.prepareToPlay(48000.0, 256);
        fillAndTag(p);
        expectEquals(p.getNumRhythms(), kMax);
        const auto before = take(p);

        constexpr int gone = 3;
        expect(p.removeLayer(gone));
        const auto after = take(p);
        expectEquals(p.getNumRhythms(), kMax - 1);
        expectEquals(p.sequencer.getNumRhythms(), kMax - 1);

        for (int i = 0; i < kMax - 1; ++i)
        {
            const int from = i < gone ? i : i + 1;
            const auto at = "slot " + juce::String(i) + " now holds the old slot " + juce::String(from);
            expectEquals(after.steps[(size_t) i], before.steps[(size_t) from], at + ": rhythm data");
            expectEquals((int) plain(p.apvts, "r" + juce::String(i) + "_stepsA"), before.steps[(size_t) from], at + ": parameters");
            expectEquals(after.colour[(size_t) i], before.colour[(size_t) from], at + ": colour");
            expect(std::abs(after.level[(size_t) i] - before.level[(size_t) from]) < 1.0e-4f, at + ": mixer strip");
            expect(std::abs(plain(p.apvts, "ch" + juce::String(i) + "_lvl") - before.level[(size_t) from]) < 2.0e-3f, at + ": strip parameter");
            expect(after.samplePath[(size_t) i] == before.samplePath[(size_t) from], at + ": sample path");
            expect(after.engine[(size_t) i] == before.engine[(size_t) from], at + ": the voice engine moved with it (same object)");
            expect(after.tail[(size_t) i] == before.tail[(size_t) from], at + ": the retired tail moved with it (same object)");
            expect(after.layer[(size_t) i] == before.layer[(size_t) from], at + ": the layer object moved by pointer");
        }

        expect(p.voiceEngines[kMax - 1] == nullptr, "the vacated top slot holds no engine");
        expect(p.retiredVoiceEngines[kMax - 1][0] == nullptr, "and no tail");
        expect(p.samples.path(kMax - 1).isEmpty(), "and no sample path");
        expect(p.getLayer(kMax - 1) == nullptr && p.sequencer.getNumRhythms() == kMax - 1, "and is not live");
        expect(p.layerAs<mu_clid::Rhythm>(kMax - 1).name != "", "though a spare rhythm stays in it");

        // Strip 6 ducked from layer 3, the one removed: it now ducks from nothing. Strip 6 itself is slot 5 now.
        expectEquals(after.scSrc[5], -1, "a sidechain from the removed rhythm is gone");
        expectEquals((int) plain(p.apvts, "ch5_scSrc"), 0, "in the parameter too");
        for (int i = 0; i < kMax - 1; ++i)
            expect(&p.sequencer.getRhythm(i) == p.getLayer(i), "the sequencer is bound to the layer in each live slot");
    }

    void swapEnds()
    {
        beginTest("swap the first and the last: data, strips, sample paths and engines exchange; tails stay with the slot");
        TempStorage storage;
        StructureProcessor p;
        p.prepareToPlay(48000.0, 256);
        fillAndTag(p);
        const auto before = take(p);
        const int a = 0, b = kMax - 1;

        expect(p.swapLayers(a, b));
        const auto after = take(p);

        for (int i = 0; i < kMax; ++i)
        {
            const int from = i == a ? b : i == b ? a : i;
            const auto at = "slot " + juce::String(i) + " holds the old slot " + juce::String(from);
            expectEquals(after.steps[(size_t) i], before.steps[(size_t) from], at + ": rhythm data");
            expectEquals((int) plain(p.apvts, "r" + juce::String(i) + "_stepsA"), before.steps[(size_t) from], at + ": parameters");
            expectEquals(after.colour[(size_t) i], before.colour[(size_t) from], at + ": colour");
            expect(std::abs(after.level[(size_t) i] - before.level[(size_t) from]) < 1.0e-4f, at + ": mixer strip");
            expect(after.samplePath[(size_t) i] == before.samplePath[(size_t) from], at + ": sample path");
            expect(after.engine[(size_t) i] == before.engine[(size_t) from], at + ": engine");
            expect(after.layer[(size_t) i] == before.layer[(size_t) from], at + ": layer object");
            expect(after.tail[(size_t) i] == before.tail[(size_t) i], at + ": a retired tail stays on its slot's channel");
        }
        // Strip 6 ducked from layer 3, which did not move.
        expectEquals(after.scSrc[6], 3, "a sidechain from an unmoved layer is unchanged");

        // Swapping the layer that strip 6 ducks from re-points it.
        expect(p.swapLayers(3, 5));
        expectEquals(p.mixerEngine.channels[6].sidechainSource.load(), 5, "the strip follows the layer it ducks from");
        expectEquals((int) plain(p.apvts, "ch6_scSrc"), 6, "in the parameter too");
    }

    void resetKeepsIdentity()
    {
        beginTest("reset one rhythm: its data, parameters and strip return to defaults; name, colour, engine and tail stay");
        TempStorage storage;
        StructureProcessor p;
        p.prepareToPlay(48000.0, 256);
        fillAndTag(p);
        p.getRhythm(2).name = "Keep me";
        const auto before = take(p);

        p.resetLayer(2);
        const auto after = take(p);

        expectEquals(p.getRhythm(2).genA.steps, mu_clid::Rhythm().genA.steps, "data is the default");
        expectEquals((int) plain(p.apvts, "r2_stepsA"), mu_clid::Rhythm().genA.steps, "parameters too");
        expect(std::abs(after.level[2] - 1.0f) < 1.0e-4f, "the strip is back to its defaults");
        expect(juce::String(p.getRhythm(2).name) == "Keep me", "the name stays");
        expectEquals(after.colour[2], before.colour[2], "the colour stays");
        expect(after.layer[2] == before.layer[2], "the layer object stays (the editor's pointers stay valid)");
        expect(after.engine[2] == before.engine[2], "the engine stays");
        expect(after.tail[2] == before.tail[2], "and so does its tail");
        expect(after.samplePath[2] == before.samplePath[2], "and the sample path");
        for (int i = 0; i < kMax; ++i)
            if (i != 2)
            {
                expectEquals(after.steps[(size_t) i], before.steps[(size_t) i], "other slots are untouched");
                expect(after.engine[(size_t) i] == before.engine[(size_t) i]);
            }
    }

    void lastLayerStays()
    {
        beginTest("the last layer cannot be removed, and a bad index changes nothing");
        TempStorage storage;
        StructureProcessor p;
        p.prepareToPlay(48000.0, 256);
        p.addLayer();
        expect(p.removeLayer(0));
        expectEquals(p.getNumRhythms(), 1);
        expect(! p.removeLayer(0), "the only layer stays");
        expect(! p.removeLayer(5));
        expect(! p.swapLayers(0, 0));
        expect(! p.swapLayers(0, 1));
        expectEquals(p.getNumRhythms(), 1);
        expect(p.voiceEngines[0] != nullptr);
    }

    // A MIDI-mode slot removed inside its 20 ms note hold still owes a note-off; the next block sends it.
    void removedSlotsOwedNoteOff()
    {
        beginTest("a removed slot's owed MIDI note-off still sounds");
        TempStorage storage;
        StructureProcessor p;
        p.prepareToPlay(48000.0, 256);
        p.addLayer();
        p.addLayer();

        juce::MidiBuffer scratch;
        p.midiEngines[1].trigger(scratch, 0, 61, 5, 1.0f);   // slot 1 has a note-on out and its off still owed
        expect(p.removeLayer(1));
        expectEquals(p.numOrphanOffs, 1, "the owed note-off was parked");

        juce::AudioBuffer<float> block(2, 256);
        juce::MidiBuffer midi;
        p.processBlock(block, midi);
        bool sawOff = false;
        for (const auto meta : midi)
        {
            const auto m = meta.getMessage();
            if (m.isNoteOff() && m.getNoteNumber() == 61 && m.getChannel() == 5) sawOff = true;
        }
        expect(sawOff, "the note-off is in the next block");
        expectEquals(p.numOrphanOffs, 0);
    }

    void editsCancelPendingSwaps()
    {
        beginTest("a structural edit cancels the pending per-slot swaps");
        TempStorage storage;
        StructureProcessor p;
        p.prepareToPlay(48000.0, 256);
        p.addLayer();
        p.addLayer();
        p.saveLayerPreset(0, "pend");
        const auto file = p.getRhythmsDir().getChildFile("pend.muRhythm");
        expect(file.existsAsFile());

        RunningPlayHead host(48000.0, 256);
        p.setPlayHead(&host);
        juce::AudioBuffer<float> block(2, 256);
        juce::MidiBuffer midi;
        for (int i = 0; i < 4; ++i) { block.clear(); midi.clear(); p.processBlock(block, midi); }
        expect(p.sequencerPlaying.load());

        auto stageAll = [&]
        {
            for (int s = 0; s < 3; ++s) p.stageRhythmPreset(s, file);
        };
        stageAll();
        expect(p.hasPendingSwap(0) && p.hasPendingSwap(1) && p.hasPendingSwap(2), "staged while playing");
        p.removeLayer(0);
        expect(! p.hasPendingSwap(0) && ! p.hasPendingSwap(1) && ! p.hasPendingSwap(2), "remove cancels every slot's");

        stageAll();
        p.swapLayers(0, 1);
        expect(! p.hasPendingSwap(0) && ! p.hasPendingSwap(1), "swap cancels both");

        stageAll();
        p.resetLayer(1);
        expect(! p.hasPendingSwap(1), "reset cancels that slot's");
        p.setPlayHead(nullptr);
    }

    void addedSlotIsClean()
    {
        beginTest("an added slot gets its own engine, a clean strip and path, and the first free colour");
        TempStorage storage;
        StructureProcessor p;
        p.prepareToPlay(48000.0, 256);
        setParam(p.apvts, "ch1_lvl", 0.3f);        // stale values left in the spare slot
        p.samples.setPath(1, "stale.wav");
        const auto firstColour = p.getLayer(0)->colourIndex;

        expectEquals(p.addLayer(), 1);
        expect(p.voiceEngines[1] != nullptr && p.voiceEngines[1] != p.voiceEngines[0], "a new engine");
        expect(p.samples.path(1).isEmpty(), "no sample path");
        expect(std::abs(plain(p.apvts, "ch1_lvl") - 1.0f) < 2.0e-3f, "a default strip");
        expect(std::abs(p.mixerEngine.channels[1].level.load() - 1.0f) < 1.0e-4f);
        expect(p.getLayer(1)->colourIndex != firstColour, "a colour the first rhythm does not use");
        expectEquals(p.sequencer.getNumRhythms(), 2);
        expect(&p.sequencer.getRhythm(1) == p.getLayer(1), "the sequencer is bound to the new layer");
        expect(juce::String(p.getRhythm(1).name) == "Rhythm 2");

        // The audio thread renders the new slot.
        RunningPlayHead host(48000.0, 256);
        p.setPlayHead(&host);
        juce::AudioBuffer<float> block(2, 256);
        juce::MidiBuffer midi;
        for (int i = 0; i < 8; ++i) { block.clear(); midi.clear(); p.processBlock(block, midi); }
        expect(bufferIsSane(block));
        p.setPlayHead(nullptr);
    }
};

static ClidLayerStructureTests clidLayerStructureTests;
