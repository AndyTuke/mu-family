// mu-Clid's structural layer edits on the REAL PluginProcessor (ProcessorBase::addLayer / removeLayer /
// swapLayers / resetLayer plus mu-Clid's hooks): everything a slot owns must follow its rhythm, or stay
// where the slot's audio state belongs. Slots are tagged (steps, level, colour, sample path, engine, tail,
// sidechain source) so each moved piece can be traced to the slot it must land on.

#include "Plugin/PluginProcessor.h"
#include "Sequencer/SampleRhythm.h"
#include "Tests/ProcessorSeamHelpers.h"

#include <array>
#include <vector>

// The allocation counter: ProcessorSeamTests.cpp carries the probe's global operator new (see AllocationProbe.h).
#include "Tests/AllocationProbe.h"

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
        std::array<const VoiceEngine*, kMax> tail {};   // the slot's first retire-bin entry, while it sounds
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
            p.retireEngineForTest(i, std::make_unique<VoiceEngine>());
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
            s.engine[(size_t) i]     = p.voiceEngine(i).get();
            s.tail[(size_t) i]       = p.retiredEngineForTest(i, 0);
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
        resetReleasesHeldNote();
        lastLayerStays();
        removedSlotsOwedNoteOff();
        editsCancelPendingSwaps();
        addedSlotIsClean();
        everyLayerIsASampleRhythm();
        playStateFollowsTheLayer();
    }

    // The play state is the layer's (the UI's circles look it up through RhythmHost on every read): after add,
    // swap, reset and remove the host hands out the live layer's own play state, and none for a slot not live.
    void playStateFollowsTheLayer()
    {
        beginTest("the UI's play state is the live layer's after add, swap, reset and remove");
        TempStorage storage;
        StructureProcessor p;
        p.prepareToPlay(48000.0, 256);
        mu_clid::RhythmHost& host = p;
        auto bound = [&](const char* when)
        {
            for (int i = 0; i < kMax; ++i)
            {
                auto* layer = dynamic_cast<mu_clid::SampleRhythm*>(p.getLayer(i));
                if (i < p.getNumRhythms())
                    expect(layer != nullptr && host.getRhythmPlayState(i) == &layer->playState,
                           juce::String("slot ") + juce::String(i) + " reads its layer's play state " + when);
                else
                    expect(host.getRhythmPlayState(i) == nullptr,
                           juce::String("slot ") + juce::String(i) + " not live reads none " + when);
            }
        };
        bound("at construction");
        p.addLayer();
        p.addLayer();
        bound("after adds");

        // A swap moves the layers, play state included: each slot's play state is the one its new layer carries.
        auto* const at0 = host.getRhythmPlayState(0);
        auto* const at2 = host.getRhythmPlayState(2);
        expect(p.swapLayers(0, 2));
        bound("after a swap");
        expect(host.getRhythmPlayState(0) == at2 && host.getRhythmPlayState(2) == at0, "the play states moved with their layers");

        // A reset takes the defaults in place (adoptDefaults): the slot keeps its layer and so its play state.
        const auto* const kept = p.getLayer(1);
        auto* const keptState = host.getRhythmPlayState(1);
        p.resetLayer(1);
        bound("after a reset");
        expect(p.getLayer(1) == kept && host.getRhythmPlayState(1) == keptState, "the reset slot keeps its layer");

        expect(p.removeLayer(0));
        bound("after a remove");
        expect(host.getRhythmPlayState(1) == at0, "the layer that moved down (from slot 2) brought its play state");
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

        expect(p.voiceEngine(kMax - 1) == nullptr, "the vacated top slot holds no engine");
        expectEquals(p.retiredCountForTest(kMax - 1), 0, "and no sounding tail (the removed slot's were cut)");
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

    // What a held-note scenario does around the reset of slot 1.
    struct HeldCase
    {
        bool reset      = false;   // reset slot 1 once the note is held
        bool relToEnd   = false;   // the engine plays the sample through instead of releasing
        bool running    = false;   // a host transport runs after the reset, so the new rhythm hits
        bool tiedAfter  = false;   // after the blocks, fire a tied (pattern-legato) hit by hand
        bool tiedMid    = false;   // fire the tied hit 20 blocks into the release instead
        bool panLeft    = false;   // pan slot 1 hard left before the held note is measured
    };

    struct HeldResult { float peak = 0.0f; float maxPeak = 0.0f; int audioAllocs = 0; float beforeTie = 0.0f; float afterTie = 0.0f; float peakL = 0.0f; float peakR = 0.0f; float peakBoth = 0.0f; };

    // A hit's amp envelope sits at sustain until the next trigger, so a held sample note keeps sounding
    // through a reset (the engine stays) unless the reset tells it to release. Runs `blocks` blocks after
    // the reset and returns slot 1's peak at the end, the largest peak seen, and the audio-thread allocations.
    HeldResult heldNote(const HeldCase& c, int blocks)
    {
        constexpr int    kBlock = 256;
        constexpr double kRate  = 48000.0;
        HeldResult res;

        TempStorage storage;
        StructureProcessor p;
        p.prepareToPlay(kRate, kBlock);
        p.addLayer();

        // A steady 4 s tone in slot 1's engine, triggered by hand; without a running transport no other hit lands.
        auto f = p.getSamplesDir().getChildFile("Held 01.wav");
        p.getSamplesDir().createDirectory();
        f.deleteFile();
        {
            juce::WavAudioFormat wav;
            std::unique_ptr<juce::OutputStream> out = f.createOutputStream();
            auto w = wav.createWriterFor(out, juce::AudioFormatWriterOptions{}.withSampleRate(44100.0)
                                                                              .withNumChannels(1)
                                                                              .withBitsPerSample(16));
            juce::AudioBuffer<float> b(1, 44100 * 4);
            for (int i = 0; i < b.getNumSamples(); ++i)
                b.setSample(0, i, 0.4f * (float) std::sin(juce::MathConstants<double>::twoPi * 220.0 * i / 44100.0));
            if (w != nullptr) w->writeFromAudioSampleBuffer(b, 0, b.getNumSamples());
        }
        p.getRhythm(1).voiceParams.ampRelToEnd = c.relToEnd;
        p.voiceEngine(1)->setParams(p.getRhythm(1).voiceParams);   // the engine takes the flag at its next trigger
        p.voiceEngine(1)->loadFile(f);
        p.voiceEngine(1)->trigger(false, false);

        juce::AudioBuffer<float> block(2, kBlock);
        juce::MidiBuffer midi;
        mu_seam_test::RunningPlayHead host(kRate, kBlock);
        auto run = [&](int n)
        {
            for (int i = 0; i < n; ++i)
            {
                block.clear(); midi.clear(); p.processBlock(block, midi);
                res.maxPeak = juce::jmax(res.maxPeak, p.mixerEngine.channelPeaks[1].load());
            }
        };
        if (c.panLeft) p.mixerEngine.channels[1].pan.store(-1.0f, std::memory_order_relaxed);
        run(20);
        expect(p.mixerEngine.channelPeaks[1].load() > 0.05f, "the held note sounds before the reset");
        res.peakL    = p.mixerEngine.channelPeaksL[1].load();
        res.peakR    = p.mixerEngine.channelPeaksR[1].load();
        res.peakBoth = p.mixerEngine.channelPeaks[1].load();

        if (c.reset) p.resetLayer(1);
        if (c.running) p.setPlayHead(&host);

        res.maxPeak = 0.0f;
        mu_alloc::tls.allocs = 0;
        mu_alloc::tls.armed = true;
        if (c.tiedMid)
        {
            // Mid-release: a tied hit must neither retrigger the envelope nor lift the level.
            run(20);
            res.beforeTie = p.mixerEngine.channelPeaks[1].load();
            p.voiceEngine(1)->trigger(false, true);
            run(1);
            res.afterTie = p.mixerEngine.channelPeaks[1].load();
            run(blocks - 21);
        }
        else
            run(blocks);
        mu_alloc::tls.armed = false;
        res.audioAllocs = mu_alloc::tls.allocs;
        p.setPlayHead(nullptr);

        if (c.tiedAfter)
        {
            p.voiceEngine(1)->trigger(false, true);
            res.maxPeak = 0.0f;
            run(20);
        }
        res.peak = p.mixerEngine.channelPeaks[1].load();
        return res;
    }

    void resetReleasesHeldNote()
    {
        beginTest("reset a sounding rhythm: its held sample note releases within the release time, allocating nothing");

        // The default release is 0.5 s (94 blocks of 256 at 48 kHz); allow 1.5 times that.
        constexpr int kBlocks = 140;
        HeldCase reset;   reset.reset = true;
        const auto released = heldNote(reset, kBlocks);
        const auto control  = heldNote(HeldCase{}, kBlocks);
        expect(control.peak > 0.05f, "without a reset the note is still held after the same time (peak " + juce::String(control.peak, 3) + ")");
        expect(released.peak < 1.0e-3f, "after a reset the slot has decayed to silence (peak " + juce::String(released.peak, 5) + ")");
        expectEquals(released.audioAllocs, 0, "the audio thread allocated nothing while the note released");

        beginTest("reset with the transport running: the new rhythm's own hits are not released");
        HeldCase running;   running.reset = true; running.running = true;
        const auto live = heldNote(running, kBlocks);
        expect(live.maxPeak > 0.05f, "a fresh hit after the reset sounds (largest peak " + juce::String(live.maxPeak, 3) + ")");
        expectEquals(live.audioAllocs, 0, "and the audio thread allocated nothing");

        beginTest("reset a release-to-end slot: the note plays through, as the setting asks");
        HeldCase toEnd;   toEnd.reset = true; toEnd.relToEnd = true;
        const auto played = heldNote(toEnd, kBlocks);
        expect(played.peak > 0.05f, "the sample is not cut by the reset (peak " + juce::String(played.peak, 3) + ")");
        expectEquals(played.audioAllocs, 0, "and the audio thread allocated nothing");

        beginTest("after a reset released the envelope, the first tied (legato) hit still sounds");
        HeldCase tied;   tied.reset = true; tied.tiedAfter = true;
        const auto tiedHit = heldNote(tied, kBlocks);
        expect(tiedHit.maxPeak > 0.05f, "the tied hit starts the envelope (largest peak " + juce::String(tiedHit.maxPeak, 3) + ")");

        beginTest("the strip's stereo peaks follow the pan: hard left leaves the right bar silent");
        HeldCase panned;   panned.panLeft = true;
        const auto pan = heldNote(panned, 1);
        const auto centre = heldNote(HeldCase{}, 1);
        expect(pan.peakL > 0.05f && pan.peakR < 1.0e-3f, "hard left: L " + juce::String(pan.peakL, 3) + ", R " + juce::String(pan.peakR, 5));
        expect(centre.peakL > 0.05f && std::abs(centre.peakL - centre.peakR) < 1.0e-3f, "centred mono source: both sides equal");
        expectWithinAbsoluteError(pan.peakBoth, juce::jmax(pan.peakL, pan.peakR), 1.0e-6f);

        beginTest("a tied hit 20 blocks into the release keeps decaying: no new attack");
        HeldCase mid;   mid.reset = true; mid.tiedMid = true;
        const auto midHit = heldNote(mid, kBlocks);
        expect(midHit.beforeTie > 0.0f && midHit.beforeTie < 0.4f, "the note is part-way through its release (peak " + juce::String(midHit.beforeTie, 3) + ")");
        // The tied hit adds a second sample voice under the same decaying envelope, so at most twice the level;
        // a retriggered envelope would instead stop decaying, which the final check catches.
        expect(midHit.afterTie <= 2.1f * midHit.beforeTie, "the tied hit adds no attack (" + juce::String(midHit.beforeTie, 3) + " to " + juce::String(midHit.afterTie, 3) + ")");
        expect(midHit.peak < 1.0e-3f, "the release still runs to silence (peak " + juce::String(midHit.peak, 5) + ")");
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
        expect(p.voiceEngine(0) != nullptr);
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
        p.sampleRhythm(1).midi.trigger(scratch, 0, 61, 5, 1.0f);   // slot 1 has a note-on out and its off still owed
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
        expect(p.voiceEngine(1) != nullptr && p.voiceEngine(1) != p.voiceEngine(0), "a new engine");
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

    // Every slot holds mu-Clid's layer type, from construction and through each structural edit.
    void everyLayerIsASampleRhythm()
    {
        beginTest("every layer is a SampleRhythm: at construction and after add, remove, swap and reset");
        TempStorage storage;
        StructureProcessor p;
        p.prepareToPlay(48000.0, 256);
        auto allSampleRhythms = [&p]
        {
            for (int i = 0; i < kMax; ++i)
                if (dynamic_cast<mu_clid::SampleRhythm*>(p.getLayer(i)) == nullptr
                    && i < p.getNumRhythms())
                    return false;
            return true;
        };
        expect(allSampleRhythms(), "at construction");
        p.addLayer();
        p.addLayer();
        expect(allSampleRhythms(), "after adds");
        expect(p.swapLayers(0, 2));
        expect(allSampleRhythms(), "after a swap");
        p.resetLayer(1);
        expect(allSampleRhythms(), "after a reset");
        expect(p.removeLayer(0));
        expect(allSampleRhythms(), "after a remove");
    }
};

static ClidLayerStructureTests clidLayerStructureTests;
