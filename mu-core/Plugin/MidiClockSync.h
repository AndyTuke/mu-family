#pragma once

#include <juce_audio_devices/juce_audio_devices.h>
#include <atomic>
#include "Plugin/MidiClockTempo.h"   // mu-core: the shared tempo PLL

// MIDI clock sync state machine — a SHARED, plugin-agnostic mu-core component
// (lifted from mu-clid so every synth product slaves to external MIDI clock the
// same way). Pure JUCE + atomics, no product symbols.
//
// Audio thread calls process() each block; it scans the MidiBuffer for real-time
// messages (0xF8 clock tick, 0xFA/FB/FC start/continue/stop, 0xF2 song position),
// feeds each tick to the shared MidiClockTempo estimator (the same one mu-link uses),
// and returns the beat position at the block's first sample.
//
// The beat is CONTINUOUS, not a pulse-count staircase. The integer pulse index is the
// truth (24 per quarter note, so 16ths land exactly); a small phase-locked loop turns it
// into a smooth beat: the last pulse anchors (sample, beat), the beat runs on from there at
// the estimated tempo, and each new pulse's phase error is slewed in over the next pulse
// period (a frequency estimate alone would drift and jitter). The beat therefore never
// steps or runs backwards between clocks (only a locate, loop or Start moves it), and a block
// reads it forward from the last clock, so a step lands where the tempo puts it, not a pulse
// late and not on a quantised value.
// Positions use the MIDI convention that the first clock after Start, Continue or a Song
// Position Pointer IS that position (tick 0 after Start), matching what mu-link sends.
//
// All cross-thread reads (isEnabled, isPlaying, getBpm, getBeatPosUI) are backed by
// atomics and safe to call from the message thread. The audio-thread-only fields
// (everything in the second private block) must not be accessed from any other thread.
class MidiClockSync
{
public:
    // ── Message-thread setters ───────────────────────────────────────────
    void setEnabled(bool on)
    {
        enabled.store(on, std::memory_order_relaxed);
        if (!on)
        {
            playing.store(false);
            bpmEst.store(0.0);   // no tempo estimate until pulses arrive again
        }
    }

    void setMessages(int mode)   // 0=clock only, 1=transport only, 2=both
    {
        mode = juce::jlimit(0, 2, mode);
        messages.store(mode, std::memory_order_relaxed);
        if (mode == 0) playing.store(false);   // Stop is ignored from now on, so drop the clock's play state
    }

    // ── Cross-thread reads ───────────────────────────────────────────────
    bool   isEnabled()    const { return enabled.load(std::memory_order_relaxed); }
    int    getMessages()  const { return messages.load(std::memory_order_relaxed); }
    bool   isPlaying()    const { return playing.load(); }
    double getBpm()       const { return bpmEst.load(); }   // 0 = no estimate yet (no pulses seen)
    double getBeatPosUI() const { return beatPosUI.load(std::memory_order_relaxed); }   // beat at the end of the last block

    // Clock health for the UI: Waiting = sync on but no pulse heard yet; Locked = pulses arriving;
    // Lost = pulses stopped (cable pulled, master gone) — the transport is held stopped until
    // they return. Only judged while the ticks drive (Messages = Clock only / Clock + Transport).
    enum class ClockState { Off, Waiting, Locked, Lost };
    ClockState getClockState() const { return (ClockState) clockState.load(std::memory_order_relaxed); }

