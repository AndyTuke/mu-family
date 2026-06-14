// C3 - hot-swap loop-boundary predicate tests.
//
// The swap-defer decision in HotSwapStager::checkBoundaries was extracted into
// pure predicates (HotSwapBoundary.h) so the rule can be tested without a live
// PluginProcessor. The headline case: a free-running full-preset
// swap (no master loop) must fall back to rhythm 0's wrap, NOT wait on a master
// loop that never arrives.

#include <juce_core/juce_core.h>
#include "Plugin/HotSwapBoundary.h"

class HotSwapBoundaryTest : public juce::UnitTest
{
public:
    HotSwapBoundaryTest() : juce::UnitTest ("Hot-swap loop boundaries", "HotSwapBoundary") {}

    void runTest() override
    {
        using namespace mu_clid::hotswap;

        beginTest ("C3: per-rhythm swap in master-loop mode follows the master wrap");
        {
            // swapMode 0 = master-loop mode: every rhythm defers to the master wrap,
            // ignoring its own loop mask.
            expect (  perRhythmBoundaryReached (0, 3, /*master*/ true,  /*mask*/ 0x00), "fires on master wrap");
            expect (! perRhythmBoundaryReached (0, 3, /*master*/ false, /*mask*/ 0xFF), "ignores rhythm mask when in master mode");
        }

        beginTest ("C3: per-rhythm swap in per-rhythm mode follows that rhythm's bit");
        {
            // swapMode != 0 = per-rhythm mode: rhythm r defers to bit r of the mask.
            expect (! perRhythmBoundaryReached (1, 3, /*master*/ true,  /*mask*/ 0x00), "ignores master wrap in per-rhythm mode");
            expect (  perRhythmBoundaryReached (1, 3, /*master*/ false, /*mask*/ 1 << 3), "fires when rhythm 3's bit is set");
            expect (! perRhythmBoundaryReached (1, 3, /*master*/ false, /*mask*/ 1 << 2), "does not fire on another rhythm's bit");
        }

        beginTest ("C3: full-preset swap WITH a master loop waits for the master wrap");
        {
            expect (  fullPresetBoundaryReached (/*hasMaster*/ true, /*master*/ true,  /*mask*/ 0x00), "fires on master wrap");
            expect (! fullPresetBoundaryReached (/*hasMaster*/ true, /*master*/ false, /*mask*/ 0xFF), "does NOT fire on a rhythm wrap when a master loop is defined");
        }

        beginTest ("C3: free-running full-preset swap falls back to rhythm 0");
        {
            // The bug: with mstrLoop=0 (free-running) the swap waited on a master
            // wrap that never comes. The fix falls back to rhythm 0's loop.
            expect (  fullPresetBoundaryReached (/*hasMaster*/ false, /*master*/ false, /*mask*/ 0x01), "fires when rhythm 0 wraps");
            expect (! fullPresetBoundaryReached (/*hasMaster*/ false, /*master*/ false, /*mask*/ 0x02), "does NOT fire when only rhythm 1 wraps");
            expect (! fullPresetBoundaryReached (/*hasMaster*/ false, /*master*/ true,  /*mask*/ 0x00),
                "free-running ignores masterLoopWrapped - that signal is meaningless without a master loop (the free-running trap)");
        }
    }
};

static HotSwapBoundaryTest hotSwapBoundaryTest;
