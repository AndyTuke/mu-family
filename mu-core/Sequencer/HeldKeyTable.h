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
    static constexpr int kOrderCapacity = 256;   // keys remembered in press order; more than any hand holds

    // Follows one channel message: a note-on sets the key (a re-press moves it to the newest), a note-off (or note-on at
    // velocity 0) clears it, and All Sound Off / All Notes Off (CC 120 / 123) clears the channel. Anything else, system
    // messages included, is ignored.
    void apply(const MidiEvent& e) noexcept
    {
        if (e.size < 2) return;
        const int type = e.bytes[0] & 0xf0, ch = e.bytes[0] & 0x0f;
        if (type == 0x90 && e.bytes[2] != 0)           press(ch, e.bytes[1] & 0x7f, e.bytes[2]);
        else if (type == 0x80 || type == 0x90)         release(ch, e.bytes[1] & 0x7f);
        else if (type == 0xb0 && (e.bytes[1] == 120 || e.bytes[1] == 123))
        {
            // Compact the order list, keeping every key of the other channels.
            int kept = 0;
            for (int i = 0; i < count; ++i)
            {
                if ((order[(size_t) i] >> 7) == ch) velocity[(size_t) ch][(size_t) (order[(size_t) i] & 127)] = 0;
                else order[(size_t) kept++] = order[(size_t) i];
            }
            count = kept;
        }
    }

    void clear() noexcept
    {
        for (auto& c : velocity) c.fill(0);
        count = 0;
    }

    bool anyHeld() const noexcept { return count > 0; }
    int  numHeld() const noexcept { return count; }

    // Writes a note-on at `frame` for each held key on a channel `mask` passes (bit c = channel c+1; 0xFFFF = omni),
    // oldest press first so the newest key ends on top of a layer's held stack as it does on a layer that was there, up to
    // `capacity` events. Returns how many were written.
    int collect(MidiEvent* out, int capacity, int frame, uint16_t mask) const noexcept
    {
        int n = 0;
        if (out == nullptr) return 0;
        for (int i = 0; i < count && n < capacity; ++i)
        {
            const int ch = order[(size_t) i] >> 7, note = order[(size_t) i] & 127;
            if (mask != 0xFFFF && ((mask >> ch) & 1u) == 0) continue;
            auto& e = out[n++];
            e.frame = frame;
            e.size = 3;
            e.bytes[0] = (uint8_t) (0x90 | ch);
            e.bytes[1] = (uint8_t) note;
            e.bytes[2] = velocity[(size_t) ch][(size_t) note];
        }
        return n;
    }

private:
    void press(int ch, int note, uint8_t v) noexcept
    {
        if (velocity[(size_t) ch][(size_t) note] != 0) removeFromOrder((uint16_t) ((ch << 7) | note));
        else if (count == kOrderCapacity)
        {
            // Full: the oldest press is forgotten, its velocity with it, so the count stays what the list holds.
            velocity[(size_t) (order[0] >> 7)][(size_t) (order[0] & 127)] = 0;
            removeFromOrder(order[0]);
        }
        order[(size_t) count++] = (uint16_t) ((ch << 7) | note);
        velocity[(size_t) ch][(size_t) note] = v;
    }

    void release(int ch, int note) noexcept
    {
        if (velocity[(size_t) ch][(size_t) note] == 0) return;
        velocity[(size_t) ch][(size_t) note] = 0;
        removeFromOrder((uint16_t) ((ch << 7) | note));
    }

    void removeFromOrder(uint16_t key) noexcept
    {
        for (int i = 0; i < count; ++i)
            if (order[(size_t) i] == key)
            {
                --count;
                for (int j = i; j < count; ++j) order[(size_t) j] = order[(size_t) j + 1];
                return;
            }
    }

    std::array<std::array<uint8_t, 128>, 16> velocity {};   // 0 = not held
    std::array<uint16_t, kOrderCapacity> order {};          // (channel << 7) | note, oldest first
    int count = 0;
};

} // namespace mu_core
