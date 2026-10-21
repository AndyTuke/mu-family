// Per-layer MIDI In in mu-Tronic (docs/mu-tronic/design.md T8a): each slot's strip carries a `ch{N}_midiIn` row (0 Omni,
// 1-16), the shared block driver turns it into the channel mask of that slot's MIDI view, and a layer that follows the
// keys (the Arp's held stack) hears only its channel. The row moves with its layer, is saved with the session, and the
// keys of a block skipped for an edit reach the layer on the channel it listens to. Also the processing-load meter.

#include "Plugin/PluginProcessor.h"
#include "Plugin/CpuLoadMeter.h"
#include "Sequencer/Arp.h"
#include "Persistence/PatternLayerType.h"
#include "Sequencer/PatternType.h"          // mu-Tant's layer library
#include "UI/PatternHost.h"
#include "Tests/GoldenRender.h"             // mu-core: MockHost
#include "Tests/ProcessorSeamHelpers.h"

namespace
{
    using mu_core::LayerTypeId;

    struct MidiInProcessor : mu_tronic::PluginProcessor
    {
        using mu_tronic::PluginProcessor::layerAccess;
    };

    mu_toni::Arp& arpIn(MidiInProcessor& p, int slot) { return *dynamic_cast<mu_toni::Arp*>(p.getLayer(slot)); }

    // One 256-frame block with `midi`; `skipped` makes it a block the base renders nothing for (an edit in flight).
    void block(MidiInProcessor& p, const juce::MidiBuffer& midi, bool skipped = false)
    {
        juce::AudioBuffer<float> buf(2, 256);
        buf.clear();
        juce::MidiBuffer m(midi);
        p.layerEditInFlight.store(skipped);
        p.processBlock(buf, m);
        p.layerEditInFlight.store(false);
    }

    juce::MidiBuffer events(std::initializer_list<juce::MidiMessage> msgs)
    {
        juce::MidiBuffer b;
        int frame = 0;
        for (const auto& m : msgs) b.addEvent(m, frame += 4);
        return b;
    }
}

struct MuTronicMidiInTests : juce::UnitTest
{
    MuTronicMidiInTests() : juce::UnitTest("mu-Tronic per-layer MIDI In and CPU meter", "mu-tronic-midi-in") {}

    void runTest() override
    {
        rowsAndRouting();
        carriedKeysFollowTheChannel();
        layerCreatedWhileKeysHeld();
        lateLayerKeepsPressOrder();
        changingTheChannelReleasesKeys();
        rowSavedAndMoves();
        cpuMeter();
    }

    void rowsAndRouting()
    {
        beginTest("each slot has a MIDI In row defaulting to Omni; two Arps on different channels hear only their own keys, Omni hears all");
        mu_seam_test::TempStorage storage;
        MidiInProcessor p;
        for (int i = 0; i < 16; ++i)
        {
            auto* row = p.apvts.getParameter("ch" + juce::String(i) + "_midiIn");
            expect(row != nullptr, "ch" + juce::String(i) + "_midiIn exists");
            if (row != nullptr) expectEquals(mu_seam_test::getParam(p.apvts, "ch" + juce::String(i) + "_midiIn"), 0.0f);
        }
        for (int s : { 0, 1, 2 }) expect(p.setLayerType(s, LayerTypeId::Arp));
        mu_seam_test::setParam(p.apvts, "ch0_midiIn", 1.0f);
        mu_seam_test::setParam(p.apvts, "ch1_midiIn", 2.0f);   // slot 2 stays Omni
        p.prepareToPlay(48000.0, 256);
        p.toggleInternalPlay();
        block(p, {});

        block(p, events({ juce::MidiMessage::noteOn(1, 60, (juce::uint8) 100), juce::MidiMessage::noteOn(2, 64, (juce::uint8) 100) }));
        expectEquals(arpIn(p, 0).heldNotes.numHeld(), 1);
        expectEquals(arpIn(p, 0).heldNotes.newest(), 60, "channel 1 hears its own key");
        expectEquals(arpIn(p, 1).heldNotes.numHeld(), 1);
        expectEquals(arpIn(p, 1).heldNotes.newest(), 64, "channel 2 hears its own key");
        expectEquals(arpIn(p, 2).heldNotes.numHeld(), 2, "Omni hears both");

        block(p, events({ juce::MidiMessage::noteOff(2, 64) }));
        expectEquals(arpIn(p, 0).heldNotes.numHeld(), 1, "a release on another channel does not touch it");
        expectEquals(arpIn(p, 1).heldNotes.numHeld(), 0);
        expectEquals(arpIn(p, 2).heldNotes.numHeld(), 1);

        mu_seam_test::setParam(p.apvts, "ch0_midiIn", 16.0f);   // channel 16 is the top bit of the mask
        block(p, events({ juce::MidiMessage::noteOn(16, 72, (juce::uint8) 100) }));
        expectEquals(arpIn(p, 0).heldNotes.numHeld(), 1, "the key held before the change is dropped; channel 16 is heard now");
        expectEquals(arpIn(p, 2).heldNotes.numHeld(), 2);
        p.flushPendingAsyncUpdates();
        p.releaseResources();
    }

