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

    // A scene stages one payload per layer inside a batch: the audio thread flags all of them at one boundary, or none.
    void stagingBatch()
    {
        using namespace mu_hotswap;

        beginTest("a staging batch: a boundary inside the window flags nothing; the next one after it flags the whole batch");
        {
            Log log; Swapper sw; wire(log, sw);
            sw.flagBoundaries(true, 0.0, 0.1);   // running
            sw.beginBatch();
            for (int i = 0; i < 4; ++i) sw.stageSlot(i, 10 + i);
            expect(! sw.flagBoundaries(true, 3.95, 0.2), "the bar line falls inside the batch: nothing flags, not even slot 0");
            for (int i = 0; i < 4; ++i) expect(! sw.commit() && sw.hasPending(i), "slot " + juce::String(i) + " still waits");
            sw.endBatch();
            expect(sw.flagBoundaries(true, 7.95, 0.2), "the next bar line flags the lot");
            sw.commit();
            expectEquals((int) log.events.size(), 4);
        }

        beginTest("a staging batch: stagings outside a batch behave as before");
        {
            Log log; Swapper sw; wire(log, sw);
            sw.flagBoundaries(true, 0.0, 0.1);
            sw.beginBatch(); sw.endBatch();
            sw.stageSlot(1, 5);
            expect(sw.flagBoundaries(true, 3.95, 0.2), "an empty batch leaves the gate open");
        }

        beginTest("a staging batch on another thread's flag passes: the flagged set seen after any pass is none or all of the batch");
        {
            Swapper sw; Log log; wire(log, sw);
            constexpr int kN = 4;
            std::atomic<bool> stop { false };
            std::atomic<int>  phase { 0 };       // 1 while a batch is being staged and flagged, 0 otherwise
            std::atomic<int>  passes { 0 }, violations { 0 };
            sw.flagBoundaries(true, 0.0, 0.1);
            std::thread audio([&]
            {
                while (! stop.load(std::memory_order_acquire))
                {
                    const bool open = phase.load(std::memory_order_acquire) == 1;
                    sw.flagBoundaries(true, 3.95, 0.2);   // every pass is a bar line
                    if (open)
                    {
                        int count = 0;
                        for (int i = 0; i < kN; ++i) count += sw.isFlagged(i) ? 1 : 0;
                        if (count != 0 && count != kN && phase.load(std::memory_order_acquire) == 1) ++violations;
                    }
                    ++passes;
                }
            });
            for (int round = 0; round < 300; ++round)
            {
                phase.store(1, std::memory_order_release);
                sw.beginBatch();
                for (int i = 0; i < kN; ++i) { sw.stageSlot(i, i + 0); if (i == 1) std::this_thread::yield(); }
                sw.endBatch();
                const int seen = passes.load();
                while (passes.load() < seen + 3) std::this_thread::yield();   // whole passes after the batch closed
                phase.store(0, std::memory_order_release);
                while (passes.load() < seen + 5) std::this_thread::yield();
                sw.commit();
                for (int i = 0; i < kN; ++i) sw.cancel(i);
            }
            stop.store(true, std::memory_order_release);
            audio.join();
            expectEquals(violations.load(), 0, "no pass ever left part of a batch flagged");
        }
    }

    void runTest() override
    {
        stagingBatch();
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

        beginTest("CommitMailbox: the drain commits flagged swaps, and does nothing without a request");
        {
            Log log; Swapper sw; wire(log, sw);
            CommitMailbox mailbox;
            int drains = 0;
            auto drain = [&] { return mailbox.drain([&] { ++drains; sw.commit(); }); };

            sw.flagBoundaries(true, 0.0, 0.5);
            sw.useSlot(1, 3);
            expect(! drain() && drains == 0 && log.events.empty(), "no request: the drain does nothing");

            // The audio thread's side: flag at the bar line, then ask for the commit.
            if (sw.flagBoundaries(true, 3.9, 0.2)) mailbox.request();
            expect(mailbox.isPending());
            expect(drain() && drains == 1, "a request runs the commit once");
            expect(log.events.size() == 1 && log.events[0] == "slot1 3", "the flagged swap landed");
            expect(! mailbox.isPending() && ! drain() && drains == 1, "the request is consumed");

            // A request with nothing flagged (e.g. the swap was cancelled) commits nothing.
            sw.useSlot(2, 4);
            mailbox.request();
            expect(drain() && log.events.size() == 1, "an unflagged swap is not committed by a stray request");
            expect(sw.hasPending(2));
        }

        beginTest("Stager: a stage made inside apply survives and the applied payload is intact");
        {
            Stager<int, int, 2> st;
            st.stage(0, 1);
            st.flagIfReady(0, true);
            int seen = 0;
            expect(st.consume(0, [&](int& v) { seen = v; st.stage(0, 2); expect(v == 1, "payload unchanged by the re-stage"); }));
            expect(seen == 1);
            expect(st.hasPending(0), "the re-staged payload is pending");
            expect(st.flagIfReady(0, true));
            int got = 0;
            expect(st.consume(0, [&got](int& v) { got = v; }) && got == 2, "and commits on its own boundary");

            st.stageFull(5);
            st.flagFullIfReady(true);
            expect(st.consumeFull([&](int&) { st.stageFull(6); }));
            int full = 0;
            expect(st.hasFullPending() && st.consumeFullNow([&full](int& v) { full = v; }) && full == 6);
        }

        beginTest("Stager: flagged, then taken back, then consumed applies exactly once");
        for (bool useFull : { false, true })
        {
            Stager<int, int, 1> st;
            int applies = 0, got = 0;
            auto apply = [&](int& v) { ++applies; got = v; };
            const auto stageIt = [&](int v) { if (useFull) st.stageFull(std::move(v)); else st.stage(0, std::move(v)); };
            const auto token   = [&] { return useFull ? st.fullToken() : st.pendingToken(0); };
            const auto flagIt  = [&](Stager<int, int, 1>::Token t) { return useFull ? st.flagFullIfReady(t, true) : st.flagIfReady(0, t, true); };

            stageIt(41);
            const auto t = token();
            expect(flagIt(t), "the boundary flags the staged payload");
            expect(useFull ? st.consumeFullNow(apply) : st.consumeNow(0, apply), "the take-back still gets a flagged payload");
            expect(! (useFull ? st.consumeFull(apply) : st.consume(0, apply)), "the commit that was already on its way finds nothing");
            expectEquals(applies, 1, "applied exactly once");
            expectEquals(got, 41);
            expect(! flagIt(t), "the dead token cannot flag again");
            expect(! (useFull ? st.hasFullPending() : st.hasPending(0)), "nothing is left staged");

            // A new payload staged afterwards is untouched by the earlier take-back and commit.
            stageIt(42);
            expect(flagIt(token()));
            expect(useFull ? st.consumeFull(apply) : st.consume(0, apply));
            expectEquals(applies, 2);
            expectEquals(got, 42);
        }

        beginTest("Stager threaded stress: no commit without a boundary, no lost payload, no double apply");
        for (bool useFull : { false, true })
        {
            const auto plain  = runStagerStress(useFull, StressMode::correct, false);
            const auto jitter = runStagerStress(useFull, StressMode::correct, true);
            for (const auto* r : { &plain, &jitter })
            {
                const juce::String which = juce::String(useFull ? "full" : "slot") + (r == &jitter ? " with jitter" : "");
                expectEquals(r->unboundaried, 0, which + ": a commit happened without a boundary or left a payload behind");
                expectEquals(r->lost,         0, which + ": a staged payload was lost or never committed");
                expectEquals(r->stale,        0, which + ": a payload was applied twice or out of order");
                expectEquals(r->wrongPayload, 0, which + ": a payload committed under a gate opened for a different payload");
                // The expected count comes from the round pattern (the rounds with a boundary that commits),
                // not from the test's own bookkeeping, so it cannot agree by construction.
                expectEquals(r->applied, kStressRounds * 2 / 5, which + ": applied count");
            }
        }

        beginTest("Stager stress has teeth: seeding the old bug (a flag that ignores the generation) is caught");
        for (bool useFull : { false, true })
        {
            // The old bug: the boundary decision is made about one payload, then the flag re-reads the
            // slot and lands on whatever is staged by then. The stress test must see that.
            int caught = 0;
            for (int attempt = 0; attempt < 5 && caught == 0; ++attempt)
            {
                const auto r = runStagerStress(useFull, StressMode::seededOldBug, true);
                caught = r.unboundaried + r.wrongPayload + r.stale + r.lost;
            }
            expect(caught > 0, juce::String(useFull ? "full" : "slot") + ": the seeded old bug went undetected by the stress test");
        }
    }

    static constexpr int kStressRounds = 20000;

    // correct: the audio thread reads the token, then the gate, then flags with that token.
    // seededOldBug: the flag ignores the generation of the decision (the convenience form re-reads the slot).
    enum class StressMode { correct, seededOldBug };

    struct StressResult { int unboundaried = 0, lost = 0, stale = 0, wrongPayload = 0, applied = 0; };

    // One audio-like thread keeps trying to flag the staged payload, but only while `gate` is open. The
    // gate carries the id of the payload its boundary belongs to (0 = closed), and apply checks the
    // payload it receives against it. The message thread stages, opens the gate, and commits / takes
    // back / cancels in a fixed cycle. With `jitter` the audio thread idles a pseudo-random time between
    // its decision and its compare-exchange, the window a descheduled thread would have. Fixed round
    // count (no wall clock); each wait is capped so a bug fails instead of hanging.
    StressResult runStagerStress(bool useFull, StressMode mode, bool jitter)
    {
        mu_hotswap::Stager<int, int, 1> st;
        std::atomic<int>  gate { 0 };
        std::atomic<bool> stop { false };

        std::thread audio([&]
        {
            uint32_t rng = 2463534242u;
            while (! stop.load(std::memory_order_acquire))
            {
                // Token first, boundary decision second: the order the audio path uses.
                const auto token = useFull ? st.fullToken() : st.pendingToken(0);
                const bool at    = gate.load(std::memory_order_acquire) != 0;
                if (jitter)
                {
                    rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;     // xorshift32
                    for (uint32_t k = rng % 512; k > 0; --k)
                        std::atomic_signal_fence(std::memory_order_seq_cst);  // idle without being optimised away
                }
                if (mode == StressMode::seededOldBug)
                {
                    if (useFull) st.flagFullIfReady(at);
                    else         st.flagIfReady(0, at);
                }
                else
                {
                    if (useFull) st.flagFullIfReady(token, at);
                    else         st.flagIfReady(0, token, at);
                }
            }
        });

        constexpr int kWaitCap = 20000000;
        auto stageIt  = [&](int v) { if (useFull) st.stageFull(std::move(v)); else st.stage(0, std::move(v)); };

        StressResult res;
        int lastApplied = 0;
        auto apply = [&](int& v)
        {
            if (v <= lastApplied) ++res.stale;
            if (gate.load(std::memory_order_acquire) != v) ++res.wrongPayload;
            lastApplied = v;
            ++res.applied;
        };
        auto commit   = [&] { return useFull ? st.consumeFull(apply) : st.consume(0, apply); };
        auto takeBack = [&] { return useFull ? st.consumeFullNow(apply) : st.consumeNow(0, apply); };
        auto cancelIt = [&] { if (useFull) st.cancelFull(); else st.cancel(0); };
        auto pending  = [&] { return useFull ? st.hasFullPending() : st.hasPending(0); };

        for (int round = 1; round <= kStressRounds; ++round)
        {
            const int first = round * 2, second = round * 2 + 1;   // unique, increasing payload ids
            switch (round % 5)
            {
                case 0:   // no boundary: nothing may commit, then a superseding stage replaces the payload
                    stageIt(first);
                    for (int k = 0; k < 200; ++k) if (commit()) ++res.unboundaried;
                    stageIt(second);
                    cancelIt();
                    if (pending()) ++res.lost;
                    break;
                case 1:   // boundary: the flag must arrive and commit exactly the staged payload
                {
                    stageIt(first);
                    gate.store(first, std::memory_order_release);
                    bool done = false;
                    for (int k = 0; k < kWaitCap && ! done; ++k)
                    {
                        done = commit();
                        if (! done) std::this_thread::yield();
                    }
                    gate.store(0, std::memory_order_release);
                    if (! done) ++res.lost;
                    break;
                }
                case 2:   // take-back while the audio thread may be flagging it: always yields the payload
                    stageIt(first);
                    gate.store(first, std::memory_order_release);
                    if (! takeBack()) ++res.lost;
                    gate.store(0, std::memory_order_release);
                    if (commit() || pending()) ++res.unboundaried;   // nothing may be left to commit
                    break;
                case 3:   // cancel while flagging: the payload must vanish and never commit
                    stageIt(first);
                    gate.store(first, std::memory_order_release);
                    cancelIt();
                    gate.store(0, std::memory_order_release);
                    for (int k = 0; k < 200; ++k) if (commit()) ++res.unboundaried;
                    break;
                default:  // the boundary passed for one payload, then a newer one is staged with the gate shut
                    stageIt(first);
                    gate.store(first, std::memory_order_release);
                    // Alternate passes: wait so the audio thread flags the first payload, or shut the gate at once so a
                    // decision about it may still be in flight (token read, compare-exchange not yet made).
                    for (int k = 0, waits = ((round / 5) % 2 != 0) ? 20 : 0; k < waits; ++k) std::this_thread::yield();
                    gate.store(0, std::memory_order_release);
                    stageIt(second);
                    // A decision made about the first payload must not flag the second: no boundary was seen for it.
                    for (int k = 0; k < 200; ++k) if (commit()) ++res.unboundaried;
                    cancelIt();
                    break;
            }
        }

        stop.store(true, std::memory_order_release);
        audio.join();
        return res;
    }
};

static HotSwapTests hotSwapTests;
