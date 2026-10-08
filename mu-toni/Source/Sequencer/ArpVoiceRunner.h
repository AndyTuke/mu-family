#pragma once

#include <cmath>

#include "Audio/ToniVoice.h"
#include "Sequencer/Arpeggiator.h"
#include <climits>

// Per-voice arpeggiator runner: an arp clock driving one ToniVoice.
// Step timing is block-granular (≤ 1 block onset jitter — the MVP contract;
// sample-accurate onset is a later refinement). See design-sequencer.md.
namespace mu_toni
{

// Rate dropdown: 1/4 · 1/4. · 1/4T · 1/8 · 1/8. · 1/8T · 1/16 · 1/16. · 1/16T · 1/32 · 1/32. · 1/32T
inline constexpr int kNumRates = 12;

inline double rateBeats(int idx) noexcept
{
    static const double base[4] = { 1.0, 0.5, 0.25, 0.125 };   // 1/4 1/8 1/16 1/32
    int group = idx / 3; if (group > 3) group = 3; if (group < 0) group = 0;
    const int kind = ((idx % 3) + 3) % 3;
    double b = base[group];
    if (kind == 1) b *= 1.5;            // dotted
    else if (kind == 2) b *= (2.0 / 3.0); // triplet
    return b;
}

// A layer's accent: a repeating on/off pattern over the arp's steps (bit i = step i+1), its length
// and how much an accented step lifts. In loop mode the pattern position follows the beat (so
// accents lock to the bar); in MIDI-trigger mode it counts from the key press.
struct ArpAccent
{
    static constexpr int kMaxSteps = 16;
    unsigned pattern = 1;
    int      length  = 4;
    float    amount  = 0.0f;   // 0..1

    // The accent of step `step` (0 when that position of the pattern is off).
    float at(int step) const noexcept
    {
        const int len = length < 1 ? 1 : (length > kMaxSteps ? kMaxSteps : length);
        const int pos = ((step % len) + len) % len;
        return ((pattern >> pos) & 1u) != 0u ? amount : 0.0f;
    }
};

// Per-block context, set once by processBlock and shared by every runner.
struct ArpContext
{
    bool   playing         = false;
    double sampleRate      = 44100.0;
    double bpm             = 120.0;
    int    rootOverrideMidi = -1;   // latest held MIDI note (root-by-MIDI), or −1
    bool   anyNoteHeld     = false;
    bool   noteOnEdge      = false; // a fresh note-on landed this block
    double startBeat       = 0.0;   // the transport beat at the block's first sample
    double beatsPerSample  = 0.0;
};

class ArpVoiceRunner
{
public:
    void prepare(double sr, int blockSize)
    {
        sampleRate = sr;
        voice.prepare(sr, blockSize);
    }

    void setVoiceParams(const ToniVoiceParams& p) { voice.setParams(p); legatoOn = p.legato; }
    void setBank(const mu_wavetable::WavetableBank* b) noexcept { voice.setBank(b); }
    void setArp(const ArpParams& p)               { arp = p; }
    void setAccent(const ArpAccent& a) noexcept { accent = a; }

    void setStep(int rateIdx, float gate01, bool midiTrig)
    {
        rateIndex   = rateIdx;
        gateLen     = gate01;
        midiTrigger = midiTrig;
    }

    // Renders one block into the channel buffer (clears first). ADDs the voice output.
    void render(juce::AudioBuffer<float>& buf, int n, const ArpContext& ctx)
    {
        buf.clear();
        if (n <= 0) return;

        bool run = ctx.playing;
        if (midiTrigger) run = run && ctx.anyNoteHeld;

        const bool started = run && ! wasRunning;
        wasRunning = run;

        if (! run)
        {
            if (noteHeld) { voice.noteOff(); noteHeld = false; }
            voice.process(buf, n);   // the release tail
            return;
        }

        const double spb = sampleRate * 60.0 / (ctx.bpm > 0.0 ? ctx.bpm : 120.0);
        const int    samplesPerStep = juce::jmax(1, (int) (spb * rateBeats(rateIndex)));

        if (midiTrigger)
            renderTriggered(buf, n, ctx, samplesPerStep);
        else
            renderOnGrid(buf, n, ctx, started, samplesPerStep);
    }

    bool isActive() const noexcept { return voice.isActive(); }