    void carriedKeysFollowTheChannel()
    {
        beginTest("keys pressed and released in a block skipped for an edit reach only the layer on that channel, once");
        mu_seam_test::TempStorage storage;
        MidiInProcessor p;
        for (int s : { 0, 1 }) expect(p.setLayerType(s, LayerTypeId::Arp));
        mu_seam_test::setParam(p.apvts, "ch0_midiIn", 1.0f);
        mu_seam_test::setParam(p.apvts, "ch1_midiIn", 2.0f);
        p.prepareToPlay(48000.0, 256);
        p.toggleInternalPlay();
        block(p, events({ juce::MidiMessage::noteOn(1, 50, (juce::uint8) 100) }));
        expectEquals(arpIn(p, 0).heldNotes.newest(), 50);

        // The skipped block holds a press on channel 2, a press on channel 1 and the release of the held key.
        block(p, events({ juce::MidiMessage::noteOn(2, 61, (juce::uint8) 100), juce::MidiMessage::noteOn(1, 62, (juce::uint8) 100),
                          juce::MidiMessage::noteOff(1, 50) }), true);
        expectEquals(arpIn(p, 0).heldNotes.newest(), 50, "nothing rendered, so nothing is heard yet");
        block(p, {});
        expectEquals(arpIn(p, 0).heldNotes.numHeld(), 1, "channel 1: the old key is released, the new one held");
        expectEquals(arpIn(p, 0).heldNotes.newest(), 62);
        expectEquals(arpIn(p, 1).heldNotes.numHeld(), 1);
        expectEquals(arpIn(p, 1).heldNotes.newest(), 61, "channel 2 holds only its own carried key");
        block(p, {});
        expectEquals(arpIn(p, 0).heldNotes.numHeld(), 1, "a carried message is delivered once");
        p.flushPendingAsyncUpdates();
        p.releaseResources();
    }

