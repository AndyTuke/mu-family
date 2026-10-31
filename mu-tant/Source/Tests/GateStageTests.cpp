// mu-tant audio gate-stage harness - renders a loud synthetic "drone" buffer
// through the same applyGateBlock() the audio engine uses, and asserts the gate
// silences the output on load (transport stopped, no envelopes, not bypassed).
// This is the regression guard for "no drone audible when the app loads", plus
// the rising-edge de-click (a 0-attack envelope ramps over ~kMinAttackMs).

#include <juce_core/juce_core.h>
#include <vector>
#include <cmath>
#include <algorithm>
#include "Sequencer/GatePattern.h"

class GateStageTest : public juce::UnitTest
{
public:
    GateStageTest() : juce::UnitTest("mu-tant gate stage (audio)", "mu-tant") {}

    static float peak(const std::vector<float>& l, const std::vector<float>& r)
    {
        float p = 0.0f;
        for (size_t i = 0; i < l.size(); ++i)
        {
            p = std::max(p, std::abs(l[i]));
            p = std::max(p, std::abs(r[i]));
        }
        return p;
    }

    void runTest() override
    {
        using namespace mu_tant;

        const int    N   = 128;
        const double SR  = 48000.0;
        const double bps = (120.0 / 60.0) / SR;   // beats per sample @ 120 BPM, 48k

        // ── THE load case: stopped, no envelopes, gater active -> SILENCE ────────
        beginTest("On load (transport stopped, no envelopes, not bypassed) -> silent");
        {
            GatePattern p;                       // empty pattern - the default state
            std::vector<float> L(N, 1.0f), R(N, 1.0f);   // full-scale drone
            applyGateBlock(p, L.data(), R.data(), N, /*gap*/0.0f,
                           /*bypassed*/false, /*playing*/false, /*beat*/0.0, bps, SR);
            expectWithinAbsoluteError(peak(L, R), 0.0f, 1.0e-7f,
                                      "no audio is audible when the app loads");
        }

        beginTest("gateModeFor: the block-level decisions");
        {
            expect(gateModeFor(false, false, true)  == GateMode::Silence,  "stopped + empty -> silence");
            expect(gateModeFor(false, false, false) == GateMode::Silence,  "stopped -> silence even with envelopes");
            expect(gateModeFor(false, true,  true)  == GateMode::Silence,  "playing + empty -> silence");
            expect(gateModeFor(false, true,  false) == GateMode::Envelope, "playing + envelopes -> envelope");
            expect(gateModeFor(true,  false, true)  == GateMode::Pass,     "bypass -> pass (audition)");
            expect(gateModeFor(true,  true,  false) == GateMode::Pass,     "bypass overrides transport + pattern");
        }

        beginTest("Gater bypass passes the raw drone through unchanged");
        {
            GatePattern p;
            std::vector<float> L(N, 0.7f), R(N, -0.7f);
            applyGateBlock(p, L.data(), R.data(), N, 0.0f, /*bypassed*/true, /*playing*/false, 0.0, bps, SR);
            expectWithinAbsoluteError(peak(L, R), 0.7f, 1.0e-7f, "bypass leaves the drone audible");
        }

        beginTest("Stopped is silent even after envelopes are drawn");
        {
            GatePattern p;
            p.subdivision = GatePattern::Subdivision::Sixteenth;
            GateEnvelope e; e.startCell = 0; e.lengthCells = 1;
            p.addEnvelope(e);
            std::vector<float> L(N, 1.0f), R(N, 1.0f);
            applyGateBlock(p, L.data(), R.data(), N, 0.0f, false, /*playing*/false, 0.0, bps, SR);
            expectWithinAbsoluteError(peak(L, R), 0.0f, 1.0e-7f, "stop closes the gate");
        }

        beginTest("Playing opens the gate where an envelope sits, silent elsewhere");
        {
            GatePattern p;
            p.subdivision = GatePattern::Subdivision::Sixteenth;   // 32 cells over 8 beats
            GateEnvelope e; e.startCell = 0; e.lengthCells = 1;    // decay in cell 0 only
            p.addEnvelope(e);

            // Render at the very start of cell 0 - the (0-attack) envelope opens, now
            // via the de-click ramp: near-silent at sample 0, open by the block's end.
            // Block must span the kMinAttackMs ramp (≈480 samples @48k) so it fully opens.
            const int M = (int) (GatePattern::kMinAttackMs * 0.001 * SR) + 64;
            std::vector<float> L(M, 1.0f), R(M, 1.0f);
            applyGateBlock(p, L.data(), R.data(), M, 0.0f, false, /*playing*/true, 0.0, bps, SR);
            expect(std::abs(L[0]) < 0.1f,      "0-attack opens via a short ramp, not an instant click");
            expect(std::abs(L[M - 1]) > 0.5f,  "gate has opened by the end of the block");

            // Render inside an empty cell (cell 1) with a frozen position -> silent.
            const double beatInEmptyCell = (8.0 / 32.0) * 1.5;
            std::vector<float> L2(N, 1.0f), R2(N, 1.0f);
            applyGateBlock(p, L2.data(), R2.data(), N, 0.0f, false, /*playing*/true,
                           beatInEmptyCell, /*beatsPerSample*/0.0, SR);
            // The gate was open when the block began, so it closes at the release rate (kMinReleaseMs), not in one sample:
            // silent from then on.
            const int releaseSamples = (int) std::ceil((double) GatePattern::kMinReleaseMs * 0.001 * SR) + 1;
            expectWithinAbsoluteError(peak(std::vector<float>(L2.begin() + releaseSamples, L2.end()),
                                           std::vector<float>(R2.begin() + releaseSamples, R2.end())),
                                      0.0f, 1.0e-6f, "empty cell -> silent while playing (after the release)");
        }

        beginTest("A block that cannot take the edit lock holds the previous gate level");
        {
            GatePattern p;
            p.subdivision = GatePattern::Subdivision::Sixteenth;
            GateEnvelope e; e.startCell = 0; e.lengthCells = 4; e.split = 0.0f;
            p.addEnvelope(e);

            // Closed: a stopped block leaves the gate shut, then an edit holds the lock for the next block.
            std::vector<float> L(N, 1.0f), R(N, 1.0f);
            applyGateBlock(p, L.data(), R.data(), N, 0.0f, false, /*playing*/false, 0.0, bps, SR);
            p.editLock.store(true);
            std::fill(L.begin(), L.end(), 1.0f);
            std::fill(R.begin(), R.end(), 1.0f);
            applyGateBlock(p, L.data(), R.data(), N, 0.0f, false, /*playing*/true, 0.0, bps, SR);
            expectWithinAbsoluteError(peak(L, R), 0.0f, 1.0e-7f, "a closed gate stays closed through a lock miss");
            expect(p.editLock.load(), "the audio thread must not release a lock it did not take");
            p.editLock.store(false);

            // Open: render until the ramp is fully up, then miss the lock again.
            const int M = 1024;
            std::vector<float> L2(M, 1.0f), R2(M, 1.0f);
            applyGateBlock(p, L2.data(), R2.data(), M, 0.0f, false, /*playing*/true, 0.0, bps, SR);
            const float open = p.gateLevel;
            expect(open > 0.9f, "the envelope is open after the ramp");
            p.editLock.store(true);
            std::fill(L.begin(), L.end(), 1.0f);
            std::fill(R.begin(), R.end(), 1.0f);
            applyGateBlock(p, L.data(), R.data(), N, 0.0f, false, /*playing*/true, 0.0, bps, SR);
            expectWithinAbsoluteError(L[0], open, 1.0e-6f, "an open gate keeps its level through a lock miss");
            expectWithinAbsoluteError(L[N - 1], open, 1.0e-6f, "and holds it for the whole block");
            p.editLock.store(false);
        }

        beginTest("0-attack envelope ramps over ~kMinAttackMs instead of clicking");
        {
            GatePattern p;
            p.subdivision = GatePattern::Subdivision::Sixteenth;
            GateEnvelope e; e.startCell = 0; e.lengthCells = 4; e.split = 0.0f;  // instant attack
            p.addEnvelope(e);

            const int M = 512;
            std::vector<float> L(M, 1.0f), R(M, 1.0f);
            applyGateBlock(p, L.data(), R.data(), M, 0.0f, false, /*playing*/true, 0.0, bps, SR);

            const int rampSamples = (int) std::round((double) GatePattern::kMinAttackMs * 0.001 * SR); // ≈480 @48k

            expect(std::abs(L[0]) < 0.1f, "starts near silent - no instant 0->1 jump");
            expect(std::abs(L[rampSamples / 2]) > std::abs(L[0]), "rises during the ramp");
            expect(std::abs(L[std::min(M - 1, rampSamples)]) > 0.9f, "fully open by ~kMinAttackMs");
            // Sanity: the slope never exceeds the cap (no faster-than-ramp jump).
            float maxStep = 0.0f;
            for (int i = 1; i < rampSamples && i < M; ++i)
                maxStep = std::max(maxStep, std::abs(L[i]) - std::abs(L[i - 1]));
            const float cap = (float) (1.0 / ((double) GatePattern::kMinAttackMs * 0.001 * SR));
            expect(maxStep <= cap + 1.0e-4f, "no rising step exceeds the slew cap");
        }

        const float fallCap = (float) (1.0 / ((double) GatePattern::kMinReleaseMs * 0.001 * SR));

        beginTest("a region ending at a non-zero level falls over ~kMinReleaseMs, not in one sample");
        {
            GatePattern p;
            p.subdivision = GatePattern::Subdivision::Sixteenth;
            GateEnvelope e; e.startCell = 0; e.lengthCells = 2; e.split = 0.0f;
            p.addEnvelope(e);

            const int M = 24000;   // two 6000-sample cells open, then closed
            std::vector<float> L(M, 1.0f), R(M, 1.0f);
            applyGateBlock(p, L.data(), R.data(), M, 0.0f, false, /*playing*/true, 0.0, bps, SR);

            float maxDrop = 0.0f, lowest = 1.0f;
            for (int i = 1; i < M; ++i)
            {
                maxDrop = std::max(maxDrop, L[i - 1] - L[i]);
                lowest  = std::min(lowest, L[i]);
            }
            expect(lowest < 0.01f, "the gate does close after the region");
            expect(maxDrop > 0.0f, "and it does fall");
            expect(maxDrop <= fallCap + 1.0e-4f, "no falling step exceeds the release cap: " + juce::String(maxDrop));
        }

        beginTest("stopping fades the open gate to silence at the release rate instead of cutting it");
        {
            GatePattern p;
            p.subdivision = GatePattern::Subdivision::Sixteenth;
            GateEnvelope e; e.startCell = 0; e.lengthCells = 8; e.split = 0.0f;
            p.addEnvelope(e);

            const int M = 1500;   // past the attack ramp, well inside the first cell's decay
            std::vector<float> L(M, 1.0f), R(M, 1.0f);
            applyGateBlock(p, L.data(), R.data(), M, 0.0f, false, /*playing*/true, 0.0, bps, SR);
            const float open = L[M - 1];
            expect(open > 0.5f, "precondition: the gate is open: " + juce::String(open));

            std::vector<float> L2(N, 1.0f), R2(N, 1.0f);
            applyGateBlock(p, L2.data(), R2.data(), N, 0.0f, false, /*playing*/false, 0.0, bps, SR);
            expect(L2[0] > open - fallCap - 1.0e-4f, "the first sample after the stop continues from the open level (no cut)");
            expect(L2[N - 1] == 0.0f, "and the gate is fully closed by the end of the block");
            float maxDrop = 0.0f;
            for (int i = 1; i < N; ++i) maxDrop = std::max(maxDrop, L2[i - 1] - L2[i]);
            expect(maxDrop <= fallCap + 1.0e-4f, "no step exceeds the release cap");

            std::vector<float> L3(N, 1.0f), R3(N, 1.0f);
            applyGateBlock(p, L3.data(), R3.data(), N, 0.0f, false, /*playing*/false, 0.0, bps, SR);
            expect(peak(L3, R3) == 0.0f, "and it stays closed");
        }

        beginTest("Pass to Silence fades from fully open, not from a stale level");
        {
            GatePattern p;
            std::vector<float> L(N, 1.0f), R(N, 1.0f);
            applyGateBlock(p, L.data(), R.data(), N, 0.0f, /*bypassed*/true, false, 0.0, bps, SR);
            expect(peak(L, R) == 1.0f, "bypass passes the drone");

            std::vector<float> L2(N, 1.0f), R2(N, 1.0f);
            applyGateBlock(p, L2.data(), R2.data(), N, 0.0f, /*bypassed*/false, /*playing*/false, 0.0, bps, SR);
            expect(L2[0] > 0.9f, "bypass-off starts at the open level, so there is no click");
            expect(L2[N - 1] == 0.0f, "and the gate has closed by the end of the block");
        }
    }
};

static GateStageTest gateStageTestInstance;
