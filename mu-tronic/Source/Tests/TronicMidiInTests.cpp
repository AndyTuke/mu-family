// Per-layer MIDI In in mu-Tronic (docs/mu-tronic/design.md T8a): each slot's strip carries a `ch{N}_midiIn` row (0 Omni,
// 1-16), the shared block driver turns it into the channel mask of that slot's MIDI view, and a layer that follows the
// keys (the Arp's held stack) hears only its channel. The row moves with its layer, is saved with the session, and the
// keys of a block skipped for an edit reach the layer on the channel it listens to. Also the processing-load meter.

#include "Plugin/PluginProcessor.h"
#include "Plugin/CpuLoadMeter.h"
#include "Sequencer/Arp.h"
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
        expectEquals(arpIn(p, 0).heldNotes.numHeld(), 2, "the key held before the change stays; channel 16 is heard now");
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
        beginTest("the load meter reports busy time over block time, smoothed, with a falling peak");
        mu_core::CpuLoadMeter m;
        expectEquals(m.load(), 0.0f);
        const double blockSec = 256.0 / 48000.0;
        for (int i = 0; i < 2000; ++i) m.update(blockSec * 0.25, blockSec);   // about 10 s at a steady 25 percent
        expectWithinAbsoluteError(m.load(), 0.25f, 0.005f);
        expect(m.peak() >= m.load() - 1.0e-4f && m.peak() < 0.3f);
        m.update(blockSec * 0.9, blockSec);
        expectWithinAbsoluteError(m.peak(), 0.9f, 1.0e-4f, "one slow block sets the peak at once");
        expect(m.load() < 0.3f, "and moves the smoothed load only a little");
        for (int i = 0; i < 4000; ++i) m.update(blockSec * 0.1, blockSec);    // about 20 s of light load
        expect(m.peak() < 0.15f, "the peak falls back");
        m.reset();
        expectEquals(m.load(), 0.0f);

        // The processor measures its own blocks: after some work the load is a real, finite fraction.
        mu_seam_test::TempStorage storage;
        MidiInProcessor p;
        for (int s = 0; s < 4; ++s) expect(p.setLayerType(s, LayerTypeId::Arp));
        p.prepareToPlay(48000.0, 256);
        p.toggleInternalPlay();
        expectEquals(p.getCpuLoad(), 0.0f, "reset by prepareToPlay");
        for (int i = 0; i < 200; ++i) block(p, {});
        expect(p.getCpuLoad() > 0.0f && p.getCpuLoad() < 100.0f && std::isfinite(p.getCpuLoad()), "a measured load " + juce::String(p.getCpuLoad(), 5));
        expect(p.getCpuPeak() >= p.getCpuLoad() - 1.0e-4f);
        p.flushPendingAsyncUpdates();
        p.releaseResources();
    }
};

static MuTronicMidiInTests muTronicMidiInTests;