    void layerCreatedWhileKeysHeld()
    {
        beginTest("a layer created while keys are held is handed them, per its MIDI In; a released key is not; a channel change picks up the new channel's keys");
        mu_seam_test::TempStorage storage;
        MidiInProcessor p;
        expect(p.setLayerType(0, LayerTypeId::Arp));
        p.prepareToPlay(48000.0, 256);
        p.toggleInternalPlay();
        block(p, {});
        block(p, events({ juce::MidiMessage::noteOn(1, 60, (juce::uint8) 100), juce::MidiMessage::noteOn(2, 64, (juce::uint8) 100) }));
        block(p, {});
        expectEquals(arpIn(p, 0).heldNotes.numHeld(), 2, "the layer that was there heard both");

        // Created after the presses: Omni, channel 2 and channel 5 (nothing held there).
        mu_seam_test::setParam(p.apvts, "ch2_midiIn", 2.0f);
        mu_seam_test::setParam(p.apvts, "ch3_midiIn", 5.0f);
        for (int s : { 1, 2, 3 }) expect(p.setLayerType(s, LayerTypeId::Arp));
        block(p, {});
        expectEquals(arpIn(p, 1).heldNotes.numHeld(), 2, "Omni: both held keys");
        expectEquals(arpIn(p, 2).heldNotes.numHeld(), 1, "channel 2: its own key");
        expectEquals(arpIn(p, 2).heldNotes.newest(), 64);
        expectEquals(arpIn(p, 3).heldNotes.numHeld(), 0, "channel 5: nothing is held there");

        // The slot's channel moves to 1: the key held there arrives, once.
        mu_seam_test::setParam(p.apvts, "ch3_midiIn", 1.0f);
        block(p, {});
        expectEquals(arpIn(p, 3).heldNotes.numHeld(), 1);
        expectEquals(arpIn(p, 3).heldNotes.newest(), 60);

        // A key let go before the layer exists is not handed over.
        block(p, events({ juce::MidiMessage::noteOff(2, 64) }));
        expect(p.setLayerType(4, LayerTypeId::Arp));
        block(p, {});
        expectEquals(arpIn(p, 4).heldNotes.numHeld(), 1, "only channel 1's key is still down");
        expectEquals(arpIn(p, 4).heldNotes.newest(), 60);
        block(p, events({ juce::MidiMessage::noteOff(1, 60) }));
        expectEquals(arpIn(p, 4).heldNotes.numHeld(), 0, "and its release reaches the seeded layer");
        p.flushPendingAsyncUpdates();
        p.releaseResources();
    }

    void lateLayerKeepsPressOrder()
    {
        beginTest("a late layer's newest held key is the last one pressed, as on a layer that was there (press order, not note order)");
        mu_seam_test::TempStorage storage;
        MidiInProcessor p;
        expect(p.setLayerType(0, LayerTypeId::Arp));
        p.prepareToPlay(48000.0, 256);
        p.toggleInternalPlay();
        block(p, {});
        for (int note : { 64, 60, 67, 62 })   // not ascending
            block(p, events({ juce::MidiMessage::noteOn(1, note, (juce::uint8) 100) }));
        block(p, events({ juce::MidiMessage::noteOff(1, 67) }));
        expectEquals(arpIn(p, 0).heldNotes.newest(), 62);
        expect(p.setLayerType(1, LayerTypeId::Arp));
        block(p, {});
        expectEquals(arpIn(p, 1).heldNotes.numHeld(), 3);
        expectEquals(arpIn(p, 1).heldNotes.newest(), arpIn(p, 0).heldNotes.newest(), "the same newest key (it drives the root override)");
        p.flushPendingAsyncUpdates();
        p.releaseResources();
    }