    // ── Audio thread ─────────────────────────────────────────────────────
    // Scans midi for real-time messages; updates internal state; returns the
    // beat position at the block's first sample (0.0 if sync is disabled).
    double process(const juce::MidiBuffer& midi, int numSamples, double sampleRate)
    {
        startedThisBlock = locatedThisBlock = false;
        if (!enabled.load(std::memory_order_relaxed))
        {
            wasEnabled = lost = haveAnchor = false;
            clockState.store((int) ClockState::Off, std::memory_order_relaxed);
            return 0.0;
        }
        if (! wasEnabled)   // just switched on: nothing heard yet
        {
            wasEnabled = true;
            heardPulse = false;
            haveAnchor = false;
            samplesSincePulse = 0;
            tempo.reset();   // a stale tempo from before sync was switched off would slew the beat wrongly
        }

        // One Messages-mode snapshot per block, shared with the resolver, so a UI change can't
        // land between the two.
        const int mode = messages.load(std::memory_order_relaxed);
        blockTicks     = mode != 1;
        blockTransport = mode != 0;
        const bool doTick      = blockTicks;
        const bool doTransport = blockTransport;
        if (doTick && ! wasTicks)   // ticks just started driving: judge the clock afresh
        {
            heardPulse = false;
            samplesSincePulse = 0;
        }
        wasTicks = doTick;

        // The beat runs on from the last pulse for at most this long (four pulse periods at the
        // estimated tempo, never under 250 ms); past it the clock counts as lost and the beat holds.
        sampleRateHz          = sampleRate;
        const double bpmNow    = bpmEst.load();
        const double pulseSecs = bpmNow > 0.0 ? 60.0 / (bpmNow * 24.0) : 0.125;
        const double lossSecs  = juce::jmax(0.25, 4.0 * pulseSecs);
        horizonSamples        = lossSecs * sampleRate;

        // The block's beat is read forward from the clocks BEFORE this block, so it is continuous
        // with the previous block's end; a Start or Song Position Pointer in this block moves the
        // song, and the block then reports that position instead.
        const double startedFrom = beatAt((double) sampleClock);
        double jumpBeat = -1.0;

        // Walk the block's real-time messages: transport changes, then tempo + beat per tick.
        for (const auto& msgRef : midi)
        {
            const auto& m = msgRef.getMessage();

            // Song Position Pointer (F2 lsb msb): the master located to a sixteenth (= 6 pulses).
            // Only valid while stopped (MIDI spec); the next Continue plays from there.
            if (m.getRawDataSize() == 3 && m.getRawData()[0] == 0xF2)
            {
                if (doTransport && ! playing.load())
                {
                    const int sixteenths = (m.getRawData()[1] & 0x7F) | ((m.getRawData()[2] & 0x7F) << 7);
                    nextPulse = (juce::int64) sixteenths * 6;
                    haveAnchor = false;
                    jumpBeat = beatOf(nextPulse);
                    locatedThisBlock = true;
                }
                continue;
            }
            if (m.getRawDataSize() != 1) continue;
            const juce::uint8 b  = m.getRawData()[0];
            const int         so = msgRef.samplePosition;

            if (doTransport)
            {
                if (b == 0xFA)
                {
                    nextPulse = 0;                   // a Start restarts the song: the next clock is tick 0
                    haveAnchor = false;
                    jumpBeat = 0.0;
                    startedThisBlock = true;
                    tempo.restartInterval();
                    playing.store(true);
                    samplesSincePulse = -so;         // give the master time to send its first pulse
                }
                else if (b == 0xFB)                   // Continue: the next clock is the pulse after the last
                {
                    haveAnchor = false;
                    tempo.restartInterval();
                    playing.store(true);
                    samplesSincePulse = -so;
                }
                else if (b == 0xFC)                   // Stop: the beat holds where the song got to
                {
                    playing.store(false);
                    haveAnchor = false;
                }
            }

            if (b == 0xF8)
            {
                // Pulses are timestamped on the audio sample clock (sample-accurate within the block).
                const double pulseSample = (double) (sampleClock + so);
                if (tempo.onPulse(pulseSample / sampleRate))
                    bpmEst.store(tempo.bpm());

                // Song position only moves while the master plays (MIDI spec); with transport
                // messages off there is no play state, so every pulse counts. Counted in every
                // mode so switching Messages mode mid-play keeps the master's position.
                if (! doTransport || playing.load())
                    anchorOnPulse(pulseSample);

                heardPulse = true;
                samplesSincePulse = -so;   // counted from this pulse; the block's length is added below
            }
        }

        // Clock-loss watchdog (ticks modes): no pulse for lossSecs means the clock is lost.
        samplesSincePulse += numSamples;
        // Many masters stop sending clock while stopped, so silence after a Stop is waiting, not lost.
        const bool gap         = heardPulse && (double) samplesSincePulse > lossSecs * sampleRate;
        const bool masterIdle  = doTransport && ! playing.load();
        lost = doTick && gap && ! masterIdle;
        const auto state = ! doTick                     ? ClockState::Waiting   // not judged in Transport only
                         : lost                        ? ClockState::Lost
                         : heardPulse && ! gap         ? ClockState::Locked
                                                        : ClockState::Waiting;
        clockState.store((int) state, std::memory_order_relaxed);

        const double blockBeat = jumpBeat >= 0.0 ? jumpBeat : startedFrom;
        sampleClock += numSamples;
        blockEndBeat = beatAt((double) sampleClock);   // read after this block's clocks: the next block starts here
        beatPosUI.store(blockEndBeat, std::memory_order_relaxed);
        return blockBeat;
    }

    // Audio thread, after process(): this block's Messages-mode snapshot — ticks give tempo +
    // beat, transport messages (Start / Continue / Stop) give play state — and whether a Start
    // (0xFA) or a Song Position Pointer (0xF2) moved the song position in it.
    bool ticksDrive()      const { return blockTicks; }
    bool transportDrives() const { return blockTransport; }
    bool startedInBlock()  const { return startedThisBlock; }
    bool locatedInBlock()  const { return locatedThisBlock; }
    bool isLost()          const { return lost; }   // the watchdog's verdict for this block

