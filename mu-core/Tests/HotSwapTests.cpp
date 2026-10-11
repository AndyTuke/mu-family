// mu-core hot-swap tests - the loop-wrap predicates and mu_hotswap::LayerSwapper (stage-or-apply,
// commit order, boundary flagging with a per-slot loop rule, the stop edge). No processor needed.

#include <juce_core/juce_core.h>
#include <vector>
#include "Plugin/HotSwap.h"

namespace
{
    // Payload = int, so the log records which apply ran with what.
    struct Log { std::vector<juce::String> events; };

    using Swapper = mu_hotswap::LayerSwapper<int, 4>;

    void wire(Log& log, Swapper& sw)
    {
        sw.setAppliers([&log](int& v)         { log.events.push_back("full " + juce::String(v)); },
                       [&log](int i, int& v)  { log.events.push_back("slot" + juce::String(i) + " " + juce::String(v)); });
    }

    // A payload whose move-assignment runs a hook, so a test can stop the transport "on the audio
    // thread" at the exact point between a load's play-state read and its re-check.
    struct Hooked
    {
        int v = 0;
        static inline std::function<void()> onMove;
        Hooked() = default;
        Hooked(int x) : v(x) {}
        Hooked(const Hooked&) = default;
        Hooked(Hooked&&) = default;
        Hooked& operator=(const Hooked&) = default;
        Hooked& operator=(Hooked&& o) { v = o.v; if (onMove) onMove(); return *this; }
    };
}

struct HotSwapTests : juce::UnitTest
{
    HotSwapTests() : juce::UnitTest("mu-core hot-swap swapper", "mu-core") {}

