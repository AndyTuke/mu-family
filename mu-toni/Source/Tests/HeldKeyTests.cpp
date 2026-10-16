// The held keys (root-by-MIDI, MIDI-trigger) across structural edits, unrendered blocks and count changes,
// on the REAL headless PluginProcessor. Written on the tree where the held-note stack was one processor-wide
// stack, and kept unchanged after it became each layer's own state: a layer that joins (added, or regrown by
// a state load) hears the keys already down, and a key that arrives while an edit silences the audio still
// counts, but its press is not a fresh trigger.

#include "Plugin/PluginProcessor.h"
#include "Tests/GoldenRender.h"
#include "Tests/ProcessorSeamHelpers.h"

namespace
{
    juce::MidiMessage keyOn(int note)  { return juce::MidiMessage::noteOn(1, note, (juce::uint8) 100); }
    juce::MidiMessage keyOff(int note) { return juce::MidiMessage::noteOff(1, note, (juce::uint8) 0); }

    // The real processor driven block by block; 48 kHz, 120 bpm, playing.
    struct KeyRig
    {
        KeyRig()
        {
            host.bpm = 120.0;
            p.setPlayHead(&host);
            p.setRateAndBufferSizeDetails(48000.0, 512);
            p.prepareToPlay(48000.0, 512);
        }
        ~KeyRig() { p.setPlayHead(nullptr); p.releaseResources(); }

        // One 512-frame block with `midi` at frame 0; the hash of its output.
        std::uint64_t block(std::vector<juce::MidiMessage> midi = {})
        {
            host.timeInSamples = frame;
            juce::AudioBuffer<float> buf(2, 512);
            buf.clear();
            juce::MidiBuffer mb;
            for (const auto& m : midi) mb.addEvent(m, 0);
            p.processBlock(buf, mb);
            mu_golden::Fnv64 h;
            for (int c = 0; c < 2; ++c)
                for (int i = 0; i < 512; ++i) h.f32(buf.getSample(c, i));
            host.ppq += 512.0 * host.bpm / (60.0 * 48000.0);
            frame += 512;
            return h.h;
        }

        void set(const juce::String& id, float plain) { mu_seam_test::setParam(p.apvts, id, plain); }
        void voice(int layer, int rate)
        {
            const juce::String pre = mu_toni::layerParamPrefix(layer);
            set(pre + "rate", (float) rate);
            set(pre + "gate", 70.0f);
        }

        mu_toni::PluginProcessor p;
        mu_golden::MockHost host;
        juce::int64 frame = 0;
    };

    juce::MemoryBlock stateOf(mu_toni::PluginProcessor& p)
    {
        juce::MemoryBlock mb;
        p.getStateInformation(mb);
        return mb;
    }
}

struct MuToniHeldKeyTests : juce::UnitTest
{
    MuToniHeldKeyTests() : juce::UnitTest("mu-Toni held keys across edits", "mu-toni-heldkeys") {}

    void runTest() override
    {
        mu_seam_test::TempStorage storage;
        mu_seam_test::ScopedEnvVar deterministic("MU_DETERMINISTIC_NOISE", "1");

        // G4 (MIDI 67) as the root a held key gives: pitch class 7, octave 4.
        constexpr int kKey = 67, kKeyRoot = 7, kKeyOct = 4;

        beginTest("A layer added while a key is held plays on that key's root at once");
        {
            KeyRig midiRig, paramRig;
            for (auto* r : { &midiRig, &paramRig }) r->voice(0, 6);
            paramRig.set("a0_root", (float) kKeyRoot);  paramRig.set("a0_roct", (float) kKeyOct);

            bool same = midiRig.block({ keyOn(kKey) }) == paramRig.block();
            for (int b = 0; b < 3; ++b) same = midiRig.block() == paramRig.block() && same;
            expect(same, "the held key drives layer 1 before the edit");

            for (auto* r : { &midiRig, &paramRig }) { r->p.addLayer(); r->voice(1, 9); }
            paramRig.set("a1_root", (float) kKeyRoot);  paramRig.set("a1_roct", (float) kKeyOct);
            for (int b = 0; b < 16; ++b) same = midiRig.block() == paramRig.block() && same;
            expect(same, "the added layer hears the key that was already down");
        }

        beginTest("A layer regrown by a state load hears the keys pressed and released while it was not live");
        {
            KeyRig midiRig, paramRig;
            for (auto* r : { &midiRig, &paramRig }) { r->p.addLayer(); r->voice(0, 6); r->voice(1, 9); }
            const auto twoLayers = stateOf(midiRig.p);
            for (auto* r : { &midiRig, &paramRig }) r->p.removeLayer(1);

            // While layer 2 is gone: a key down (C4), then a second key down and the first released. The
            // parameter rig follows on its root parameters, so layer 1 sounds the same in both meanwhile.
            paramRig.set("a0_root", 0.0f);  paramRig.set("a0_roct", 4.0f);
            midiRig.block({ keyOn(60) });  paramRig.block();
            paramRig.set("a0_root", (float) kKeyRoot);  paramRig.set("a0_roct", (float) kKeyOct);
            midiRig.block({ keyOn(kKey), keyOff(60) });  paramRig.block();

            for (auto* r : { &midiRig, &paramRig }) r->p.setStateInformation(twoLayers.getData(), (int) twoLayers.getSize());
            expectEquals(midiRig.p.getNumLayers(), 2);
            for (const char* l : { "a0_", "a1_" })
            {
                paramRig.set(juce::String(l) + "root", (float) kKeyRoot);
                paramRig.set(juce::String(l) + "roct", (float) kKeyOct);
            }
            bool same = true;
            for (int b = 0; b < 16; ++b) same = midiRig.block() == paramRig.block() && same;
            expect(same, "both layers play on the key still held, the regrown one included");
        }

        beginTest("A key pressed in a block an edit silenced is held afterwards, but does not restart a MIDI-trigger pattern");
        {
            KeyRig pressed, quiet;
            for (auto* r : { &pressed, &quiet }) { r->voice(0, 6); r->set("a0_trig", 1.0f); }
            for (auto* r : { &pressed, &quiet })
            {
                r->block({ keyOn(60) });
                for (int b = 0; b < 9; ++b) r->block();
            }
            for (auto* r : { &pressed, &quiet }) r->p.layerEditInFlight.store(true);
            pressed.block({ keyOn(kKey) });
            quiet.block();
            for (auto* r : { &pressed, &quiet }) r->p.layerEditInFlight.store(false);
            for (auto* r : { &pressed, &quiet }) r->block();

            expectEquals(pressed.p.getArp(0).runner.lastStep(), quiet.p.getArp(0).runner.lastStep(),
                         "the pattern runs on from where it was (a press in an unrendered block is no fresh trigger)");
            expectEquals(pressed.p.getArp(0).runner.stepsFired(), quiet.p.getArp(0).runner.stepsFired());

            // ...but the key is held: releasing the first key keeps the pattern running on the second.
            for (auto* r : { &pressed, &quiet }) r->block({ keyOff(60) });
            const int firedP = pressed.p.getArp(0).runner.stepsFired(), firedQ = quiet.p.getArp(0).runner.stepsFired();
            for (auto* r : { &pressed, &quiet }) for (int b = 0; b < 14; ++b) r->block();
            expect(pressed.p.getArp(0).runner.stepsFired() > firedP, "the key pressed during the edit still holds the arp on");
            expectEquals(quiet.p.getArp(0).runner.stepsFired(), firedQ, "with no key left the arp stops");
        }
    }
};

static MuToniHeldKeyTests muToniHeldKeyTests;