    // The beat at the end of the block just processed. The next block starts exactly here, so
    // a sequencer that interpolates from process()'s start beat to this end beat is contiguous
    // across block edges (no overlap, no gap), whatever the phase corrections did meanwhile.
    double getBlockEndBeat() const { return blockEndBeat; }

private:
    // Cross-thread atomics.
    std::atomic<bool>   enabled   { false };
    std::atomic<int>    messages  { 2 };
    std::atomic<bool>   playing { false };
    std::atomic<double> bpmEst    { 0.0 };   // 0 = no estimate yet
    std::atomic<double> beatPosUI { 0.0 };
    std::atomic<int>    clockState { (int) ClockState::Off };

    // PLL tuning. A pulse further than this from where the model expected it (a locate, a loop,
    // a long gap) re-anchors at once; closer ones are pulled in by kPhaseGain of the error,
    // slewed over the next pulse period. The slew rate stays below the beat's own rate while
    // gain * snap < one pulse (0.25 * 3/24 < 1/24), so the beat can never run backwards.
    static constexpr double kSnapBeats = 3.0 / 24.0;
    static constexpr double kPhaseGain = 0.25;

    // ── Audio-thread-only state ──────────────────────────────────────────
    static double beatOf(juce::int64 pulses) { return (double) pulses / 24.0; }

    // The pulse at sample `sp` is song pulse nextPulse: re-anchor the beat model on it.
    void anchorOnPulse(double sp)
    {
        const double target = beatOf(nextPulse++);
        const double bps    = beatsPerSample();
        if (! haveAnchor || bps <= 0.0)
        {
            anchorBeat = target;                       // first pulse (or no tempo yet): take it as it is
            phaseCorr = 0.0;
        }
        else
        {
            const double predicted = beatAt(sp);        // where the model had the beat at this clock
            const double err       = target - predicted;
            if (std::abs(err) > kSnapBeats)
            {
                anchorBeat = target;                   // a locate, loop or long gap: jump
                phaseCorr = 0.0;
            }
            else
            {
                anchorBeat = predicted;                // continuous at this clock …
                phaseCorr       = kPhaseGain * err;         // … with the phase error slewed in from here
            }
        }
        phaseCorrSamples  = bps > 0.0 ? 1.0 / (24.0 * bps) : 1.0;   // one pulse period
        anchorSample = sp;
        haveAnchor   = true;
    }

    // Beats per audio sample at the estimated tempo (0 until there is an estimate).
    double beatsPerSample() const { return sampleRateHz > 0.0 ? tempo.bpm() / 60.0 / sampleRateHz : 0.0; }

    // The beat at absolute sample `s` (at or after the last clock): the anchor run on at the
    // estimated tempo with the last phase correction slewed in over one pulse period, held once
    // the clock has been silent past the horizon. With no anchor yet (after Start / Continue /
    // locate / Stop) the song sits at its position.
    double beatAt(double s) const
    {
        if (! haveAnchor)
            return beatOf(nextPulse);
        const double dt = juce::jlimit(0.0, horizonSamples, s - anchorSample);
        return anchorBeat + dt * beatsPerSample() + phaseCorr * juce::jmin(1.0, dt / phaseCorrSamples);
    }

    juce::int64  nextPulse = 0;          // song index the next counted clock will have (Start = 0, SPP = sixteenths * 6)
    bool         haveAnchor = false;     // a counted clock has anchored the beat since the last Start / Continue / locate / Stop
    double       anchorBeat = 0.0;       // beat at anchorSample
    double       anchorSample = 0.0;     // absolute sample (sampleClock timeline) of the last counted clock
    double       blockEndBeat = 0.0;     // beat at the end of the last block (see getBlockEndBeat)
    double       phaseCorr = 0.0;             // phase correction (beats) still being slewed in after anchorSample
    double       phaseCorrSamples = 1.0;      // how long that slew takes (one pulse period)
    double       horizonSamples = 0.0;   // how long the beat runs on after the last clock
    double       sampleRateHz = 48000.0; // this block's rate (beatsPerSample)
    bool         startedThisBlock = false;
    bool         locatedThisBlock = false;
    bool         wasEnabled = false;       // watchdog: sync was on last block
    bool         wasTicks   = false;       // watchdog: ticks drove last block
    bool         heardPulse = false;       // watchdog: a pulse arrived since sync went on
    bool         lost       = false;       // watchdog: this block's verdict
    juce::int64  samplesSincePulse = 0;
    bool         blockTicks     = true;    // Messages mode snapshot (default 2 = both)
    bool         blockTransport = true;
    juce::int64  sampleClock = 0;          // samples processed while enabled (pulse timestamps)
    mu_core::MidiClockTempo tempo;         // shared family tempo estimator
};
