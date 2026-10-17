// mu_midi note tests on the raw bytes of a MidiBuffer event, and the audio-thread MIDI scans that use them.
//
// Building a juce::MidiMessage from a buffer event allocates for messages over 8 bytes (SysEx), which must never
// happen inside processBlock. The raw-byte tests give JUCE's meaning (velocity 0 is not a Note On, it is a Note Off)
// without building a message. The allocation check feeds a 16-byte SysEx through MidiClockSync::process, one of the
// shared scans.

#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>

#include "Plugin/MidiRaw.h"
#include "Plugin/MidiClockSync.h"
// The counting operators are carried by MidiControlTests.cpp (one translation unit per executable).
#include "AllocationProbe.h"

struct MidiRawTests : juce::UnitTest
{
    MidiRawTests() : juce::UnitTest("MidiRaw note tests and allocation-free MIDI scans", "mu-core") {}

    void runTest() override
    {
        beginTest("isNoteOn / isNoteOff / noteNumber follow JUCE's meaning on raw bytes");
        {
            struct Case { const char* name; juce::uint8 bytes[16]; int numBytes; bool on, off; int note; };
            const Case cases[] = {
                { "Note On, velocity 0 (a Note Off)",   { 0x90, 60, 0 },   3, false, true,  60 },
                { "Note On, velocity > 0",              { 0x91, 61, 100 }, 3, true,  false, 61 },
                { "Note Off",                           { 0x80, 62, 64 },  3, false, true,  62 },
                { "Note Off on channel 16",             { 0x8F, 63, 0 },   3, false, true,  63 },
                { "truncated Note On (2 bytes)",        { 0x90, 60 },      2, false, false, -1 },
                { "single status byte",                 { 0x90 },          1, false, false, -1 },
                { "Control Change (no running status)", { 0xB0, 7, 100 },  3, false, false, -1 },
                { "Poly pressure",                      { 0xA0, 60, 90 },  3, false, false, -1 },
                { "Clock",                              { 0xF8 },          1, false, false, -1 },
            };
            for (const auto& c : cases)
            {
                const juce::MidiMessageMetadata meta(c.bytes, c.numBytes, 0);
                expect(mu_midi::isNoteOn(meta) == c.on, juce::String(c.name) + ": isNoteOn");
                expect(mu_midi::isNoteOff(meta) == c.off, juce::String(c.name) + ": isNoteOff");
                if (c.on || c.off)
                    expectEquals(mu_midi::noteNumber(meta), c.note, juce::String(c.name) + ": noteNumber");
            }
        }

        beginTest("a 16-byte SysEx is neither a Note On nor a Note Off");
        {
            const juce::uint8 payload[14] = { 0x7D, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13 };
            const auto sysex = juce::MidiMessage::createSysExMessage(payload, 14);   // F0 ... F7: 16 bytes
            expectEquals(sysex.getRawDataSize(), 16);
            const juce::MidiMessageMetadata meta(sysex.getRawData(), sysex.getRawDataSize(), 0);
            expect(! mu_midi::isNoteOn(meta) && ! mu_midi::isNoteOff(meta), "SysEx is not a note message");
        }

        beginTest("the raw tests agree with juce::MidiMessage on every status byte and velocity");
        {
            // Sweep the 3-byte channel-voice statuses (0x80..0xEF without Program Change and Channel Pressure, which
            // are 2 bytes long), velocity 0 and above.
            int disagreements = 0;
            for (int status = 0x80; status <= 0xEF; ++status)
                for (const int velocity : { 0, 1, 64, 127 })
                {
                    if ((status & 0xF0) == 0xC0 || (status & 0xF0) == 0xD0)
                        continue;
                    const juce::uint8 bytes[3] = { (juce::uint8) status, 60, (juce::uint8) velocity };
                    const juce::MidiMessage message(bytes, 3);
                    const juce::MidiMessageMetadata meta(bytes, 3, 0);
                    if (message.isNoteOn() != mu_midi::isNoteOn(meta) || message.isNoteOff() != mu_midi::isNoteOff(meta))
                        ++disagreements;
                }
            expectEquals(disagreements, 0, "status/velocity pairs where the raw tests differ from juce::MidiMessage");
        }

        beginTest("MidiClockSync::process allocates nothing on a block that carries a 16-byte SysEx");
        {
            const juce::uint8 payload[14] = { 0x7D, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13 };
            const auto sysex = juce::MidiMessage::createSysExMessage(payload, 14);

            // Start, a few clock pulses and the SysEx between them, built before the probe is armed.
            juce::MidiBuffer midi;
            midi.addEvent(juce::MidiMessage(0xFA), 0);
            midi.addEvent(juce::MidiMessage(0xF8), 0);
            midi.addEvent(sysex, 40);
            midi.addEvent(juce::MidiMessage(0xF8), 100);
            midi.addEvent(sysex, 150);
            midi.addEvent(juce::MidiMessage(0xF8), 200);

            // Positive control: the probe sees the allocation of the call the scans used to make. On a build where the
            // probe cannot count it (it hooks plain malloc only on the MSVC debug CRT) the control reads 0 and the
            // check below proves nothing there; that is logged rather than hidden.
            int controlAllocs = 0;
            {
                mu_alloc::Armed region;
                for (const auto meta : midi) { auto m = meta.getMessage(); juce::ignoreUnused(m); }
                region.stop();
                controlAllocs = region.allocs();
            }

            MidiClockSync sync;
            sync.setEnabled(true);
            sync.setMessages(2);
            // Warm-up block: the first block after enabling may set up state; it is not the audio-thread steady state.
            { juce::MidiBuffer none; sync.process(none, 512, 48000.0); }

            int allocs = -1;
            {
                mu_alloc::Armed region;
                sync.process(midi, 512, 48000.0);
                region.stop();
                allocs = region.allocs();
            }
            logMessage("allocations: building a message from a SysEx event " + juce::String(controlAllocs)
                       + ", MidiClockSync::process " + juce::String(allocs));
            if (controlAllocs == 0)
                logMessage("the probe does not count the message construction on this build: this check is vacuous here");
            expectEquals(allocs, 0, "MidiClockSync::process allocated on a SysEx block");
            expect(sync.isPlaying(), "the Start in the block was still handled");
        }
    }
};

static MidiRawTests midiRawTests;