    // The last step fired: its number in the pattern, and its sample offset in the block that fired it.
    int lastStep()       const noexcept { return lastFiredStep; }
    int lastStepOffset() const noexcept { return lastFiredOffset; }
    int stepsFired()     const noexcept { return firedCount; }

private:
    // MIDI-trigger mode: the pattern restarts at each key press and counts its own steps from there.
    void renderTriggered(juce::AudioBuffer<float>& buf, int n, const ArpContext& ctx, int samplesPerStep)
    {
        if (ctx.noteOnEdge) { stepIndex = 0; stepCounter = 0; prevTied = false; }
        int pos = 0;
        while (stepCounter < n)
        {
            const int at = juce::jmax(pos, stepCounter);
            renderSpan(buf, pos, at);
            pos = at;
            lastFiredOffset = at;
            fireStep(ctx, samplesPerStep);
            stepCounter += samplesPerStep;
        }
        renderSpan(buf, pos, n);
        stepCounter -= n;
    }

    // Loop mode: steps sit on the beat grid — step k starts at beat k × the step length — so the
    // arp locks to the host's bars, and a loop or a position jump lands on the same step (and note).
    // A start, a rate change or a jump in the beat re-finds the next step.
    void renderOnGrid(juce::AudioBuffer<float>& buf, int n, const ArpContext& ctx, bool started, int samplesPerStep)
    {
        const double stepBeats = rateBeats(rateIndex);
        const double bps       = ctx.beatsPerSample;
        const double endBeat   = ctx.startBeat + bps * (double) n;
        if (started || stepBeats != gridStepBeats || std::abs(ctx.startBeat - expectedBeat) > 0.5 * stepBeats)
        {
            gridStepBeats = stepBeats;
            nextStep      = (long long) std::ceil(ctx.startBeat / stepBeats - 1.0e-9);
        }
        expectedBeat = endBeat;

        // Fire each step whose beat falls inside this block, at its sample.
        int pos = 0;
        while (bps > 0.0 && (double) nextStep * stepBeats < endBeat)
        {
            const int at = juce::jlimit(pos, n - 1,
                                        (int) std::ceil(((double) nextStep * stepBeats - ctx.startBeat) / bps - 1.0e-9));
            renderSpan(buf, pos, at);
            pos = at;
            stepIndex = (int) (nextStep % (1LL << 30));   // the note-pool position follows the beat
            lastFiredOffset = at;
            fireStep(ctx, samplesPerStep);
            ++nextStep;
        }
        renderSpan(buf, pos, n);
    }

    // Render buf[from, to), ending a gated note on its exact sample.
    void renderSpan(juce::AudioBuffer<float>& buf, int from, int to)
    {
        while (from < to)
        {
            int len = to - from;
            const bool gateEnds = noteHeld && ! tiedOut && gateCounter <= len;
            if (gateEnds) len = juce::jmax(0, gateCounter);
            voice.process(buf, from, len);
            from += len;
            if (noteHeld && ! tiedOut) gateCounter -= len;
            if (gateEnds) { voice.noteOff(); noteHeld = false; }
        }
    }

    void fireStep(const ArpContext& ctx, int samplesPerStep)
    {
        ArpParams p = arp;
        if (ctx.rootOverrideMidi >= 0)
        {
            const int m = ctx.rootOverrideMidi;
            p.rootNote   = m % 12;
            p.rootOctave = juce::jlimit(0, 8, m / 12 - 1);
        }

        const int midi = stepMidi(p, stepIndex);
        lastFiredStep = stepIndex;
        ++firedCount;
        ++stepIndex;

        const bool tie   = legatoOn && gateLen >= 0.99f;   // 100 % gate + legato → tie/slide
        const bool glide = legatoOn && prevTied;
        voice.setAccent(accent.at(lastFiredStep), glide);
        if (glide) voice.noteOnLegato(midi);   // glide, no retrigger
        else       voice.noteOn(midi);

        noteHeld    = true;
        tiedOut     = tie;
        prevTied    = tie;
        gateCounter = tie ? (INT_MAX / 2) : juce::jmax(1, (int) ((double) samplesPerStep * gateLen));
    }

    ToniVoice voice;
    ArpParams arp;
    ArpAccent accent;
    int    rateIndex   = 6;      // 1/16
    float  gateLen     = 0.5f;
    bool   legatoOn    = false;
    bool   midiTrigger = false;
    double sampleRate  = 44100.0;

    int  stepIndex   = 0;
    int  stepCounter = 0;
    int  gateCounter = 0;
    bool noteHeld    = false;
    bool tiedOut     = false;
    bool prevTied    = false;
    bool wasRunning  = false;

    int  lastFiredStep   = -1;
    int  lastFiredOffset = -1;
    int  firedCount      = 0;

    // Beat-grid stepping (loop mode).
    long long nextStep      = 0;
    double    gridStepBeats = 0.0;
    double    expectedBeat  = 0.0;
};

} // namespace mu_toni