    void runTest() override
    {
        using namespace mu_hotswap;

        beginTest("loopWrapped and boundaryReached: wrap, stop edge, stopped");
        {
            expect(  loopWrapped(7.9, 8.1, 8.0));
            expect(! loopWrapped(1.0, 2.0, 8.0));
            expect(! loopWrapped(1.0, 2.0, 0.0), "a zero-length loop never wraps");
            expect(  boundaryReached(true,  true, 7.9, 8.1, 8.0));
            expect(  boundaryReached(false, true, 3.0, 3.0, 8.0), "the stop edge commits at once");
            expect(! boundaryReached(false, false, 0.0, 0.0, 8.0), "stopped with no edge never commits");
        }

        beginTest("stopped loads apply at once, in order");
        {
            Log log; Swapper sw; wire(log, sw);
            sw.useSlot(2, 7);
            sw.useFull(9);
            expect(log.events.size() == 2 && log.events[0] == "slot2 7" && log.events[1] == "full 9");
            expect(! sw.hasPending(2) && ! sw.hasFullPending(), "nothing left staged");
        }

        beginTest("playing loads wait for the bar line, full first");
        {
            Log log; Swapper sw; wire(log, sw);
            expect(! sw.flagBoundaries(true, 0.0, 0.5), "mid-bar: nothing to flag");
            sw.useSlot(1, 3);
            expect(sw.hasPending(1) && log.events.empty(), "staged, not applied");
            expect(! sw.flagBoundaries(true, 1.0, 0.5), "no bar line in [1.0, 1.5)");
            expect(! sw.commit() && log.events.empty(), "commit before the boundary does nothing");
            sw.useFull(5);
            expect(! sw.hasPending(1), "a full preset supersedes the slot swap");
            sw.useSlot(0, 4);
            expect(sw.flagBoundaries(true, 3.75, 0.5), "the bar line at beat 4 flags");
            expect(sw.commit(), "commit reports the full preset");
            expect(log.events.size() == 2 && log.events[0] == "full 5" && log.events[1] == "slot0 4", "full then slot");
            expect(! sw.hasPending(0) && ! sw.hasFullPending(), "committed swaps are cleared");
        }

        beginTest("a slot commits on its own loop length from the boundary callable");
        {
            Log log; Swapper sw; wire(log, sw);
            sw.flagBoundaries(true, 0.0, 0.5);
            sw.useSlot(1, 11);
            sw.useSlot(2, 22);
            auto rule = [](int slot) { return slot == 1 ? 4.0 : 8.0; };
            expect(sw.flagBoundaries(true, 3.9, 0.2, rule), "slot 1's 4-beat loop wraps");
            sw.commit();
            expect(log.events.size() == 1 && log.events[0] == "slot1 11", "only slot 1 landed");
            expect(sw.hasPending(2), "slot 2's 8-beat loop has not wrapped");
            expect(sw.flagBoundaries(true, 7.9, 0.2, rule));
            sw.commit();
            expect(log.events.size() == 2 && log.events[1] == "slot2 22");
        }

        beginTest("the full preset is asked about with kFullPreset; a negative length never flags");
        {
            Log log; Swapper sw; wire(log, sw);
            sw.flagBoundaries(true, 0.0, 0.5);
            sw.useFull(6);
            sw.useSlot(3, 8);
            std::vector<int> asked;
            auto rule = [&asked](int slot) { asked.push_back(slot); return slot == kFullPreset ? 8.0 : -1.0; };
            expect(! sw.flagBoundaries(true, 1.0, 0.5, rule));
            expect(asked.size() == 2 && asked[0] == kFullPreset && asked[1] == 3, "asked only about what is pending");
            expect(sw.flagBoundaries(true, 7.9, 0.2, rule));
            sw.commit();
            expect(log.events.size() == 1 && log.events[0] == "full 6", "the ineligible slot did not commit");
            expect(sw.hasPending(3), "the ineligible slot stays staged");
        }

        beginTest("stopping commits what is staged; cancel drops it");
        {
            Log log; Swapper sw; wire(log, sw);
            sw.flagBoundaries(true, 0.0, 0.5);
            sw.useSlot(3, 8);
            sw.useSlot(2, 6);
            sw.cancel(2);
            expect(sw.flagBoundaries(false, 0.5, 0.5), "the play-to-stop edge flags at once");
            sw.commit();
            expect(log.events.size() == 1 && log.events[0] == "slot3 8", "only the uncancelled slot lands");
        }

        beginTest("a swap cancelled after it was flagged does not commit");
        {
            Log log; Swapper sw; wire(log, sw);
            sw.flagBoundaries(true, 0.0, 0.5);
            sw.useSlot(0, 1);
            expect(sw.flagBoundaries(true, 3.9, 0.2));
            sw.cancel(0);
            sw.commit();
            expect(log.events.empty());
        }

        beginTest("a stopped load applies at once and cancels anything staged for its scope");
        {
            Log log; Swapper sw; wire(log, sw);
            sw.stageSlot(1, 4);
            sw.stageFull(5);
            sw.stageSlot(2, 6);
            expect(! sw.isRunning());
            sw.useSlot(2, 7);
            expect(log.events.size() == 1 && log.events[0] == "slot2 7", "applied now");
            expect(! sw.hasPending(2), "the staged slot was dropped, not left to overwrite later");
            sw.useFull(8);
            expect(! sw.hasFullPending() && ! sw.hasPending(1), "a stopped full load drops the staged full and slots");
            expect(log.events.back() == "full 8");
            sw.flagBoundaries(false, 0.0, 0.5);
            expect(! sw.commit(), "nothing is left to land later");
            expect(log.events.size() == 2);
        }

        beginTest("MIDI-clock-lost: a load with the transport stopped applies at once, nothing pending");
        {
            Log log; Swapper sw; wire(log, sw);
            sw.flagBoundaries(true, 0.0, 0.5);
            sw.flagBoundaries(false, 0.5, 0.5);    // the clock stopped
            expect(! sw.isRunning());
            sw.useFull(3);
            sw.useSlot(0, 4);
            expect(! sw.hasFullPending() && ! sw.hasPending(0));
            expect(log.events.size() == 2 && log.events[0] == "full 3" && log.events[1] == "slot0 4");
            expect(! sw.flagBoundaries(false, 0.5, 0.5) && ! sw.commit());
        }

        beginTest("stage then stop edge commits");
        {
            Log log; Swapper sw; wire(log, sw);
            sw.flagBoundaries(true, 0.0, 0.5);
            expect(sw.isRunning());
            sw.useSlot(1, 9);
            expect(sw.hasPending(1) && log.events.empty());
            expect(sw.flagBoundaries(false, 0.5, 0.5), "the stop edge flags the staged load");
            sw.commit();
            expect(log.events.size() == 1 && log.events[0] == "slot1 9");
        }

        beginTest("race: the transport stops while a load is staging -> applied, not stranded");
        {
            using HSwapper = LayerSwapper<Hooked, 4>;
            std::vector<juce::String> ev;
            HSwapper sw;
            sw.setAppliers([&ev](Hooked& h)        { ev.push_back("full " + juce::String(h.v)); },
                           [&ev](int i, Hooked& h) { ev.push_back("slot" + juce::String(i) + " " + juce::String(h.v)); });
            sw.flagBoundaries(true, 0.0, 0.5);
            // The stop lands (and its edge passes with nothing flagged) during the staging.
            Hooked::onMove = [&sw] { sw.flagBoundaries(false, 0.5, 0.5); Hooked::onMove = nullptr; };
            sw.useSlot(2, Hooked { 5 });
            expect(! sw.hasPending(2), "not left staged with no future boundary");
            expect(ev.size() == 1 && ev[0] == "slot2 5", "applied by the re-check");
            sw.commit();
            expect(ev.size() == 1, "and not applied twice");

            sw.flagBoundaries(true, 0.0, 0.5);
            Hooked::onMove = [&sw] { sw.flagBoundaries(false, 0.5, 0.5); Hooked::onMove = nullptr; };
            sw.useFull(Hooked { 6 });
            expect(! sw.hasFullPending() && ev.size() == 2 && ev[1] == "full 6", "same for a full preset");
            Hooked::onMove = nullptr;
        }

        beginTest("stageSlot and stageFull stage whatever the play state");
        {
            Log log; Swapper sw; wire(log, sw);   // never played: useSlot would apply now
            sw.stageSlot(1, 4);
            sw.stageFull(5);
            expect(sw.hasFullPending() && ! sw.hasPending(1), "the full preset superseded the slot");
            expect(log.events.empty());
            sw.stageSlot(1, 4);
            expect(sw.hasPending(1) && sw.hasFullPending());
        }
    }
};

static HotSwapTests hotSwapTests;
