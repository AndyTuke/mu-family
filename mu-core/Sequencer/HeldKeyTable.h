#pragma once

#include "RenderContext.h"

#include <array>
#include <cstdint>

// The keys physically held on the processor's MIDI In right now: a velocity per channel and note (0 = not held).
// The block driver feeds it every channel message in arrival order and, when a layer is created or its MIDI In
// channel changes, turns the table into note-on events for that layer (a layer cannot know keys that went down before
// it existed). Plain fixed storage, no allocation; touched by the audio thread only.
namespace mu_core
{

class HeldKeyTable
{
public:
    // Follows one channel message: a note-on sets the key, a note-off (or note-on at velocity 0) clears it, and
    // All Sound Off / All Notes Off (CC 120 / 123) clears the channel. Anything else, system messages included, is ignored.
    void apply(const MidiEvent& e) noexcept
    {
        if (e.size < 2) return;
        const int type = e.bytes[0] & 0xf0, ch = e.bytes[0] & 0x0f;
        if (type == 0x90 && e.bytes[2] != 0)           set(ch, e.bytes[1] & 0x7f, e.bytes[2]);
        else if (type == 0x80 || type == 0x90)         set(ch, e.bytes[1] & 0x7f, 0);
        else if (type == 0xb0 && (e.bytes[1] == 120 || e.bytes[1] == 123))
            for (int n = 0; n < 128; ++n) set(ch, n, 0);
    }

    void clear() noexcept
    {
        for (auto& c : velocity) c.fill(0);
        count = 0;
    }

    bool anyHeld() const noexcept { return count > 0; }
    int  numHeld() const noexcept { return count; }

    // Writes a note-on at `frame` for each held key on a channel `mask` passes (bit c = channel c+1; 0xFFFF = omni),
    // channel by channel and note by note upwards, up to `capacity` events. Returns how many were written.
    int collect(MidiEvent* out, int capacity, int frame, uint16_t mask) const noexcept
    {
        int n = 0;
        if (count == 0 || out == nullptr) return 0;
        for (int ch = 0; ch < 16 && n < capacity; ++ch)
        {
            if (mask != 0xFFFF && ((mask >> ch) & 1u) == 0) continue;
            for (int note = 0; note < 128 && n < capacity; ++note)
            {
                const uint8_t v = velocity[(size_t) ch][(size_t) note];
                if (v == 0) continue;
                auto& e = out[n++];
                e.frame = frame;
                e.size = 3;
                e.bytes[0] = (uint8_t) (0x90 | ch);
                e.bytes[1] = (uint8_t) note;
                e.bytes[2] = v;
            }
        }
        return n;
    }

private:
    void set(int ch, int note, uint8_t v) noexcept
    {
        auto& slot = velocity[(size_t) ch][(size_t) note];
        if (slot == 0 && v != 0) ++count;
        else if (slot != 0 && v == 0) --count;
        slot = v;
    }

    std::array<std::array<uint8_t, 128>, 16> velocity {};
    int count = 0;
};

} // namespace mu_core
