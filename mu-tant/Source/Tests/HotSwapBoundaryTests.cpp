// mu-tant hot-swap staging tests - the pure loop-boundary predicates
// (mu-core HotSwap.h, as mu-tant uses them) and mu_hotswap::LayerSwapper driven by mu-tant's boundary rule. No PluginProcessor needed: this is the regression guard for the
// swap-defer decision (wrap detection incl. non-divisor pattern lengths,
// apply-on-stop, supersede, and per-voice vs full-preset boundaries).

#include <juce_data_structures/juce_data_structures.h>
#include <array>
#include "Plugin/HotSwap.h"   // mu-core: the shared loop-wrap predicates
#include "Sequencer/SwapBoundary.h"   // mu-tant: the boundary rule
#include "MuLimits.h"
#include <vector>

class HotSwapBoundaryTest : public juce::UnitTest
{
public:
    HotSwapBoundaryTest() : juce::UnitTest("mu-tant hot-swap boundary", "mu-tant") {}

    void runTest() override
    {
        using namespace mu_tant;
        using namespace mu_hotswap;

        beginTest("loopWrapped: loop-index crossing");
        {
            // 8-beat pattern. No wrap within a loop, wrap when the index advances.
            expect(! loopWrapped(0.0, 1.0, 8.0),  "no wrap mid-loop");
            expect(! loopWrapped(7.0, 7.9, 8.0),  "no wrap approaching the boundary");
            expect(  loopWrapped(7.9, 8.1, 8.0),  "wrap crossing 8.0");
            expect(  loopWrapped(15.9, 16.1, 8.0), "wrap crossing the 2nd loop point");
            expect(! loopWrapped(8.0, 8.0, 8.0),  "zero-length block never wraps");
            expect(! loopWrapped(1.0, 2.0, 0.0),  "non-positive patBeats -> never");
        }

        beginTest("loopWrapped: non-divisor lengths (12, 20 beats) work pre-ceiling");
        {
            // 12-beat pattern does not divide the 64-beat transport ceiling; the
            // predicate runs on the RAW advanced position so it still detects the wrap.
            expect(  loopWrapped(11.9, 12.1, 12.0), "12-beat wrap detected");
            expect(! loopWrapped(12.1, 13.0, 12.0), "no spurious wrap after crossing");
            expect(  loopWrapped(59.9, 60.1, 20.0), "20-beat wrap at 60 (3rd loop)");
        }

        beginTest("boundaryReached: playing / stop-edge / stopped");
        {
            // Playing: commit only on a reference-pattern wrap.
            expect(! boundaryReached(true,  true,  0.0, 1.0, 8.0),  "playing, no wrap -> wait");
            expect(  boundaryReached(true,  true,  7.9, 8.1, 8.0),  "playing, wrap -> commit");
            // Stop edge: commit immediately regardless of position.
            expect(  boundaryReached(false, true,  3.0, 3.0, 8.0),  "playing->stopped edge -> commit");
            // Stopped, no edge: never (a stopped stage applies immediately at stage time).
            expect(! boundaryReached(false, false, 0.0, 0.0, 8.0),  "stopped, no edge -> never");
        }

        // The swapper as mu-tant drives it: the boundary rule is swapLoopBeats over the per-voice
        // gate-pattern lengths. Payload = ValueTree, as in the processor.
        using Swapper = LayerSwapper<juce::ValueTree, mu_limits::kMaxLayers>;
        struct Applied { int fullId = -1; std::vector<std::pair<int, int>> slots; };   // slot, id
        auto wire = [](Swapper& sw, Applied& a)
        {
            sw.setAppliers([&a](juce::ValueTree& t) { a.fullId = (int) t.getProperty("id"); },
                           [&a](int v, juce::ValueTree& t) { a.slots.push_back({ v, (int) t.getProperty("id") }); });
        };
        auto flag = [](Swapper& sw, int numActive, bool playing, double start, double beats,
                       bool onMaster, double master, const std::array<double, mu_limits::kMaxLayers>& pat)
        {
            return sw.flagBoundaries(playing, start, beats, [&](int slot) noexcept
                                     { return swapLoopBeats(slot, numActive, onMaster ? SwapTiming::OnMasterLoop : SwapTiming::OnLayerLoop,
                                                            master, pat.data()); });
        };
        std::array<double, mu_limits::kMaxLayers> eight {}; eight.fill(8.0);

        beginTest("Swapper: full-preset swap commits at voice-0 boundary");
        {
            Swapper sw; Applied got; wire(sw, got);
            juce::ValueTree tree("Full"); tree.setProperty("id", 42, nullptr);
            sw.stageFull(std::move(tree));
            expect(sw.hasFullPending(), "full preset staged");
            flag(sw, 2, true, 0.0, 0.5, false, 0.0, eight);   // establish playing
            expect(! flag(sw, 2, true, 0.0, 1.0, false, 0.0, eight), "no boundary mid-loop");
            expect(flag(sw, 2, true, 7.9, 0.2, false, 0.0, eight), "voice-0 wrap flags the full swap");
            expect(sw.commit(), "committed");
            expectEquals(got.fullId, 42);
            expect(! sw.hasFullPending(), "slot cleared after commit");
            expect(! sw.commit(), "nothing left to commit");
        }

        beginTest("Swapper: per-voice swap fires on the voice's OWN length, not voice 0");
        {
            Swapper sw; Applied got; wire(sw, got);
            juce::ValueTree v1("Voice"); v1.setProperty("id", 1, nullptr);
            sw.stageSlot(1, std::move(v1));
            auto pat = eight; pat[1] = 4.0;   // voice 1 has a 4-beat pattern
            expect(flag(sw, 2, true, 3.9, 0.2, false, 0.0, pat), "voice 1 commits at its own 4-beat loop");
            sw.commit();
            expect(got.slots.size() == 1 && got.slots[0].first == 1, "voice 1 only");
        }

        beginTest("Swapper: re-stage supersedes; full supersedes pending voice swaps");
        {
            Swapper sw; Applied got; wire(sw, got);
            juce::ValueTree a("Voice"); a.setProperty("id", 1, nullptr);
            sw.stageSlot(0, std::move(a));
            juce::ValueTree b("Voice"); b.setProperty("id", 2, nullptr);
            sw.stageSlot(0, std::move(b));     // supersedes a
            flag(sw, 1, true, 7.9, 0.2, false, 0.0, eight);
            sw.commit();
            expect(got.slots.size() == 1 && got.slots[0].second == 2, "latest stage wins");

            juce::ValueTree v("Voice"); sw.stageSlot(2, std::move(v));
            expect(sw.hasPending(2), "voice 2 pending");
            juce::ValueTree full("Full"); sw.stageFull(std::move(full));
            expect(! sw.hasPending(2), "full preset supersedes the pending voice swap");
            expect(sw.hasFullPending(), "full now pending");
        }

        beginTest("Swapper: stop-edge commits a staged swap immediately");
        {
            Swapper sw; Applied got; wire(sw, got);
            flag(sw, 1, true, 0.0, 0.5, false, 0.0, eight);
            juce::ValueTree v("Voice"); v.setProperty("id", 7, nullptr);
            sw.stageSlot(0, std::move(v));
            expect(flag(sw, 1, false, 2.0, 0.5, false, 0.0, eight), "stop edge flags the staged voice swap");
            sw.commit();
            expect(got.slots.size() == 1 && got.slots[0].second == 7, "committed on stop");
        }

        beginTest("Swapper: host play with no internal Play stages; a stopped transport applies at once");
        {
            Swapper sw; Applied got; wire(sw, got);
            flag(sw, 1, true, 0.0, 0.5, false, 0.0, eight);    // the resolved transport (host) plays
            juce::ValueTree v("Voice"); v.setProperty("id", 3, nullptr);
            sw.useSlot(0, std::move(v));
            expect(sw.hasPending(0) && got.slots.empty(), "staged until the voice's loop wraps");
            expect(flag(sw, 1, true, 7.9, 0.2, false, 0.0, eight));
            sw.commit();
            expect(got.slots.size() == 1 && got.slots[0].second == 3);

            flag(sw, 1, false, 8.0, 0.5, false, 0.0, eight);   // stopped (edge consumed)
            flag(sw, 1, false, 8.0, 0.5, false, 0.0, eight);
            juce::ValueTree f("Full"); f.setProperty("id", 4, nullptr);
            sw.useFull(std::move(f));
            expect(got.fullId == 4 && ! sw.hasFullPending(), "applied at once, nothing pending");
        }

        // OnBar through the swapper: a staged swap waits for the 4-beat bar line, not the voice's 8-beat
        // loop nor a 16-beat master loop, and a full preset follows the same rule.
        auto flagBar = [](Swapper& sw, double start, double beats, double master,
                          const std::array<double, mu_limits::kMaxLayers>& pat)
        {
            return sw.flagBoundaries(true, start, beats, [&](int slot) noexcept
                                     { return swapLoopBeats(slot, 2, SwapTiming::OnBar, master, pat.data()); });
        };

        for (const double master : { 0.0, 16.0 })   // free-running, then with a master loop
        {
            const juce::String mode = master > 0.0 ? " (master loop set)" : " (free-running)";

            beginTest("Swapper OnBar: a voice swap commits on the bar line, not the voice loop" + mode);
            {
                Swapper sw; Applied got; wire(sw, got);
                flagBar(sw, 0.0, 0.5, master, eight);                       // establish playing
                juce::ValueTree v("Voice"); v.setProperty("id", 5, nullptr);
                sw.stageSlot(1, std::move(v));
                expect(! flagBar(sw, 1.0, 1.0, master, eight), "mid-bar: wait");
                expect(  flagBar(sw, 3.9, 0.2, master, eight), "the bar line (beat 4) flags it, well before the 8-beat loop");
                sw.commit();
                expect(got.slots.size() == 1 && got.slots[0].first == 1 && got.slots[0].second == 5, "voice 1 committed");
            }

            beginTest("Swapper OnBar: a full preset commits on the bar line" + mode);
            {
                Swapper sw; Applied got; wire(sw, got);
                flagBar(sw, 0.0, 0.5, master, eight);
                juce::ValueTree f("Full"); f.setProperty("id", 9, nullptr);
                sw.stageFull(std::move(f));
                expect(! flagBar(sw, 4.1, 3.0, master, eight), "no wrap between beat 4.1 and 7.1");
                expect(  flagBar(sw, 7.9, 0.2, master, eight), "the next bar line (beat 8) flags it");
                sw.commit();
                expectEquals(got.fullId, 9);
                expect(! sw.hasFullPending(), "cleared after commit");
            }
        }

        beginTest("Swapper OnBar: a voice pattern not yet locked (length 0) still commits on the bar");
        {
            Swapper sw; Applied got; wire(sw, got);
            std::array<double, mu_limits::kMaxLayers> unlocked {};
            flagBar(sw, 0.0, 0.5, 0.0, unlocked);
            juce::ValueTree v("Voice"); v.setProperty("id", 6, nullptr);
            sw.stageSlot(0, std::move(v));
            expect(flagBar(sw, 3.9, 0.2, 0.0, unlocked), "bar line flags it without a locked length");
            sw.commit();
            expect(got.slots.size() == 1 && got.slots[0].second == 6);
        }

        beginTest("swapLoopBeats: the mu-tant boundary rule");
        {
            std::array<double, mu_limits::kMaxLayers> pat {}; pat.fill(8.0); pat[0] = 12.0; pat[1] = 4.0;
            const auto master = SwapTiming::OnMasterLoop, own = SwapTiming::OnLayerLoop, bar = SwapTiming::OnBar;
            expectEquals(swapLoopBeats(kFullPreset, 2, own, 0.0, pat.data()), 12.0, "full: voice 0 with no master loop");
            expectEquals(swapLoopBeats(kFullPreset, 2, own, 16.0, pat.data()), 16.0, "full: the master loop when one is set");
            expectEquals(swapLoopBeats(1, 2, own, 16.0, pat.data()), 4.0, "voice: its own length");
            expectEquals(swapLoopBeats(1, 2, master, 16.0, pat.data()), 16.0, "voice: the master loop when OnMasterLoop");
            expectEquals(swapLoopBeats(1, 2, master, 0.0, pat.data()), 4.0, "voice: OnMasterLoop with no master loop uses its own");
            expect(swapLoopBeats(2, 2, own, 0.0, pat.data()) < 0.0, "an inactive voice never commits");
            expect(swapLoopBeats(2, 2, bar, 0.0, pat.data()) < 0.0, "an inactive voice never commits on a bar either");
            std::array<double, mu_limits::kMaxLayers> unlocked {};
            expectEquals(swapLoopBeats(0, 1, own, 0.0, unlocked.data()), 0.0, "length 0 before the first locked block: stop edge only");
            expectEquals(swapLoopBeats(1, 2, bar, 16.0, pat.data()), kBarBeats, "OnBar: a voice swap lands on the bar line");
            expectEquals(swapLoopBeats(kFullPreset, 2, bar, 16.0, pat.data()), kBarBeats, "OnBar: a full preset lands on the bar line");
            expectEquals(swapLoopBeats(0, 1, bar, 0.0, unlocked.data()), kBarBeats, "OnBar needs no locked pattern length");
        }
    }
};

static HotSwapBoundaryTest hotSwapBoundaryTestInstance;
