#pragma once

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

// Per-block context, set once by processBlock and shared by every runner.
struct ArpContext
{
    bool   playing         = false;
    double sampleRate      = 44100.0;
    double bpm             = 120.0;
    int    rootOverrideMidi = -1;   // latest held MIDI note (root-by-MIDI), or −1
    bool   anyNoteHeld     = false;
    bool   noteOnEdge      = false; // a fresh note-on landed this block
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
    void setArp(const ArpParams& p)               { arp = p; }
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

        // Restart the pattern cleanly on a trigger edge / on transport start.
        if (midiTrigger && ctx.noteOnEdge)  { stepIndex = 0; stepCounter = 0; prevTied = false; }
        if (! midiTrigger && run && ! wasRunning) { stepCounter = 0; }
        wasRunning = run;

        const double spb = sampleRate * 60.0 / (ctx.bpm > 0.0 ? ctx.bpm : 120.0);
        const int    samplesPerStep = juce::jmax(1, (int) (spb * rateBeats(rateIndex)));

        if (run)
        {
            if (noteHeld && ! tiedOut)
            {
                gateCounter -= n;
                if (gateCounter <= 0) { voice.noteOff(); noteHeld = false; }
            }

            stepCounter -= n;
            if (stepCounter <= 0)
            {
                stepCounter += samplesPerStep;
                fireStep(ctx, samplesPerStep);
            }
        }
        else if (noteHeld)
        {
            voice.noteOff();
            noteHeld = false;
        }

        voice.process(buf, n);
    }

    bool isActive() const noexcept { return voice.isActive(); }

private:
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
        ++stepIndex;

        const bool tie = legatoOn && gateLen >= 0.99f;   // 100 % gate + legato → tie/slide
        if (legatoOn && prevTied) voice.noteOnLegato(midi);   // glide, no retrigger
        else                      voice.noteOn(midi);

        noteHeld    = true;
        tiedOut     = tie;
        prevTied    = tie;
        gateCounter = tie ? (INT_MAX / 2) : juce::jmax(1, (int) ((double) samplesPerStep * gateLen));
    }

    ToniVoice voice;
    ArpParams arp;
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
};

} // namespace mu_toni
