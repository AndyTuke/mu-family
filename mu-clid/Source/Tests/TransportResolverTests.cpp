// Family transport rule tests: mu-core resolveTransport (TransportResolver.h).
// Checks which source each block's play state, tempo and beat come from — a host with a
// position, a host without one, standalone MIDI clock in its three Messages modes, and the
// product's own transport — and that the Play button mirror and beat carry follow the source.

#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include "Plugin/TransportResolver.h"

class TransportResolverTest : public juce::UnitTest
{
public:
    TransportResolverTest() : juce::UnitTest ("Transport resolver", "Transport") {}

    static constexpr double kSr    = 48000.0;
    static constexpr int    kBlock = 480;   // 10 ms

    // The product's own transport atomics, as a product holds them.
    struct Own
    {
        std::atomic<bool>   playing { false };
        std::atomic<double> bpm     { 100.0 };
        std::atomic<double> beat    { 0.0 };
        mu_core::InternalTransport ref() { return { playing, bpm, beat }; }
    };

    // One standalone block: feed `midi` to the clock, then resolve.
    static mu_core::BlockTransport block(MidiClockSync& clock, Own& own, juce::MidiBuffer midi = {})
    {
        const double clockBeat = clock.process(midi, kBlock, kSr);
        return mu_core::resolveTransport({}, true, clock, clockBeat, own.ref(), kBlock, kSr);
    }

    static juce::MidiBuffer msgs(std::initializer_list<int> bytes)
    {
        juce::MidiBuffer m;
        int at = 0;
        for (int b : bytes) m.addEvent(juce::MidiMessage((juce::uint8) b), at++);
        return m;
    }

