// mu-core hot-swap tests - the loop-wrap predicates and mu_hotswap::LayerSwapper (stage-or-apply,
// commit order, boundary flagging with a per-slot loop rule, the stop edge). No processor needed.

#include <juce_core/juce_core.h>
#include <atomic>
#include <thread>
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
            expect(log.events.size() == 2 && log.events[0] == "full 5" && log.events[1] == "slot2 7",
                   "the older staged full preset lands first, then the slot, applied now");
            expect(! sw.hasPending(2), "the staged slot was dropped, not left to overwrite later");
            sw.useFull(8);
            expect(! sw.hasFullPending() && ! sw.hasPending(1), "a stopped full load drops the staged full and slots");
            expect(log.events.back() == "full 8");
            sw.flagBoundaries(false, 0.0, 0.5);
            expect(! sw.commit(), "nothing is left to land later");
            expect(log.events.size() == 3);
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

        beginTest("a slot load after the stop edge lands after the older staged full preset");
        {
            Log log; Swapper sw; wire(log, sw);
            sw.flagBoundaries(true, 0.0, 0.5);
            sw.useFull(5);
            expect(sw.flagBoundaries(false, 0.5, 0.5), "the stop edge flags the full preset");
            sw.useSlot(1, 7);
            sw.commit();
            expect(log.events.size() == 2 && log.events[0] == "full 5" && log.events[1] == "slot1 7",
                   "the slot load is applied last and the async commit finds nothing to overwrite it");
        }

        beginTest("take-back path: a stop during slot staging applies the older full preset first");
        {
            using HSwapper = LayerSwapper<Hooked, 4>;
            std::vector<juce::String> ev;
            HSwapper sw;
            sw.setAppliers([&ev](Hooked& h)        { ev.push_back("full " + juce::String(h.v)); },
                           [&ev](int i, Hooked& h) { ev.push_back("slot" + juce::String(i) + " " + juce::String(h.v)); });
            sw.flagBoundaries(true, 0.0, 0.5);
            sw.useFull(Hooked { 5 });
            Hooked::onMove = [&sw] { sw.flagBoundaries(false, 0.5, 0.5); Hooked::onMove = nullptr; };
            sw.useSlot(2, Hooked { 8 });
            Hooked::onMove = nullptr;
            sw.commit();
            expect(ev.size() == 2 && ev[0] == "full 5" && ev[1] == "slot2 8");
        }

        beginTest("prepareToPlay reset: after resetPlayState a full load applies at once");
        {
            Log log; Swapper sw; wire(log, sw);
            sw.flagBoundaries(true, 0.0, 0.5);          // the host was playing, then stopped calling processBlock
            expect(sw.isRunning());
            sw.resetPlayState();
            expect(! sw.isRunning());
            sw.useFull(4);
            expect(! sw.hasFullPending() && log.events.size() == 1 && log.events[0] == "full 4");
        }

        beginTest("the stop edge commits an ineligible (negative length) staged slot");
        {
            Log log; Swapper sw; wire(log, sw);
            sw.flagBoundaries(true, 0.0, 0.5);
            sw.useSlot(3, 8);
            auto rule = [](int) { return -1.0; };
            expect(! sw.flagBoundaries(true, 7.9, 0.2, rule), "still ineligible while playing");
            expect(sw.flagBoundaries(false, 8.1, 0.5, rule), "the stop edge overrides");
            sw.commit();
            expect(log.events.size() == 1 && log.events[0] == "slot3 8");
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

        beginTest("full preset: a negative (ineligible) length never flags while playing, the stop edge commits it");
        {
            Log log; Swapper sw; wire(log, sw);
            sw.flagBoundaries(true, 0.0, 0.5);
            sw.useFull(6);
            std::vector<int> asked;
            auto rule = [&asked](int slot) { asked.push_back(slot); return -1.0; };
            expect(! sw.flagBoundaries(true, 7.9, 0.2, rule), "ineligible: no loop to wait for");
            expect(! sw.flagBoundaries(true, 15.9, 0.2, rule), "still ineligible a loop later");
            expect(asked.size() == 2 && asked[0] == kFullPreset && asked[1] == kFullPreset);
            expect(! sw.commit() && log.events.empty(), "nothing committed while playing");
            expect(sw.hasFullPending(), "the full preset stays staged");
            expect(sw.flagBoundaries(false, 16.1, 0.5, rule), "the stop edge overrides");
            expect(sw.commit() && log.events.size() == 1 && log.events[0] == "full 6");
        }

        beginTest("Stager state machine: a stale token cannot flag a re-staged payload");
        {
            Stager<int, int, 2> st;
            st.stage(0, 1);
            const auto old = st.pendingToken(0);
            expect(old != 0, "a ready slot gives a token");
            st.cancel(0);
            expect(st.pendingToken(0) == 0, "an empty slot gives none");
            st.stage(0, 2);
            expect(! st.flagIfReady(0, old, true), "the old token must not flag the new payload");
            expect(! st.isFlagged(0) && st.hasPending(0));
            expect(st.flagIfReady(0, st.pendingToken(0), true), "a fresh token flags it");
            expect(! st.flagIfReady(0, st.pendingToken(0), true), "already flagged: no second flag");
            int got = 0;
            expect(st.consume(0, [&got](int& v) { got = v; }) && got == 2);
            expect(! st.hasPending(0) && ! st.consume(0, [](int&) {}), "consumed once only");

            // Same for the full preset, and restaging after a flag drops the flag.
            st.stageFull(5);
            const auto oldFull = st.fullToken();
            expect(st.flagFullIfReady(oldFull, true));
            st.stageFull(6);
            expect(! st.consumeFull([](int&) {}), "a flag does not survive a re-stage");
            expect(! st.flagFullIfReady(oldFull, true), "the pre-restage token is dead");
            expect(st.hasFullPending(), "the new payload is still staged");
        }

        beginTest("Stager state machine: consume takes only flagged, the take-back takes any staged");
        {
            Stager<int, int, 1> st;
            st.stage(0, 3);
            expect(! st.consume(0, [](int&) {}), "ready but not flagged: not consumable");
            int got = 0;
            expect(st.consumeNow(0, [&got](int& v) { got = v; }) && got == 3, "take-back takes a ready payload");
            expect(! st.flagIfReady(0, true), "and leaves nothing to flag");
            st.stage(0, 4);
            expect(st.flagIfReady(0, true));
            expect(st.consumeNow(0, [&got](int& v) { got = v; }) && got == 4, "take-back also takes a flagged one");
            expect(! st.consume(0, [](int&) {}));
        }

        beginTest("Stager threaded stress: no commit without a boundary, no lost payload, no double apply");
        {
            runStagerStress(false);
            runStagerStress(true);
        }
    }

    // One audio-like thread keeps trying to flag the staged payload, but only while `gate` is open;
    // the message thread stages, opens the gate, and commits / takes back / cancels in a fixed
    // cycle. Fixed round count (no wall clock); each wait is capped so a bug fails instead of hanging.
    void runStagerStress(bool useFull)
    {
        mu_hotswap::Stager<int, int, 1> st;
        std::atomic<bool> gate { false }, stop { false };

        std::thread audio([&]
        {
            while (! stop.load(std::memory_order_acquire))
            {
                // Token first, boundary decision second: the order the audio path uses.
                const auto token = useFull ? st.fullToken() : st.pendingToken(0);
                const bool at    = gate.load(std::memory_order_acquire);
                if (useFull) st.flagFullIfReady(token, at);
                else         st.flagIfReady(0, token, at);
            }
        });

        constexpr int kRounds = 20000;
        constexpr int kWaitCap = 20000000;
        auto stageIt  = [&](int v) { if (useFull) st.stageFull(std::move(v)); else st.stage(0, std::move(v)); };

        int lastApplied = 0, applied = 0, expectedApplied = 0, badWithoutGate = 0, lost = 0, stale = 0;
        auto apply = [&](int& v) { if (v <= lastApplied) ++stale; lastApplied = v; ++applied; };
        auto commit   = [&] { return useFull ? st.consumeFull(apply) : st.consume(0, apply); };
        auto takeBack = [&] { return useFull ? st.consumeFullNow(apply) : st.consumeNow(0, apply); };
        auto cancelIt = [&] { if (useFull) st.cancelFull(); else st.cancel(0); };
        auto pending  = [&] { return useFull ? st.hasFullPending() : st.hasPending(0); };

        for (int round = 1; round <= kRounds; ++round)
        {
            switch (round % 5)
            {
                case 0:   // no boundary: nothing may commit, then a superseding stage replaces the payload
                    stageIt(round);
                    for (int k = 0; k < 200; ++k) if (commit()) ++badWithoutGate;
                    stageIt(round);   // supersede: the same value, must still apply at most once later
                    cancelIt();
                    if (pending()) ++lost;
                    break;
                case 1:   // boundary: the flag must arrive and commit exactly the staged payload
                {
                    stageIt(round);
                    gate.store(true, std::memory_order_release);
                    bool done = false;
                    for (int k = 0; k < kWaitCap && ! done; ++k)
                    {
                        done = commit();
                        if (! done) std::this_thread::yield();
                    }
                    gate.store(false, std::memory_order_release);
                    if (done) ++expectedApplied; else ++lost;
                    break;
                }
                case 2:   // take-back while the audio thread may be flagging it: always yields the payload
                    stageIt(round);
                    gate.store(true, std::memory_order_release);
                    if (takeBack()) ++expectedApplied; else ++lost;
                    gate.store(false, std::memory_order_release);
                    if (commit() || pending()) ++badWithoutGate;   // nothing may be left to commit
                    break;
                case 3:   // cancel while flagging: the payload must vanish and never commit
                    stageIt(round);
                    gate.store(true, std::memory_order_release);
                    cancelIt();
                    gate.store(false, std::memory_order_release);
                    for (int k = 0; k < 200; ++k) if (commit()) ++badWithoutGate;
                    break;
                default:  // re-stage while flagging: only the newer payload may commit, and only at a boundary
                    stageIt(round);
                    gate.store(true, std::memory_order_release);
                    stageIt(round);
                    gate.store(false, std::memory_order_release);
                    // The gate was open for both stages, so one legitimate commit of the newer payload is allowed.
                    for (int k = 0; k < 200; ++k) if (commit()) ++expectedApplied;
                    cancelIt();
                    break;
            }
        }

        stop.store(true, std::memory_order_release);
        audio.join();

        const juce::String which = useFull ? "full" : "slot";
        expect(badWithoutGate == 0, which + ": a commit happened without a boundary or left a payload behind");
        expect(lost == 0,           which + ": a staged payload was lost or never committed");
        expect(stale == 0,          which + ": a payload was applied twice or out of order");
        expect(applied == expectedApplied, which + ": applied count matches the rounds that should apply");
    }
};

static HotSwapTests hotSwapTests;
