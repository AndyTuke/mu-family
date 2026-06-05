// mu-tant hot-swap staging tests — the pure loop-boundary predicates
// (HotSwapBoundary.h) and the VoiceHotSwapStager store-release/load-acquire
// handshake. No PluginProcessor needed: this is the regression guard for the
// swap-defer decision (wrap detection incl. non-divisor pattern lengths,
// apply-on-stop, supersede, and per-voice vs full-preset boundaries).

#include <juce_data_structures/juce_data_structures.h>
#include <array>
#include "Plugin/HotSwapBoundary.h"
#include "Plugin/VoiceHotSwapStager.h"

class HotSwapBoundaryTest : public juce::UnitTest
{
public:
    HotSwapBoundaryTest() : juce::UnitTest("mu-tant hot-swap boundary", "mu-tant") {}

    void runTest() override
    {
        using namespace mu_tant;
        using namespace mu_tant::hotswap;

        beginTest("patternWrapped: loop-index crossing");
        {
            // 8-beat pattern. No wrap within a loop, wrap when the index advances.
            expect(! patternWrapped(0.0, 1.0, 8.0),  "no wrap mid-loop");
            expect(! patternWrapped(7.0, 7.9, 8.0),  "no wrap approaching the boundary");
            expect(  patternWrapped(7.9, 8.1, 8.0),  "wrap crossing 8.0");
            expect(  patternWrapped(15.9, 16.1, 8.0), "wrap crossing the 2nd loop point");
            expect(! patternWrapped(8.0, 8.0, 8.0),  "zero-length block never wraps");
            expect(! patternWrapped(1.0, 2.0, 0.0),  "non-positive patBeats → never");
        }

        beginTest("patternWrapped: non-divisor lengths (12, 20 beats) work pre-ceiling");
        {
            // 12-beat pattern does not divide the 64-beat transport ceiling; the
            // predicate runs on the RAW advanced position so it still detects the wrap.
            expect(  patternWrapped(11.9, 12.1, 12.0), "12-beat wrap detected");
            expect(! patternWrapped(12.1, 13.0, 12.0), "no spurious wrap after crossing");
            expect(  patternWrapped(59.9, 60.1, 20.0), "20-beat wrap at 60 (3rd loop)");
        }

        beginTest("swapBoundaryReached: playing / stop-edge / stopped");
        {
            // Playing: commit only on a reference-pattern wrap.
            expect(! swapBoundaryReached(true,  true,  0.0, 1.0, 8.0),  "playing, no wrap → wait");
            expect(  swapBoundaryReached(true,  true,  7.9, 8.1, 8.0),  "playing, wrap → commit");
            // Stop edge: commit immediately regardless of position.
            expect(  swapBoundaryReached(false, true,  3.0, 3.0, 8.0),  "playing→stopped edge → commit");
            // Stopped, no edge: never (a stopped stage applies immediately at stage time).
            expect(! swapBoundaryReached(false, false, 0.0, 0.0, 8.0),  "stopped, no edge → never");
        }

        beginTest("Stager: full-preset swap commits at voice-0 boundary");
        {
            VoiceHotSwapStager st;
            juce::ValueTree tree("Full"); tree.setProperty("id", 42, nullptr);
            st.stageFull(std::move(tree));
            expect(st.hasFullPending(), "full preset staged");

            std::array<double, VoiceHotSwapStager::kMaxVoices> patBeats {};
            patBeats.fill(8.0);
            const double fullPat = 8.0; // voice 0's pattern

            // Mid-loop: not yet.
            expect(! st.checkBoundaries(2, true, true, 0.0, 1.0, patBeats, fullPat),
                   "no boundary mid-loop");
            // Cross voice-0's wrap → flagged.
            expect(st.checkBoundaries(2, true, true, 7.9, 8.1, patBeats, fullPat),
                   "voice-0 wrap flags the full swap");

            juce::ValueTree out;
            expect(st.takeFull(out), "full swap is takeable after boundary");
            expect((int) out.getProperty("id") == 42, "the staged tree is what comes out");
            expect(! st.hasFullPending(), "slot cleared after take");
            expect(! st.takeFull(out),    "nothing left to take");
        }

        beginTest("Stager: per-voice swap fires on the voice's OWN length, not voice 0");
        {
            VoiceHotSwapStager st;
            juce::ValueTree v1("Voice"); v1.setProperty("idx", 1, nullptr);
            st.stageVoice(1, std::move(v1));

            // Voice 0 has an 8-beat pattern; voice 1 has a 4-beat pattern.
            std::array<double, VoiceHotSwapStager::kMaxVoices> patBeats {};
            patBeats.fill(8.0);
            patBeats[1] = 4.0;

            // Crossing 4.0 wraps voice 1 (its own length) but not voice 0.
            expect(st.checkBoundaries(2, true, true, 3.9, 4.1, patBeats, /*fullPat*/8.0),
                   "voice 1 commits at its own 4-beat loop");
            juce::ValueTree out;
            expect(st.takeVoice(1, out), "voice 1 swap takeable");
            expect((int) out.getProperty("idx") == 1, "correct voice tree");
            expect(! st.takeVoice(0, out), "voice 0 untouched");
        }

        beginTest("Stager: re-stage supersedes; full supersedes pending voice swaps");
        {
            VoiceHotSwapStager st;
            juce::ValueTree a("Voice"); a.setProperty("gen", 1, nullptr);
            st.stageVoice(0, std::move(a));
            juce::ValueTree b("Voice"); b.setProperty("gen", 2, nullptr);
            st.stageVoice(0, std::move(b));     // supersedes a

            std::array<double, VoiceHotSwapStager::kMaxVoices> patBeats {}; patBeats.fill(8.0);
            st.checkBoundaries(1, true, true, 7.9, 8.1, patBeats, 8.0);
            juce::ValueTree out;
            expect(st.takeVoice(0, out), "voice swap takeable");
            expect((int) out.getProperty("gen") == 2, "latest stage wins");

            // A full preset cancels a pending voice swap.
            juce::ValueTree v("Voice"); st.stageVoice(2, std::move(v));
            expect(st.hasVoicePending(2), "voice 2 pending");
            juce::ValueTree full("Full"); st.stageFull(std::move(full));
            expect(! st.hasVoicePending(2), "full preset supersedes the pending voice swap");
            expect(st.hasFullPending(), "full now pending");
        }

        beginTest("Stager: stop-edge commits a staged swap immediately");
        {
            VoiceHotSwapStager st;
            juce::ValueTree v("Voice"); st.stageVoice(0, std::move(v));
            std::array<double, VoiceHotSwapStager::kMaxVoices> patBeats {}; patBeats.fill(8.0);
            // playing→stopped with no wrap: still flagged via the stop edge.
            expect(st.checkBoundaries(1, /*playing*/false, /*wasPlaying*/true,
                                      2.0, 2.0, patBeats, 8.0),
                   "stop edge commits the staged voice swap");
            juce::ValueTree out;
            expect(st.takeVoice(0, out), "committed on stop");
        }
    }
};

static HotSwapBoundaryTest hotSwapBoundaryTestInstance;
