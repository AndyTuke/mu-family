#pragma once

#include "MuLimits.h"

#include <cstddef>
#include <cstdint>
#include <type_traits>

// The per-call context a layer renders with (docs/mu-tronic/design.md 2.1.5, the time model C4).
//
// One plain struct, built once per block by ProcessorBase and copied per layer with `slot` set. It mirrors
// a C struct: standard layout, trivially copyable, fixed-width fields, no JUCE or std type, no reference
// and no callback, so a later C boundary could carry it unchanged (extraction-plan 9.6). Fields are only
// ever appended, and `structSize` / `version` at the head say which ones a reader may use. Every
// reserved field is explicit so the struct has no implicit padding (the layout test freezes it).
//
// Every transport-anchored position a layer computes is a function of the absolute beat in here; a layer
// keeps no free-running phase (design 2.1.5, "State a layer may keep").
namespace mu_core
{
    inline constexpr uint32_t kRenderContextVersion = 1;

    // One short MIDI message (sysex and longer messages are not carried), 8 bytes.
    struct MidiEvent
    {
        int32_t frame;               // offset from the start of the HOST block
        uint8_t size;                // 1..3
        uint8_t bytes[3];
    };

    // The block's MIDI after the control router's claims: decoded once per block, never copied per layer.
    struct MidiView
    {
        const MidiEvent* events;     // time-ordered, valid for this block only
        int32_t  count;
        int32_t  firstFrame;         // this call covers host frames [firstFrame, firstFrame + blockSize)
        uint16_t channelMask;        // bit c = channel c+1 is this layer's (0xFFFF = omni)
        uint16_t dropped;            // events the decoder had no room for; 0 expected
        uint32_t reserved;
    };

    // Where a layer writes MIDI (MIDI output mode; a tail's owed note-offs). events == nullptr: none.
    struct MidiSink
    {
        MidiEvent* events;
        int32_t    capacity;
        int32_t    reserved;
        int32_t*   count;            // appended by the layer; frames relative to the host block
    };

    // A priority-0 layer's output this block, post-insert and pre-strip (C10).
    struct PeerTap
    {
        const float* channel[2];     // channel[1] == channel[0] when mono
        int32_t      valid;          // 0: Empty, priority-1 or not rendered: read as silence
        int32_t      reserved;
    };

    struct RenderContext
    {
        uint32_t structSize;         // sizeof (RenderContext)
        uint32_t version;            // kRenderContextVersion
        double   sampleRate;
        int32_t  blockSize;          // frames in THIS call, 1..preparedBlockSize (a longer host block is chunked)
        int32_t  preparedBlockSize;
        int32_t  frameOffset;        // where this call starts inside the host block (0 unless chunked)
        int32_t  slot;
        double   bpm;
        double   startBeat;          // song beat at this call's first frame; never wrapped by the contract; may be negative
        double   beatsPerSample;     // the resolver's (under MIDI clock: span / frames); > 0 also while stopped
        double   callBeats;          // beats this call spans; 0 while stopped
        double   beatWrap;           // 0 = unbounded; > 0 = the HOST bounds its beat space to [0, beatWrap)
        uint8_t  playing;
        uint8_t  justStarted;        // playing, and not playing in the last RENDERED block
        uint8_t  justStopped;        // not playing, and playing in the last rendered block
        uint8_t  transportJumped;    // playing in both, and a host-block discontinuity since the last rendered block
        uint8_t  retiring;           // 1 in a tail call (empty MIDI in, a valid note-off sink, no peers)
        uint8_t  frozen;             // 1: render from the last resolved values, read no parameter or matrix
        uint8_t  reserved[2];
        MidiView midi;
        MidiSink midiOut;
        const PeerTap* peers;        // kMaxLayers entries in the priority-1 pass, else nullptr
        int32_t  numPeers;
        int32_t  reserved2;

        // Helpers, not part of the C mirror (non-virtual members do not change the layout).
        const PeerTap* peer(int s) const noexcept
        {
            return peers != nullptr && s >= 0 && s < numPeers && peers[s].valid != 0 ? &peers[s] : nullptr;
        }
        double endBeat() const noexcept { return startBeat + callBeats; }

        // Appends one event to the call's MIDI sink; false (nothing written) when there is no sink or it is
        // full. `frameInCall` is relative to this call and is stored relative to the host block.
        bool emitMidi(int frameInCall, uint8_t b0, uint8_t b1, uint8_t b2, int size = 3) const noexcept
        {
            if (midiOut.events == nullptr || midiOut.count == nullptr || *midiOut.count >= midiOut.capacity) return false;
            auto& e = midiOut.events[*midiOut.count];
            e.frame = frameOffset + frameInCall;
            e.size = (uint8_t) size;
            e.bytes[0] = b0; e.bytes[1] = b1; e.bytes[2] = b2;
            ++*midiOut.count;
            return true;
        }
    };

    static_assert(std::is_standard_layout_v<MidiEvent>     && std::is_trivially_copyable_v<MidiEvent>);
    static_assert(std::is_standard_layout_v<MidiView>      && std::is_trivially_copyable_v<MidiView>);
    static_assert(std::is_standard_layout_v<MidiSink>      && std::is_trivially_copyable_v<MidiSink>);
    static_assert(std::is_standard_layout_v<PeerTap>       && std::is_trivially_copyable_v<PeerTap>);
    static_assert(std::is_standard_layout_v<RenderContext> && std::is_trivially_copyable_v<RenderContext>);
    static_assert(sizeof (MidiEvent) == 8, "MidiEvent is 8 bytes");

    // A zeroed context with its header filled: the base starts every block's template from this.
    inline RenderContext makeRenderContext() noexcept
    {
        RenderContext c {};
        c.structSize = (uint32_t) sizeof (RenderContext);
        c.version = kRenderContextVersion;
        c.slot = -1;
        return c;
    }

    // Calls fn(event, frameInCall) for each event of the view that falls inside this call's frame range and
    // on one of the view's channels (system messages, with no channel, always pass). Inline, not a callback.
    template <class Fn>
    inline void forEachMidi(const RenderContext& ctx, Fn&& fn)
    {
        const auto& v = ctx.midi;
        const int32_t first = v.firstFrame, end = v.firstFrame + ctx.blockSize;
        for (int32_t i = 0; i < v.count; ++i)
        {
            const auto& e = v.events[i];
            if (e.frame < first || e.frame >= end) continue;
            if (e.size > 0 && (e.bytes[0] & 0xf0) != 0xf0 && v.channelMask != 0xFFFF
                && ((v.channelMask >> (e.bytes[0] & 0x0f)) & 1u) == 0)
                continue;
            fn(e, (int) (e.frame - first));
        }
    }
}