    void runTest() override
    {
        beginTest ("Host with a position: play, tempo and beat follow it; Play mirrors it");
        {
            MidiClockSync clock;
            Own own;
            mu_core::HostTransport host { true, 140.0, true, 8.0 };
            const auto t = mu_core::resolveTransport(host, false, clock, 0.0, own.ref(), kBlock, kSr);
            expect (t.playing, "host playing");
            expectEquals (t.bpm, 140.0);
            expectEquals (t.startBeat, 8.0);
            expect (own.playing.load(), "Play button mirrors the host");
            expect (own.beat.load() > 8.0, "beat carried on past the block");
        }

        beginTest ("Host without a position: play + tempo follow it, the own beat runs on");
        {
            MidiClockSync clock;
            Own own;
            own.beat = 3.0;
            mu_core::HostTransport host { true, 90.0, false, 0.0 };
            const auto t = mu_core::resolveTransport(host, false, clock, 0.0, own.ref(), kBlock, kSr);
            expect (t.playing);
            expectEquals (t.bpm, 90.0);
            expectEquals (t.startBeat, 3.0);
            expectWithinAbsoluteError (own.beat.load(), 3.0 + 90.0 / 60.0 * kBlock / kSr, 1.0e-12);
        }

        beginTest ("Clock + transport: the clock owns play, tempo and beat");
        {
            MidiClockSync clock;
            clock.setEnabled(true);
            clock.setMessages(2);
            Own own;
            auto t = block(clock, own, msgs({ 0xFA, 0xF8, 0xF8, 0xF8, 0xF8, 0xF8, 0xF8 }));
            expect (t.playing, "Start plays");
            expect (own.playing.load(), "Play button mirrors the clock");
            t = block(clock, own);
            expectEquals (t.startBeat, 0.25, "6 pulses = one 16th");
            t = block(clock, own, msgs({ 0xFC }));
            t = block(clock, own);
            expect (! t.playing, "Stop stops");
        }

        beginTest ("Clock only: the Play button starts it, the clock ticks give the beat");
        {
            MidiClockSync clock;
            clock.setEnabled(true);
            clock.setMessages(0);
            Own own;
            auto t = block(clock, own, msgs({ 0xF8, 0xF8, 0xF8, 0xF8, 0xF8, 0xF8 }));
            expect (! t.playing, "not playing until Play is pressed");
            own.playing = true;                       // the user presses Play
            t = block(clock, own, msgs({ 0xFA }));    // a Start is ignored in this mode
            expect (t.playing, "Play button is not overwritten by the clock");
            expect (own.playing.load(), "Play button stays on");
            expectEquals (t.startBeat, 0.25, "beat from the ticks, Start ignored");
            own.playing = false;
            t = block(clock, own);
            expect (! t.playing, "the Play button stops it");
        }

        beginTest ("Transport only: Start / Stop drive play; the BPM field drives tempo + beat");
        {
            MidiClockSync clock;
            clock.setEnabled(true);
            clock.setMessages(1);
            Own own;
            own.bpm  = 150.0;
            own.beat = 5.0;
            auto t = block(clock, own, msgs({ 0xFA, 0xF8, 0xF8 }));
            expect (t.playing, "Start plays");
            expectEquals (t.bpm, 150.0, "tempo from the BPM field");
            expectEquals (t.startBeat, 0.0, "Start restarts the own beat at bar 1");
            const double perBlock = 150.0 / 60.0 * kBlock / kSr;
            t = block(clock, own);
            expectWithinAbsoluteError (t.startBeat, perBlock, 1.0e-12, "the own beat advances");
            t = block(clock, own, msgs({ 0xFC }));
            expect (! t.playing, "Stop stops");
            expect (! own.playing.load(), "Play button mirrors the Stop");
            const double held = own.beat.load();
            t = block(clock, own, msgs({ 0xFB }));
            expect (t.playing, "Continue plays");
            expectWithinAbsoluteError (t.startBeat, held, 1.0e-12, "Continue resumes where it stopped");
        }

        beginTest ("Clock + transport: a Start plays from bar 1, not from the old count");
        {
            MidiClockSync clock;
            clock.setEnabled(true);
            clock.setMessages(2);
            Own own;
            juce::MidiBuffer run = msgs({ 0xFA });
            for (int i = 0; i < 30; ++i) run.addEvent(juce::MidiMessage((juce::uint8) 0xF8), 1 + i);
            block(clock, own, run);
            block(clock, own, msgs({ 0xFC }));
            juce::MidiBuffer stopped;
            for (int i = 0; i < 30; ++i) stopped.addEvent(juce::MidiMessage((juce::uint8) 0xF8), i);
            auto t = block(clock, own, stopped);
            expectEquals (t.startBeat, 30.0 / 24.0, "pulses while stopped don't move the song");
            t = block(clock, own, msgs({ 0xFA, 0xF8 }));
            expect (t.playing);
            expectEquals (t.startBeat, 0.0, "the Start block plays from bar 1");
        }

        beginTest ("Switching Messages mode mid-play keeps the master's position");
        {
            MidiClockSync clock;
            clock.setEnabled(true);
            clock.setMessages(1);
            Own own;
            juce::MidiBuffer run = msgs({ 0xFA });
            for (int i = 0; i < 48; ++i) run.addEvent(juce::MidiMessage((juce::uint8) 0xF8), 1 + i);
            block(clock, own, run);
            clock.setMessages(2);
            const auto t = block(clock, own);
            expectEquals (t.startBeat, 2.0, "48 pulses counted while ticks were off");
        }

        beginTest ("Ticks mode with no clock heard yet: tempo falls back to the BPM field");
        {
            MidiClockSync clock;
            clock.setEnabled(true);
            clock.setMessages(0);
            Own own;
            own.playing = true;
            const auto t = block(clock, own);
            expectEquals (t.bpm, 100.0, "no estimate yet, so the BPM field's tempo");
        }

        beginTest ("Song Position Pointer: a locate while stopped, then Continue, plays from there");
        {
            auto spp = [](int sixteenths)
            {
                juce::MidiBuffer m;
                m.addEvent(juce::MidiMessage::songPositionPointer(sixteenths), 0);
                return m;
            };
            for (const int mode : { 2, 1 })
            {
                MidiClockSync clock;
                clock.setEnabled(true);
                clock.setMessages(mode);
                Own own;
                const auto label = "mode " + juce::String(mode);
                block(clock, own, msgs({ 0xFA, 0xF8, 0xF8, 0xF8 }));
                block(clock, own, msgs({ 0xFC }));
                auto t = block(clock, own, spp(16));        // locate to beat 4 while stopped
                expect (! t.playing, label + ": still stopped after the locate");
                expectEquals (t.startBeat, 4.0, label + ": the locate block sits at the new position");
                t = block(clock, own, msgs({ 0xFB }));       // Continue
                expect (t.playing, label + ": Continue plays");
                expectEquals (t.startBeat, 4.0, label + ": Continue resumes at the located beat");
            }

            MidiClockSync clock;
            clock.setEnabled(true);
            clock.setMessages(2);
            Own own;
            juce::MidiBuffer run = msgs({ 0xFA });
            for (int i = 0; i < 12; ++i) run.addEvent(juce::MidiMessage((juce::uint8) 0xF8), 1 + i);
            block(clock, own, run);
            const auto t = block(clock, own, spp(64));
            expectEquals (t.startBeat, 0.5, "a locate while playing is ignored");
        }

        beginTest ("Own transport: nothing outside touches the Play button");
        {
            MidiClockSync clock;   // sync off
            Own own;
            own.playing = true;
            const auto t = mu_core::resolveTransport({}, true, clock, 0.0, own.ref(), kBlock, kSr);
            expect (t.playing);
            expectEquals (t.bpm, 100.0);
            expect (own.playing.load(), "Play button left alone");
        }
    }
};

static TransportResolverTest transportResolverTest;
