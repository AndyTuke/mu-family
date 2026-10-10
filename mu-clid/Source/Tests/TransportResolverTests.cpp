// Family transport rule tests: mu-core resolveTransport (TransportResolver.h).
// Checks which source each block's play state, tempo and beat come from — a host with a
// position, a host without one, standalone MIDI clock in its three Messages modes, and the
// product's own transport — and that the Play button mirror and beat carry follow the source.

#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include "Plugin/TransportResolver.h"
#include "Plugin/TapTempo.h"
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
            expectEquals (t.startBeat, 5.0 / 24.0, "six clocks, the first being tick 0, reach pulse 5");
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
            expectEquals (t.startBeat, 5.0 / 24.0, "beat from the ticks (six clocks, the first being tick 0), Start ignored");
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
            expectEquals (t.startBeat, 47.0 / 24.0, "48 clocks counted while ticks were off (the first being tick 0)");
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
            expectEquals (t.startBeat, 11.0 / 24.0, "a locate while playing is ignored (12 clocks seen, the first being tick 0)");
        }

        beginTest ("A lost clock holds the transport stopped until pulses return");
        {
            MidiClockSync clock;
            clock.setEnabled(true);
            clock.setMessages(2);
            Own own;
            block(clock, own);
            expect (clock.getClockState() == MidiClockSync::ClockState::Waiting, "waiting before the first pulse");

            // 120 BPM: a pulse every 1000 samples, so one per 10 ms block on average.
            auto t = block(clock, own, msgs({ 0xFA, 0xF8 }));
            for (int b = 0; b < 20; ++b) t = block(clock, own, msgs({ 0xF8 }));
            expect (t.playing && clock.getClockState() == MidiClockSync::ClockState::Locked, "locked and playing");

            for (int b = 0; b < 20; ++b) t = block(clock, own);   // 200 ms of silence: still inside the window
            expect (t.playing, "a short gap is not a loss");
            for (int b = 0; b < 10; ++b) t = block(clock, own);   // 300 ms
            expect (! t.playing, "lost: stopped");
            expect (clock.getClockState() == MidiClockSync::ClockState::Lost);

            t = block(clock, own, msgs({ 0xF8 }));
            expect (t.playing && clock.getClockState() == MidiClockSync::ClockState::Locked,
                    "pulses back while the master still runs: playing again");
        }

        beginTest ("Silence after a Stop is waiting, and a Start restarts the loss timer");
        {
            MidiClockSync clock;
            clock.setEnabled(true);
            clock.setMessages(2);
            Own own;
            auto t = block(clock, own, msgs({ 0xFA, 0xF8 }));
            for (int b = 0; b < 10; ++b) t = block(clock, own, msgs({ 0xF8 }));
            t = block(clock, own, msgs({ 0xFC }));
            for (int b = 0; b < 50; ++b) t = block(clock, own);   // the master goes quiet while stopped
            expect (clock.getClockState() == MidiClockSync::ClockState::Waiting, "stopped and quiet: waiting, not lost");

            t = block(clock, own, msgs({ 0xFA }));                // Start, first pulse in the next block
            expect (t.playing, "the Start block plays");
            t = block(clock, own, msgs({ 0xF8 }));
            expect (t.playing && clock.getClockState() == MidiClockSync::ClockState::Locked);
        }

        beginTest ("Small blocks under a jittery clock: the beat never runs backwards or overlaps");
        {
            // A sequencer evaluates positions across each block edge (mu-Tant's gates per sample from
            // the block's start beat), so a block must not start before the previous one's end.
            for (const int blockSize : { 32, 64, 480 })
            {
                MidiClockSync clock;
                clock.setEnabled(true);
                clock.setMessages(2);
                const double tick = 1000.0;                               // 120 BPM at 48 kHz
                juce::int64 n = 0, start = 0;
                double prevStart = -1.0, prevEnd = -1.0, worstOverlap = 0.0;
                bool backwards = false;
                for (int b = 0; b < 6000; ++b, start += blockSize)
                {
                    juce::MidiBuffer midi;
                    if (b == 0) midi.addEvent(juce::MidiMessage((juce::uint8) 0xFA), 0);
                    for (;; ++n)
                    {
                        const int j = (int) ((n * 7919) % 81) - 40;       // +/- 40 samples of jitter
                        const juce::int64 at = juce::jmax<juce::int64>(0, (juce::int64) (n * tick) + j);
                        if (at >= start + blockSize) break;
                        midi.addEvent(juce::MidiMessage((juce::uint8) 0xF8), (int) juce::jmax<juce::int64>(0, at - start));
                    }
                    const double s = clock.process(midi, blockSize, kSr);
                    if (b > 60)
                    {
                        if (s < prevStart) backwards = true;
                        worstOverlap = juce::jmax(worstOverlap, prevEnd - s);
                    }
                    prevStart = s;
                    prevEnd   = clock.getBlockEndBeat();   // where the next block starts, exactly
                }
                const auto label = "block " + juce::String(blockSize);
                expect (! backwards, label + ": a block started before the previous one");
                expectLessOrEqual (worstOverlap, 1.0e-12, label + ": overlap with the previous block's end");
            }
        }

        beginTest ("Sync offset: under MIDI clock the beat is advanced by offset x tempo, not elsewhere");
        {
            // A settled 120 BPM clock (a pulse every 1000 samples at 48 kHz).
            MidiClockSync clock;
            Own own;
            clock.setEnabled(true);
            clock.setMessages(2);
            block(clock, own, msgs({ 0xFA }));
            for (int b = 0; b < 200; ++b)
            {
                juce::MidiBuffer m;
                m.addEvent(juce::MidiMessage((juce::uint8) 0xF8), (b * 480) % 1000 < 480 ? (b * 480) % 1000 : 0);
                clock.process(m, kBlock, kSr);   // keep the tempo estimate alive
            }
            const double clockBeat = clock.process(juce::MidiBuffer(), kBlock, kSr);
            const double bpm = clock.getBpm();
            expect (bpm > 100.0, "tempo estimate settled");

            std::atomic<double> applied { 0.0 }, phase { 0.0 };
            std::atomic<int> dir { 0 };
            auto resolve = [&](double offsetMs)
            {
                mu_core::InternalTransport it { own.playing, own.bpm, own.beat, &phase, &dir, &applied };
                return mu_core::resolveTransport({}, true, clock, clockBeat, it, kBlock, kSr, 0.0, offsetMs);
            };
            const auto base = resolve(0.0);

            // Once the offset has settled the block starts that many ms (x tempo) earlier on the beat.
            applied.store(25.0 * bpm / 60000.0);
            expectWithinAbsoluteError (resolve(25.0).startBeat - base.startBeat, 25.0 * bpm / 60000.0, 1.0e-9, "+25 ms plays earlier");
            applied.store(-10.0 * bpm / 60000.0);
            expectWithinAbsoluteError (resolve(-10.0).startBeat - base.startBeat, -10.0 * bpm / 60000.0, 1.0e-9, "negative plays later");

            // A change while playing ramps in: each block moves by at most a tenth of its span, and
            // consecutive blocks stay contiguous (the end of one is the start of the next).
            applied.store(0.0);
            double prevEnd = -1.0, worstGap = 0.0, worstStep = 0.0, prevApplied = 0.0;
            for (int b = 0; b < 60; ++b)
            {
                juce::MidiBuffer m;
                if ((b * 480) % 1000 < 480) m.addEvent(juce::MidiMessage((juce::uint8) 0xF8), (b * 480) % 1000);
                const double beat = clock.process(m, kBlock, kSr);   // the clock moves on, as in real blocks
                mu_core::InternalTransport it { own.playing, own.bpm, own.beat, &phase, &dir, &applied };
                const auto t2 = mu_core::resolveTransport({}, true, clock, beat, it, kBlock, kSr, 0.0, 100.0);
                if (prevEnd >= 0.0) worstGap = juce::jmax(worstGap, std::abs(t2.startBeat - prevEnd));
                worstStep = juce::jmax(worstStep, std::abs(applied.load() - prevApplied) / juce::jmax(1.0e-9, t2.blockBeats));
                prevApplied = applied.load();
                prevEnd = t2.startBeat + t2.blockBeats;
            }
            expect (applied.load() > 0.0, "the offset ramps in");
            expectLessOrEqual (worstStep, 0.11, "no block moves the offset by more than ~10 % of its span");
            expectLessOrEqual (worstGap, 1.0e-9, "no jump while the offset ramps");

            // Own transport and a host are untouched by the offset.
            MidiClockSync off;                                   // sync off: the own transport drives
            Own own2;
            own2.playing = true;
            const auto a = mu_core::resolveTransport({}, true, off, 0.0, own2.ref(), kBlock, kSr, 0.0, 0.0);
            own2.beat = 0.0;
            const auto b2 = mu_core::resolveTransport({}, true, off, 0.0, own2.ref(), kBlock, kSr, 0.0, 100.0);
            expectEquals (a.startBeat, b2.startBeat, "own transport ignores the offset");
        }
        beginTest ("Tap tempo: needs two taps, averages the last four intervals, resets after a pause");
        {
            mu_core::TapTempo tap;
            double bpm = 0.0;
            expect (! tap.tap(10.0, bpm), "one tap is not a tempo");
            expect (tap.tap(10.5, bpm));
            expectWithinAbsoluteError (bpm, 120.0, 1.0e-9, "0.5 s apart = 120 BPM");
            tap.tap(11.0, bpm); tap.tap(11.5, bpm);
            expectWithinAbsoluteError (bpm, 120.0, 1.0e-9);
            tap.tap(12.1, bpm);   // one sloppy tap: intervals .5 .5 .5 .6 → average 0.525 s
            expectWithinAbsoluteError (bpm, 114.3, 1.0e-9, "averaged and rounded to a tenth");

            mu_core::TapTempo fast;
            fast.tap(0.0, bpm); fast.tap(0.05, bpm);
            expectWithinAbsoluteError (bpm, 300.0, 1.0e-9, "clamped to the family maximum");
            mu_core::TapTempo slow;
            slow.tap(0.0, bpm); slow.tap(1.9, bpm);
            expectWithinAbsoluteError (bpm, 31.6, 1.0e-9, "just inside the pause limit: 60/1.9");

            mu_core::TapTempo pause;
            pause.tap(0.0, bpm);
            expect (! pause.tap(3.0, bpm), "a 3 s gap is a new first tap, not a 20 BPM tempo");
            expect (pause.tap(3.5, bpm));
            expectWithinAbsoluteError (bpm, 120.0, 1.0e-9);
        }

        beginTest ("Nudge: the own clock runs 4 % slower / faster while held, exactly set when released");
        {
            MidiClockSync off;                                  // sync off: the own transport drives
            Own own;
            own.playing = true;
            own.bpm     = 120.0;
            std::atomic<int>    dir { 0 };
            std::atomic<double> phase { 0.0 };
            auto resolve = [&]
            {
                mu_core::InternalTransport it { own.playing, own.bpm, own.beat, &phase, &dir };
                return mu_core::resolveTransport({}, true, off, 0.0, it, kBlock, kSr);
            };
            expectEquals (resolve().bpm, 120.0, "released: the set tempo");
            dir = +1;  expectWithinAbsoluteError (resolve().bpm, 120.0 * 1.04, 1.0e-9, "held +: 4 % faster");
            dir = -1;  expectWithinAbsoluteError (resolve().bpm, 120.0 * 0.96, 1.0e-9, "held -: 4 % slower");
            dir = 0;   expectEquals (resolve().bpm, 120.0, "released again: exactly the set tempo");
            expectEquals (own.bpm.load(), 120.0, "the BPM field's value never changed");
        }

        beginTest ("Nudge under MIDI clock: the held bend shifts the groove, stays contiguous, and a Start clears it");
        {
            MidiClockSync clock;
            clock.setEnabled(true);
            clock.setMessages(2);
            Own own;
            std::atomic<int>    dir { 0 };
            std::atomic<double> phase { 0.0 }, applied { 0.0 };
            auto resolve = [&](juce::MidiBuffer midi)
            {
                const double beat = clock.process(midi, kBlock, kSr);
                mu_core::InternalTransport it { own.playing, own.bpm, own.beat, &phase, &dir, &applied };
                return mu_core::resolveTransport({}, true, clock, beat, it, kBlock, kSr);
            };
            auto pulses = [&](int b)
            {
                juce::MidiBuffer m;
                if ((b * 480) % 1000 < 480) m.addEvent(juce::MidiMessage((juce::uint8) 0xF8), (b * 480) % 1000);
                return m;
            };
            resolve(msgs({ 0xFA }));
            for (int b = 0; b < 100; ++b) resolve(pulses(b));
            expectEquals (phase.load(), 0.0, "no nudge, no shift");

            // Hold + for 10 blocks, then - for 10, then release: every block starts where the last ended.
            double prevEnd = -1.0, worstGap = 0.0;
            double sumDelta = 0.0;
            for (int b = 0; b < 30; ++b)
            {
                dir = b < 10 ? +1 : (b < 20 ? -1 : 0);
                const auto t = resolve(pulses(100 + b));
                if (prevEnd >= 0.0) worstGap = juce::jmax(worstGap, std::abs(t.startBeat - prevEnd));
                prevEnd = t.startBeat + t.blockBeats;
                sumDelta = phase.load();
            }
            expectLessOrEqual (worstGap, 1.0e-9, "held nudging leaves no gap or overlap between blocks");
            expectWithinAbsoluteError (sumDelta, 0.0, 0.003, "10 blocks faster then 10 slower roughly cancel");

            dir = +1;
            for (int b = 0; b < 10; ++b) resolve(pulses(130 + b));
            dir = 0;
            expect (phase.load() > 0.0, "held + shifts the groove ahead");
            const double kept = phase.load();
            resolve(pulses(140));
            expectEquals (phase.load(), kept, "released: the shift stays");

            resolve(msgs({ 0xFA }));
            expectEquals (phase.load(), 0.0, "a Start clears it");
        }

        beginTest ("Nudge slower right after a Start never replays beat 0");
        {
            MidiClockSync clock;
            clock.setEnabled(true);
            clock.setMessages(2);
            Own own;
            std::atomic<int>    dir { -1 };   // slow nudge held from the start
            std::atomic<double> phase { 0.0 }, applied { 0.0 };
            double prevEnd = 0.0, worstOverlap = 0.0, minStart = 1.0e9;
            for (int b = 0; b < 60; ++b)
            {
                juce::MidiBuffer m;
                if (b == 0) m.addEvent(juce::MidiMessage((juce::uint8) 0xFA), 0);
                if ((b * 480) % 1000 < 480) m.addEvent(juce::MidiMessage((juce::uint8) 0xF8), (b * 480) % 1000);
                const double beat = clock.process(m, kBlock, kSr);
                mu_core::InternalTransport it { own.playing, own.bpm, own.beat, &phase, &dir, &applied };
                const auto t = mu_core::resolveTransport({}, true, clock, beat, it, kBlock, kSr, 0.0, -20.0);
                minStart = juce::jmin(minStart, t.startBeat);
                if (b > 0) worstOverlap = juce::jmax(worstOverlap, prevEnd - t.startBeat);
                prevEnd = t.startBeat + t.blockBeats;
            }
            expectGreaterOrEqual (minStart, 0.0, "the beat is never negative");
            expectLessOrEqual (worstOverlap, 1.0e-9, "no block starts before the previous one ended");
        }
        beginTest ("Transport only: no clock pulses is not a loss");
        {
            MidiClockSync clock;
            clock.setEnabled(true);
            clock.setMessages(1);
            Own own;
            auto t = block(clock, own, msgs({ 0xFA }));
            for (int b = 0; b < 60; ++b) t = block(clock, own);
            expect (t.playing, "transport-only plays on with no pulses");
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
