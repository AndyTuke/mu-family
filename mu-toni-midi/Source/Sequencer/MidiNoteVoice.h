#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <cstdint>

// The "voice" of mu-Toni MIDI: it has the same note interface as mu-Toni's ToniVoice (noteOn, noteOnLegato, noteOff,
// setAccent, process), so the shared arp runner (mu_toni::BasicArpRunner) drives it unchanged, but instead of sound it
// records MIDI note events at the sample the runner fired them. The runner calls process(from, len) for each span it
// "renders", so the running position tells every note event its sample offset in the block.
//
// Allocation-free: a fixed event list per block (a block that fires more than kMaxEvents events drops the rest).
namespace mu_toni_midi
{

class MidiNoteVoice
{
public:
    static constexpr int kMaxEvents = 64;

    struct Event
    {
        int     sample = 0;
        uint8_t status = 0;    // 0x90 note on, 0x80 note off (channel added when the event is written out)
        uint8_t note   = 0;
        uint8_t velocity = 0;
        uint8_t channel  = 1;  // 1..16: a note-off goes out on the channel its note-on used
    };

    void prepare(double, int) noexcept {}

    // The silent, nothing-sounding state (a transport stop, a slot hand-over). Any note still sounding is NOT released
    // here: the caller asks for that with MidiArpCore::flushNoteOff so the off reaches the host.
    void reset() noexcept { count = 0; pos = 0; sounding = -1; soundingCh = 1; accent = 0.0f; glide = false; }

    // Start of a block: forget last block's events and restart the position.
    void beginBlock(int channelOneBased, int baseVelocity) noexcept
    {
        count = 0;
        pos = 0;
        channel = juce::jlimit(1, 16, channelOneBased);
        baseVel = juce::jlimit(1, 127, baseVelocity);
    }

    // ── The ToniVoice interface the runner uses ──────────────────────────────
    void setAccent(float amount01, bool glideNext) noexcept { accent = juce::jlimit(0.0f, 1.0f, amount01); glide = glideNext; }

    void noteOn(int midi) noexcept
    {
        if (sounding >= 0) emit(0x80, sounding, 0, soundingCh);   // never two notes at once
        emit(0x90, midi, velocity(), channel);
        sounding = clampNote(midi);
        soundingCh = channel;
    }

    // Legato: the new note starts before the old one ends, so a mono synth glides instead of retriggering.
    void noteOnLegato(int midi) noexcept
    {
        const int previous = sounding, previousCh = soundingCh;
        emit(0x90, midi, velocity(), channel);
        sounding = clampNote(midi);
        soundingCh = channel;
        if (previous >= 0 && (previous != sounding || previousCh != soundingCh)) emit(0x80, previous, 0, previousCh);
    }

    void noteOff() noexcept
    {
        if (sounding >= 0) emit(0x80, sounding, 0, soundingCh);
        sounding = -1;
    }

    // The runner "renders" a span; the voice only moves its position on.
    void process(juce::AudioBuffer<float>&, int from, int len) noexcept { pos = from + len; }
    void process(juce::AudioBuffer<float>&, int n) noexcept             { pos = n; }
    bool isActive() const noexcept                                      { return sounding >= 0; }

    // ── What the processor reads ─────────────────────────────────────────────
    int          numEvents() const noexcept        { return count; }
    const Event& event(int i) const noexcept       { return events[i]; }
    int          soundingNote() const noexcept     { return sounding; }
    int          soundingChannel() const noexcept  { return soundingCh; }

private:
    static int clampNote(int midi) noexcept { return juce::jlimit(0, 127, midi); }
    int velocity() const noexcept { return juce::jlimit(1, 127, juce::roundToInt((float) baseVel + accent * (float) (127 - baseVel))); }

    void emit(uint8_t status, int note, int vel, int ch) noexcept
    {
        if (count >= kMaxEvents) return;
        events[count++] = { pos, status, (uint8_t) clampNote(note), (uint8_t) vel, (uint8_t) juce::jlimit(1, 16, ch) };
    }

    Event events[kMaxEvents] {};
    int   count = 0, pos = 0, sounding = -1, soundingCh = 1, channel = 1, baseVel = 100;
    float accent = 0.0f;
    bool  glide = false;
};

} // namespace mu_toni_midi
