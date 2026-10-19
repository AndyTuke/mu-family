#pragma once

#include <array>

// The keys held right now, newest on top: a note-on pushes (a re-pressed note moves to the top), a note-off
// removes. Plain data with a fixed capacity, so it never allocates and is safe on the audio thread; touched
// by one thread at a time. The overflow policy says what a push does when every slot is taken.
namespace mu_core
{

enum class HeldNoteOverflow
{
    IgnoreNew,    // the new note is not tracked
    DropOldest    // the oldest held note is forgotten to make room
};

template <int Capacity, HeldNoteOverflow Overflow>
class HeldNoteStack
{
    static_assert(Capacity > 0, "a held-note stack needs at least one slot");

public:
    static constexpr int kCapacity = Capacity;

    void push(int note) noexcept
    {
        remove(note);
        if (count == kCapacity)
        {
            if constexpr (Overflow == HeldNoteOverflow::IgnoreNew)
                return;

            // Full: shift everything down one slot, dropping the oldest.
            for (int j = 0; j + 1 < count; ++j) stack[(size_t) j] = stack[(size_t) j + 1];
            --count;
        }
        stack[(size_t) count++] = note;
    }

    void remove(int note) noexcept
    {
        for (int i = 0; i < count; ++i)
            if (stack[(size_t) i] == note)
            {
                --count;
                for (int j = i; j < count; ++j) stack[(size_t) j] = stack[(size_t) j + 1];
                return;
            }
    }

    void clear() noexcept           { count = 0; }

    bool anyHeld() const noexcept   { return count > 0; }
    int  numHeld() const noexcept   { return count; }
    int  newest() const noexcept    { return count > 0 ? stack[(size_t) (count - 1)] : -1; }   // -1: none held

private:
    std::array<int, (size_t) Capacity> stack {};
    int count = 0;
};

} // namespace mu_core
