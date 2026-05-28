// mu-tant audio gate-stage harness — renders a loud synthetic "drone" buffer
// through the same applyGateBlock() the audio engine uses, and asserts the gate
// silences the output on load (transport stopped, no envelopes, not bypassed).
// This is the regression guard for "no drone audible when the app loads".

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
        const double bps  = (120.0 / 60.0) / 48000.0;   // beats per sample @ 120 BPM, 48k

        // ── THE load case: stopped, no envelopes, gater active → SILENCE ────────
        beginTest("On load (transport stopped, no envelopes, not bypassed) → silent");
        {
            GatePattern p;                       // empty pattern — the default state
            std::vector<float> L(N, 1.0f), R(N, 1.0f);   // full-scale drone
            applyGateBlock(p, L.data(), R.data(), N, /*gap*/0.0f,
                           /*bypassed*/false, /*playing*/false, /*beat*/0.0, bps);
            expectWithinAbsoluteError(peak(L, R), 0.0f, 1.0e-7f,
                                      "no audio is audible when the app loads");
        }

        beginTest("gateModeFor: the block-level decisions");
        {
            expect(gateModeFor(false, false, true)  == GateMode::Silence,  "stopped + empty → silence");
            expect(gateModeFor(false, false, false) == GateMode::Silence,  "stopped → silence even with envelopes");
            expect(gateModeFor(false, true,  true)  == GateMode::Silence,  "playing + empty → silence");
            expect(gateModeFor(false, true,  false) == GateMode::Envelope, "playing + envelopes → envelope");
            expect(gateModeFor(true,  false, true)  == GateMode::Pass,     "bypass → pass (audition)");
            expect(gateModeFor(true,  true,  false) == GateMode::Pass,     "bypass overrides transport + pattern");
        }

        beginTest("Gater bypass passes the raw drone through unchanged");
        {
            GatePattern p;
            std::vector<float> L(N, 0.7f), R(N, -0.7f);
            applyGateBlock(p, L.data(), R.data(), N, 0.0f, /*bypassed*/true, /*playing*/false, 0.0, bps);
            expectWithinAbsoluteError(peak(L, R), 0.7f, 1.0e-7f, "bypass leaves the drone audible");
        }

        beginTest("Stopped is silent even after envelopes are drawn");
        {
            GatePattern p;
            p.subdivision = GatePattern::Subdivision::Sixteenth;
            GateEnvelope e; e.startCell = 0; e.lengthCells = 1;
            p.addEnvelope(e);
            std::vector<float> L(N, 1.0f), R(N, 1.0f);
            applyGateBlock(p, L.data(), R.data(), N, 0.0f, false, /*playing*/false, 0.0, bps);
            expectWithinAbsoluteError(peak(L, R), 0.0f, 1.0e-7f, "stop closes the gate");
        }

        beginTest("Playing opens the gate where an envelope sits, silent elsewhere");
        {
            GatePattern p;
            p.subdivision = GatePattern::Subdivision::Sixteenth;   // 32 cells over 8 beats
            GateEnvelope e; e.startCell = 0; e.lengthCells = 1;    // decay in cell 0 only
            p.addEnvelope(e);

            // Render at the very start of cell 0 — the envelope opens (≈ full then decays).
            std::vector<float> L(N, 1.0f), R(N, 1.0f);
            applyGateBlock(p, L.data(), R.data(), N, 0.0f, false, /*playing*/true, 0.0, bps);
            expect(std::abs(L[0]) > 0.5f, "gate is open at the envelope's start");

            // Render inside an empty cell (cell 1) with a frozen position → silent.
            const double beatInEmptyCell = (8.0 / 32.0) * 1.5;
            std::vector<float> L2(N, 1.0f), R2(N, 1.0f);
            applyGateBlock(p, L2.data(), R2.data(), N, 0.0f, false, /*playing*/true,
                           beatInEmptyCell, /*beatsPerSample*/0.0);
            expectWithinAbsoluteError(peak(L2, R2), 0.0f, 1.0e-6f, "empty cell → silent while playing");
        }
    }
};

static GateStageTest gateStageTestInstance;
