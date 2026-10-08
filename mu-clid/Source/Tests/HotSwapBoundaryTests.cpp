// C3 - hot-swap loop-boundary predicate tests.
//
// The swap-defer decision in HotSwapStager::checkBoundaries was extracted into
// pure predicates (HotSwapBoundary.h) so the rule can be tested without a live
// PluginProcessor. The headline case: a free-running full-preset
// swap (no master loop) must fall back to rhythm 0's wrap, NOT wait on a master
// loop that never arrives.

#include <juce_core/juce_core.h>
#include "Plugin/HotSwapBoundary.h"
#include "Plugin/HotSwap.h"   // mu-core: BarLineSwapper
#include <vector>

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

        // ── mu-core BarLineSwapper (mu-Toni / mu-On bar-line hot-swap) ──────────────
        // Payload = int, so the log records which apply ran with what.
        struct Log { std::vector<juce::String> events; };
        auto makeSwapper = [](Log& log, mu_hotswap::BarLineSwapper<int, 4>& sw)
        {
            sw.setAppliers([&log](int& v)         { log.events.push_back("full " + juce::String(v)); },
                           [&log](int i, int& v)  { log.events.push_back("slot" + juce::String(i) + " " + juce::String(v)); });
        };

        beginTest ("BarLineSwapper: stopped loads apply at once");
        {
            Log log; mu_hotswap::BarLineSwapper<int, 4> sw; makeSwapper(log, sw);
            sw.useSlot(2, 7);
            sw.useFull(9);
            expect (log.events.size() == 2 && log.events[0] == "slot2 7" && log.events[1] == "full 9", "applied immediately, in order");
            expect (! sw.hasPending(2) && ! sw.hasFullPending(), "nothing left staged");
        }

        beginTest ("BarLineSwapper: playing loads wait for the bar line, full first");
        {
            Log log; mu_hotswap::BarLineSwapper<int, 4> sw; makeSwapper(log, sw);
            expect (! sw.flagBoundaries(true, 0.0, 0.5), "playing, mid-bar: nothing to flag");
            sw.useSlot(1, 3);
            expect (sw.hasPending(1) && log.events.empty(), "staged, not applied");
            expect (! sw.flagBoundaries(true, 1.0, 0.5), "no bar line in [1.0, 1.5)");
            expect (! sw.commit() && log.events.empty(), "commit before the boundary does nothing");
            sw.useFull(5);
            expect (! sw.hasPending(1), "a staged full preset drops the slot swap");
            sw.useSlot(0, 4);
            expect (sw.flagBoundaries(true, 3.75, 0.5), "bar line at beat 4 flags");
            expect (sw.commit(), "commit reports the full preset");
            expect (log.events.size() == 2 && log.events[0] == "full 5" && log.events[1] == "slot0 4", "full then slot");
        }

        beginTest ("BarLineSwapper: stopping commits what is staged; cancel drops it");
        {
            Log log; mu_hotswap::BarLineSwapper<int, 4> sw; makeSwapper(log, sw);
            sw.flagBoundaries(true, 0.0, 0.5);
            sw.useSlot(3, 8);
            sw.useSlot(2, 6);
            sw.cancel(2);
            expect (sw.flagBoundaries(false, 0.5, 0.5), "the play->stop edge flags at once");
            sw.commit();
            expect (log.events.size() == 1 && log.events[0] == "slot3 8", "only the uncancelled slot lands");
        }
    }
};

static HotSwapBoundaryTest hotSwapBoundaryTest;