    void changingTheChannelReleasesKeys()
    {
        beginTest("changing a slot's MIDI In while a key is held drops the key: the release on the old channel is no longer heard");
        mu_seam_test::TempStorage storage;
        MidiInProcessor p;
        expect(p.setLayerType(0, LayerTypeId::Arp));
        expect(p.setLayerType(1, LayerTypeId::Arp));
        mu_seam_test::setParam(p.apvts, "ch0_midiIn", 1.0f);   // slot 1 starts Omni
        p.prepareToPlay(48000.0, 256);
        p.toggleInternalPlay();
        block(p, {});
        block(p, events({ juce::MidiMessage::noteOn(1, 60, (juce::uint8) 100) }));
        expectEquals(arpIn(p, 0).heldNotes.numHeld(), 1);
        expectEquals(arpIn(p, 1).heldNotes.numHeld(), 1);

        mu_seam_test::setParam(p.apvts, "ch0_midiIn", 2.0f);   // channel 1 to 2
        mu_seam_test::setParam(p.apvts, "ch1_midiIn", 3.0f);   // Omni to 3
        block(p, events({ juce::MidiMessage::noteOff(1, 60) }));
        expectEquals(arpIn(p, 0).heldNotes.numHeld(), 0, "channel 1 to 2: nothing left held");
        expectEquals(arpIn(p, 1).heldNotes.numHeld(), 0, "Omni to channel 3: nothing left held");

        // The signal is for the one call after the change: keys pressed afterwards are kept.
        block(p, events({ juce::MidiMessage::noteOn(2, 65, (juce::uint8) 100), juce::MidiMessage::noteOn(3, 67, (juce::uint8) 100) }));
        block(p, {});
        expectEquals(arpIn(p, 0).heldNotes.newest(), 65);
        expectEquals(arpIn(p, 1).heldNotes.newest(), 67);
        p.flushPendingAsyncUpdates();
        p.releaseResources();

        beginTest("the same for a Note mode Pattern: a key held on the old channel does not keep it sounding");
        mu_seam_test::TempStorage storage2;
        MidiInProcessor q;
        expect(q.setLayerType(1, LayerTypeId::Pattern));
        mu_seam_test::setParam(q.apvts, mu_tant::layerParamId(1, "gate_bypass"), 1.0f);
        expect(mu_tronic::setPatternNoteMode(q, 1, 1));
        mu_seam_test::setParam(q.apvts, "ch1_midiIn", 1.0f);

        struct PeakTap : MixerEngine::ChannelTap
        {
            void capture(int ch, const juce::AudioBuffer<float>& b, int n) noexcept override
            {
                if (ch != 1) return;
                for (int c = 0; c < 2 && c < b.getNumChannels(); ++c) peak = juce::jmax(peak, b.getMagnitude(c, 0, n));
            }
            float peak = 0.0f;
        } tap;
        q.setChannelTapForTest(&tap);
        q.setRateAndBufferSizeDetails(48000.0, 256);
        q.prepareToPlay(48000.0, 256);
        q.toggleInternalPlay();
        for (int i = 0; i < 20; ++i) block(q, {});
        block(q, events({ juce::MidiMessage::noteOn(1, 60, (juce::uint8) 100) }));
        for (int i = 0; i < 40; ++i) block(q, {});
        tap.peak = 0.0f;
        for (int i = 0; i < 10; ++i) block(q, {});
        expect(tap.peak > 0.01f, "the held key opens the Note Pattern");

        mu_seam_test::setParam(q.apvts, "ch1_midiIn", 2.0f);
        block(q, events({ juce::MidiMessage::noteOff(1, 60) }));
        for (int i = 0; i < 10; ++i) block(q, {});   // the 8 ms ramp has finished
        tap.peak = 0.0f;
        for (int i = 0; i < 20; ++i) block(q, {});
        expectEquals(tap.peak, 0.0f, "the Pattern is silent: the key was dropped with the channel change");
        q.setChannelTapForTest(nullptr);
        q.flushPendingAsyncUpdates();
        q.releaseResources();
    }

    void rowSavedAndMoves()
    {
        beginTest("the MIDI In row is saved with the session and travels with its layer through a swap");
        mu_seam_test::TempStorage storage;
        MidiInProcessor a;
        expect(a.setLayerType(2, LayerTypeId::Arp));
        mu_seam_test::setParam(a.apvts, "ch2_midiIn", 5.0f);
        mu_seam_test::setParam(a.apvts, "ch9_midiIn", 16.0f);   // the strip of an Empty slot keeps it too
        juce::MemoryBlock saved;
        a.getStateInformation(saved);

        mu_seam_test::TempStorage storage2;
        MidiInProcessor b;
        b.setStateInformation(saved.getData(), (int) saved.getSize());
        expectEquals(mu_seam_test::getParam(b.apvts, "ch2_midiIn"), 5.0f);
        expectEquals(mu_seam_test::getParam(b.apvts, "ch9_midiIn"), 16.0f);
        expectEquals(mu_seam_test::getParam(b.apvts, "ch3_midiIn"), 0.0f);

        expect(b.swapLayers(2, 7));
        expectEquals(mu_seam_test::getParam(b.apvts, "ch7_midiIn"), 5.0f, "the channel moved with the layer");
        expectEquals(mu_seam_test::getParam(b.apvts, "ch2_midiIn"), 0.0f, "and the slot it left is Omni again");

        b.prepareToPlay(48000.0, 256);
        b.toggleInternalPlay();
        block(b, {});
        block(b, events({ juce::MidiMessage::noteOn(5, 60, (juce::uint8) 100), juce::MidiMessage::noteOn(6, 62, (juce::uint8) 100) }));
        expectEquals(arpIn(b, 7).heldNotes.numHeld(), 1, "after the swap the layer in slot 7 still hears channel 5 only");
        expectEquals(arpIn(b, 7).heldNotes.newest(), 60);
        b.flushPendingAsyncUpdates();
        b.releaseResources();
    }

