// HitGenerator::getPattern() and getStepTypes() — euclidean pattern generation tests.
//
// These tests verify the core correctness of the euclidean step sequencer. A
// regression here would silently produce wrong rhythms for all users.

#include <juce_core/juce_core.h>
#include "../Sequencer/HitGenerator.h"

class HitGeneratorTest : public juce::UnitTest
{
public:
    HitGeneratorTest() : juce::UnitTest ("HitGenerator pattern generation", "Sequencer") {}

    void runTest() override
    {
        // ── Basic euclidean distribution ─────────────────────────────────────
        beginTest ("E(0,8) — zero hits → all false");
        {
            HitGenerator h;
            h.steps = 8; h.hits = 0;
            auto pat = h.getPattern();
            expect (pat.size() == 8);
            for (bool b : pat) expect (!b);
        }

        beginTest ("E(8,8) — all hits → all true");
        {
            HitGenerator h;
            h.steps = 8; h.hits = 8;
            auto pat = h.getPattern();
            expect (pat.size() == 8);
            for (bool b : pat) expect (b);
        }

        beginTest ("E(4,8) — evenly spaced, 4 hits out of 8 steps");
        {
            HitGenerator h;
            h.steps = 8; h.hits = 4;
            auto pat = h.getPattern();
            expect (pat.size() == 8);
            int count = 0; for (bool b : pat) if (b) ++count;
            expectEquals (count, 4);
            // Evenly spaced: hits at positions 0, 2, 4, 6
            expect (pat[0]); expect (!pat[1]); expect (pat[2]); expect (!pat[3]);
            expect (pat[4]); expect (!pat[5]); expect (pat[6]); expect (!pat[7]);
        }

        beginTest ("E(3,8) — classic Euclidean: 3 hits out of 8");
        {
            HitGenerator h;
            h.steps = 8; h.hits = 3;
            auto pat = h.getPattern();
            expect (pat.size() == 8);
            int count = 0; for (bool b : pat) if (b) ++count;
            expectEquals (count, 3);
        }

        // ── Rotation ─────────────────────────────────────────────────────────
        beginTest ("Rotation shifts first hit to front");
        {
            HitGenerator base;
            base.steps = 8; base.hits = 2; base.rotate = 0;
            auto basePat = base.getPattern();

            // Find first hit position in unrotated pattern
            int firstHit = -1;
            for (int i = 0; i < 8; ++i) { if (basePat[i]) { firstHit = i; break; } }
            expect (firstHit >= 0);

            HitGenerator rotated;
            rotated.steps = 8; rotated.hits = 2; rotated.rotate = firstHit;
            auto rotPat = rotated.getPattern();
            expect (rotPat[0]);  // first step should now be a hit
        }

        // ── Mute ─────────────────────────────────────────────────────────────
        beginTest ("Mute → all false regardless of hits");
        {
            HitGenerator h;
            h.steps = 8; h.hits = 4; h.mute = true;
            auto pat = h.getPattern();
            for (bool b : pat) expect (!b);
        }

        // ── Hit count clamping ────────────────────────────────────────────────
        beginTest ("hits > steps → clamps gracefully, no crash");
        {
            HitGenerator h;
            h.steps = 4; h.hits = 8;
            auto pat = h.getPattern();
            expect ((int)pat.size() == 4);
        }

        // ── Override variant (audio-thread path) ─────────────────────────────
        beginTest ("getPattern(overrides) produces same result as member fields");
        {
            HitGenerator h;
            h.steps = 8; h.hits = 3; h.rotate = 1;
            auto memberPat = h.getPattern();

            EuclidGenOverrides ov;
            ov.hits = 3; ov.rotate = 1;
            std::vector<bool> out, scratch;
            out.reserve(8); scratch.reserve(8);
            h.hits = 0; h.rotate = 0;   // override should win
            h.getPattern(ov, out, scratch);

            expectEquals ((int)out.size(), (int)memberPat.size());
            for (int i = 0; i < (int)out.size(); ++i)
                expect (out[i] == memberPat[i], "override mismatch at step " + juce::String(i));
        }

        // ── StepTypes ────────────────────────────────────────────────────────
        beginTest ("getStepTypes() hit count matches getPattern() hit count");
        {
            HitGenerator h;
            h.steps = 8; h.hits = 3;
            auto pat   = h.getPattern();
            auto types = h.getStepTypes();
            expectEquals ((int)types.size(), (int)pat.size());
            for (int i = 0; i < (int)pat.size(); ++i)
            {
                bool patHit  = pat[i];
                bool typeHit = (types[i] == StepType::Hit);
                expect (patHit == typeHit, "step " + juce::String(i) + " mismatch between getPattern and getStepTypes");
            }
        }
    }
};

static HitGeneratorTest hitGeneratorTest;
