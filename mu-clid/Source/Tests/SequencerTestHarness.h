#pragma once

// A RhythmSequencer that owns its rhythms, for tests: the sequencer itself only binds rhythms the
// product owns, so a test that wants a few rhythms to play needs somewhere to keep them. The member
// functions mirror what a product does on its structural edits (move the data, then the slot state).

#include "Sequencer/GeneratorAliases.h"

#include <array>
#include <utility>

class TestSequencer : public mu_clid::RhythmSequencer
{
public:
    TestSequencer()
    {
        for (int i = 0; i < MaxRhythms; ++i)
            bindRhythm(i, &store[(size_t) i]);
    }

    // Appends a rhythm as the next live slot.
    void addRhythm(const mu_clid::Rhythm& r)
    {
        const int n = getNumRhythms();
        if (n >= MaxRhythms) return;
        store[(size_t) n] = r;
        setNumRhythms(n + 1);
        updatePattern(n);
    }

    // Removes slot `index`: the rhythms above it move down, and so does their slot state.
    void removeRhythm(int index)
    {
        const int n = getNumRhythms();
        if (index < 0 || index >= n) return;
        for (int i = index; i < n - 1; ++i)
            store[(size_t) i] = std::move(store[(size_t) i + 1]);
        store[(size_t) n - 1] = mu_clid::Rhythm{};
        removeSlotState(index);
        setNumRhythms(n - 1);
    }

    // Exchanges two slots: the rhythms and their slot state.
    void swapRhythmSlots(int i, int j)
    {
        const int n = getNumRhythms();
        if (i == j || i < 0 || j < 0 || i >= n || j >= n) return;
        std::swap(store[(size_t) i], store[(size_t) j]);
        swapSlotState(i, j);
    }

private:
    std::array<mu_clid::Rhythm, MaxRhythms> store;
};
