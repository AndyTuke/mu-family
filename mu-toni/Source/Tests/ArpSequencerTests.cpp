// mu-toni arpeggiator engine tests — the deterministic note core.
// Covers pool derivation (chord · inversion · octaves), the skewed-triangle
// Direction scan at its anchor points, step→MIDI mapping, and determinism.

#include <juce_core/juce_core.h>
#include "Sequencer/Arpeggiator.h"

using namespace mu_toni;

namespace
{
bool poolEquals(const ArpParams& p, std::initializer_list<int> expected)
{
    int pool[kMaxPool];
    const int n = buildPool(p, pool, kMaxPool);
    if (n != (int) expected.size()) return false;
    int i = 0;
    for (int e : expected) if (pool[i++] != e) return false;
    return true;
}

bool scanEquals(int poolLen, float dir, std::initializer_list<int> expected)
{
    int scan[kMaxScan];
    const int n = buildScan(poolLen, dir, scan, kMaxScan);
    if (n != (int) expected.size()) return false;
    int i = 0;
    for (int e : expected) if (scan[i++] != e) return false;
    return true;
}
} // namespace

class ArpeggiatorTest : public juce::UnitTest
{
public:
    ArpeggiatorTest() : juce::UnitTest("Arpeggiator engine", "mu-toni") {}

    void runTest() override
    {
        beginTest("Pool — chord · octaves · inversion");
        {
            ArpParams p;                       // default: Major (2), span 2, inv 0
            p.octavesSpan = 1;
            expect(poolEquals(p, {0, 4, 7}), "Major triad, 1 octave");

            p.octavesSpan = 2;
            expect(poolEquals(p, {0, 4, 7, 12, 16, 19}), "Major triad, 2 octaves");

            p.octavesSpan = 1;
            p.inversion = 1;
            expect(poolEquals(p, {4, 7, 12}), "Major 1st inversion (root up)");

            p.inversion = -1;
            expect(poolEquals(p, {-5, 0, 4}), "Major drop voicing (top down)");

            p.inversion = 0;
            p.chord = 1;                       // Power (5th)
            expect(poolEquals(p, {0, 7}), "Power chord pool");

            p.chord = 3;                       // Minor
            expect(poolEquals(p, {0, 3, 7}), "Minor triad pool");
        }

        beginTest("Direction — skewed-triangle scan anchors (6-note pool)");
        {
            expect(scanEquals(6, 100.0f, {0, 1, 2, 3, 4, 5}), "+100 = up-arp");
            expect(scanEquals(6, -100.0f, {5, 4, 3, 2, 1, 0}), "-100 = down-arp");
            expect(scanEquals(6, 0.0f, {0, 1, 2, 3, 4, 5, 4, 3, 2, 1}),
                   "0 = symmetric up-down");
        }

        beginTest("Direction — degenerate pools");
        {
            expect(scanEquals(1, 0.0f, {0}), "single-note pool");
            expect(scanEquals(2, 0.0f, {0, 1}), "two-note pool, no interior");
            expect(scanEquals(2, 100.0f, {0, 1}), "two-note up");
        }

        beginTest("stepMidi — Major, C, octave 4, up");
        {
            ArpParams p;
            p.rootNote = 0; p.rootOctave = 4; p.chord = 2;
            p.octavesSpan = 1; p.direction = 100.0f;   // pool {0,4,7}, cycle 3
            expect(stepMidi(p, 0) == 60, "step 0 = C4 (60)");
            expect(stepMidi(p, 1) == 64, "step 1 = E4 (64)");
            expect(stepMidi(p, 2) == 67, "step 2 = G4 (67)");
            expect(stepMidi(p, 3) == 60, "step 3 wraps to C4");
        }

        beginTest("stepMidi — root transpose");
        {
            ArpParams p;
            p.rootNote = 7; p.rootOctave = 4;          // G
            p.chord = 2; p.octavesSpan = 1; p.direction = 100.0f;
            expect(stepMidi(p, 0) == 67, "G root, step 0 = G4 (67)");
        }

        beginTest("Determinism — same params, same sequence every loop");
        {
            ArpParams p;
            p.rootNote = 2; p.rootOctave = 3; p.chord = 12;  // m7
            p.octavesSpan = 3; p.inversion = 1; p.direction = 37.0f;
            for (int i = 0; i < 40; ++i)
                expect(stepMidi(p, i) == stepMidi(p, i),
                       "stepMidi is a pure function");
            // Cycle repeats identically.
            int scan[kMaxScan];
            int pool[kMaxPool];
            const int N = buildPool(p, pool, kMaxPool);
            const int cyc = buildScan(N, p.direction, scan, kMaxScan);
            expect(cyc > 0, "non-empty cycle");
            for (int i = 0; i < cyc; ++i)
                expect(stepMidi(p, i) == stepMidi(p, i + cyc),
                       "note at step i equals step i+cycle");
        }
    }
};

static ArpeggiatorTest arpeggiatorTest;