    void cpuMeter()
    {
        beginTest("the load meter reports busy time over block time, smoothed; the peak is the worst figure of the last second");
        using M = mu_core::CpuLoadMeter;
        M m;
        expectEquals(m.load(), 0.0f);
        const double blockSec = 256.0 / 48000.0;
        for (int i = 0; i < 400; ++i) m.accumulate(blockSec * 0.25, blockSec);   // about 2 s at a steady 25 percent
        for (int i = 0; i < 1600; ++i) m.accumulate(blockSec * 0.25, blockSec);
        expectWithinAbsoluteError(m.load(), 0.25f, 0.005f);
        expectWithinAbsoluteError(m.peak(), 0.25f, 1.0e-3f);

        // A spike: the peak takes it at once, holds it for one second, then falls to the current figure.
        m.accumulate(blockSec * 0.9, blockSec);
        expectWithinAbsoluteError(m.peak(), 0.9f, 1.0e-3f, "one slow block sets the peak at once");
        expect(m.load() < 0.3f, "and moves the smoothed load only a little");
        double elapsed = 0.0;
        bool fell = false;
        while (elapsed < 1.5 && ! fell)
        {
            m.accumulate(blockSec * 0.1, blockSec);
            elapsed += blockSec;
            fell = m.peak() < 0.5f;
            if (! fell) expectWithinAbsoluteError(m.peak(), 0.9f, 1.0e-3f, "held until the second is up");
        }
        expect(fell, "the peak falls");
        expectWithinAbsoluteError(elapsed, M::kPeakHoldSeconds, 2.0 * blockSec, "after one second, give or take a block");
        expect(m.peak() < 0.15f, "to the current figure");

        beginTest("blocks of a single frame are summed until they cover enough time: no thousand percent readings");
        M tiny;
        const double frameSec = 1.0 / 48000.0;
        for (int i = 0; i < 48000; ++i)
            tiny.accumulate(i == 24000 ? 0.0005 : frameSec * 0.2, frameSec);   // one frame that took half a millisecond
        expect(tiny.load() < 1.0f && tiny.peak() < 1.0f, "load " + juce::String(tiny.load(), 3) + " peak " + juce::String(tiny.peak(), 3));
        tiny.accumulate(0.01, 0.0);   // a block of no length is ignored
        expect(std::isfinite(tiny.load()) && std::isfinite(tiny.peak()));
        tiny.reset();
        expectEquals(tiny.load(), 0.0f);
        expectEquals(tiny.peak(), 0.0f);

        // The processor measures its own blocks: after some work the load is a real, finite fraction.
        mu_seam_test::TempStorage storage;
        MidiInProcessor p;
        for (int s = 0; s < 4; ++s) expect(p.setLayerType(s, LayerTypeId::Arp));
        p.prepareToPlay(48000.0, 256);
        p.toggleInternalPlay();
        expectEquals(p.getCpuLoad(), 0.0f, "reset by prepareToPlay");
        for (int i = 0; i < 200; ++i) block(p, {});
        expect(p.getCpuLoad() > 0.0f && p.getCpuLoad() < 100.0f && std::isfinite(p.getCpuLoad()), "a measured load " + juce::String(p.getCpuLoad(), 5));
        expect(std::isfinite(p.getCpuPeak()) && p.getCpuPeak() >= 0.0f);
        p.flushPendingAsyncUpdates();
        p.releaseResources();
    }
};

static MuTronicMidiInTests muTronicMidiInTests;
