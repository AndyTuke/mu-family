#pragma once

#include "Control/ControlSink.h"
#include "Control/MidiControlMap.h"
#include "Plugin/TransportResolver.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <atomic>

// The hand-off between the audio thread, where a mapped CC / note arrives, and the message
// thread, where it is performed (APVTS listeners and the host wrapper are not real-time safe).
//
// Audio thread, once per block (`process`): every message the map claims is taken out of the
// MIDI buffer (a mapped pad must not also play a note). A knob mapping just stores its latest
// value and a dirty flag, so a sweep coalesces into one write. An action mapping queues an
// event; mute / solo / stop first wait for the next beat or bar of the resolved transport
// (`noteBlock` hands the router the block's transport after the product resolves it).
// Message thread (`drain`): performs what is waiting through a ControlSink.
//
// Timing is block-granular plus one message-thread hop: an action lands within a block of its
// boundary, not on the sample. Queued events carry the map's epoch, so one queued before the
// table was edited (indices shift) is dropped rather than performed as the wrong mapping.
namespace mu_core
{

class MidiControlRouter
{
public:
    explicit MidiControlRouter(MidiControlMap& mapToUse);

    // ---- audio thread (no allocation, no locks) ----
    // Returns true when something is waiting for the message thread (call triggerAsyncUpdate).
    bool process(juce::MidiBuffer& midi) noexcept;

    // This block's resolved transport, used to time the next block's quantised actions.
    void noteBlock(const BlockTransport& transport, double beatsPerBar) noexcept;

    // Whether play comes from the host / clock: transport actions then do nothing (the resolver
    // owns the clock and a pad must never bypass it).
    bool playIsOutside() const noexcept { return playOutside.load(std::memory_order_relaxed); }

    // ---- message thread ----
    void drain(ControlSink& sink);

private:
    struct Event   { int16_t mapping = -1; float value = 0.0f; uint32_t epoch = 0; };
    struct Pending { int16_t mapping = -1; float value = 0.0f; uint32_t epoch = 0; double boundaryBeat = 0.0; };

    static constexpr int kFifoSize     = 64;
    static constexpr int kMaxPending   = 32;
    static constexpr int kScratchBytes = 64 * 1024;   // room to rebuild a block's MIDI without allocating

    bool push(int mapping, float value, uint32_t epoch) noexcept;
    bool releaseDue(uint32_t epoch) noexcept;
    bool queueAction(int mapping, float value, uint32_t epoch) noexcept;

    MidiControlMap& map;

    juce::AbstractFifo                  fifo { kFifoSize };
    std::array<Event, kFifoSize>        queue {};
    std::array<Pending, kMaxPending>    pending {};
    int                                 numPending = 0;

    BlockTransport      last;
    bool                discontinuity = false;   // the beat did not continue from the last block (loop / relocate / jump)
    double              barBeats = 4.0;
    std::atomic<bool>   playOutside { false };
    juce::MidiBuffer    scratch;                  // keeps its reserved capacity: never swapped with the host's buffer
};

} // namespace mu_core
