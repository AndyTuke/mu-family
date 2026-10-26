#include "Sequencer/HeldNoteStack.h"

#include <juce_core/juce_core.h>

struct HeldNoteStackTests : juce::UnitTest
{
    HeldNoteStackTests() : juce::UnitTest("HeldNoteStack", "mu-core") {}

    void runTest() override
    {
        using mu_core::HeldNoteOverflow;
        using Drop   = mu_core::HeldNoteStack<4, HeldNoteOverflow::DropOldest>;
        using Ignore = mu_core::HeldNoteStack<4, HeldNoteOverflow::IgnoreNew>;

        beginTest("empty: nothing held, newest is -1");
        {
            Drop s;
            expect(! s.anyHeld());
            expectEquals(s.numHeld(), 0);
            expectEquals(s.newest(), -1);
        }

        beginTest("push is newest on top, remove falls back to the previous key");
        {
            Drop s;
            s.push(60); s.push(64); s.push(67);
            expectEquals(s.numHeld(), 3);
            expectEquals(s.newest(), 67);
            s.remove(67);
            expectEquals(s.newest(), 64);
            s.remove(60);
            expectEquals(s.numHeld(), 1);
            expectEquals(s.newest(), 64);
            s.remove(99);   // not held: no change
            expectEquals(s.numHeld(), 1);
        }

        beginTest("a re-pressed note moves to the top without duplicating");
        {
            Drop s;
            s.push(60); s.push(64); s.push(60);
            expectEquals(s.numHeld(), 2);
            expectEquals(s.newest(), 60);
            s.remove(60);
            expectEquals(s.newest(), 64);
        }

        beginTest("DropOldest: a full stack forgets the oldest");
        {
            Drop s;
            for (int n = 1; n <= 4; ++n) s.push(n);
            s.push(5);
            expectEquals(s.numHeld(), 4);
            expectEquals(s.newest(), 5);
            s.remove(5); s.remove(4); s.remove(3);
            expectEquals(s.newest(), 2);   // 1 was dropped
            s.remove(2);
            expect(! s.anyHeld());
        }

        beginTest("IgnoreNew: a full stack does not track the new note");
        {
            Ignore s;
            for (int n = 1; n <= 4; ++n) s.push(n);
            s.push(5);
            expectEquals(s.numHeld(), 4);
            expectEquals(s.newest(), 4);
            s.push(2);   // a re-press still fits: its own slot is freed first
            expectEquals(s.numHeld(), 4);
            expectEquals(s.newest(), 2);
        }

        beginTest("clear empties the stack");
        {
            Ignore s;
            s.push(60); s.push(61);
            s.clear();
            expect(! s.anyHeld());
            expectEquals(s.newest(), -1);
        }
    }
};

static HeldNoteStackTests heldNoteStackTests;
