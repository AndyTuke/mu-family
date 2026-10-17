#pragma once

#include <array>

// The keys held in mu-Tant's Note mode: a last-note-priority stack, newest on top. A note-on pushes (a
// re-pressed note moves to the top), a note-off removes; at most 16 notes are tracked, a 17th drops the
// oldest. Plain data, touched by one thread at a time: each Pattern keeps one for its pitch track (audio
// thread), and the processor keeps one for the final-mix gate (audio thread).
namespace mu_tant
{

class HeldNoteStack
{
public:
    static constexpr int kCapacity = 16;

    void noteOn(int note) noexcept
    {
        remove(note);
        if (count == kCapacity)   // full: drop the oldest
        {
            for (int j = 0; j + 1 < count; ++j) stack[(size_t) j] = stack[(size_t) j + 1];
            --count;
        }
        stack[(size_t) count++] = note;
    }

    void noteOff(int note) noexcept { remove(note); }
    void clear() noexcept           { count = 0; }

    int  numHeld() const noexcept { return count; }
    int  newest() const noexcept  { return count > 0 ? stack[(size_t) (count - 1)] : -1; }   // -1: none held

private:
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

    std::array<int, kCapacity> stack {};
    int count = 0;
};

} // namespace mu_tant
