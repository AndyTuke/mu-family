// HitGenerator::getPattern() and getStepTypes() - euclidean pattern generation tests.
//
// These tests verify the core correctness of the euclidean step sequencer. A
// regression here would silently produce wrong rhythms for all users.

#include <juce_core/juce_core.h>
#include "Sequencer/HitGenerator.h"

class HitGeneratorTest : public juce::UnitTest
{
public:
    HitGeneratorTest() : juce::UnitTest ("HitGenerator pattern generation", "Sequencer") {}

    void runTest() override
    {
        // ── Basic euclidean distribution ─────────────────────────────────────
        beginTest ("E(0,8) - zero hits -> all false");
        {
            HitGenerator h;
            h.steps = 8; h.hits = 0;
            auto pat = h.getPattern();
            expect (pat.size() == 8);
            for (bool b : pat) expect (!b);
        }

        beginTest ("E(8,8) - all hits -> all true");
        {
            HitGenerator h;
            h.steps = 8; h.hits = 8;
            auto pat = h.getPattern();
            expect (pat.size() == 8);
            for (bool b : pat) expect (b);
        }

        beginTest ("E(4,8) - evenly spaced, 4 hits out of 8 steps");
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

        beginTest ("E(3,8) - classic Euclidean: 3 hits out of 8");
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
        beginTest ("Mute -> all false regardless of hits");
        {
            HitGenerator h;
            h.steps = 8; h.hits = 4; h.mute = true;
            auto pat = h.getPattern();
            for (bool b : pat) expect (!b);
        }

        // ── Hit count clamping ────────────────────────────────────────────────
        beginTest ("hits > steps -> clamps gracefully, no crash");
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

        // ── Padding layout limits ────────────────────────────────────────────
        beginTest ("Pads share a Steps - 1 budget, in priority order pre -> post -> insert");
        {
            HitGenerator h;
            h.steps = 8;   // budget 7
            const auto lay = h.clampLayout({ 3, 0, 5, 4, 0, 6 });
            expectEquals (lay.prePad, 5);
            expectEquals (lay.postPad, 2);
            expectEquals (lay.insertLength, 0);
            expectEquals (lay.prePad + lay.postPad + lay.insertLength, 7);
        }

        beginTest ("Over-budget pads still leave at least one Euclid step and keep the pattern length");
        {
            HitGenerator h;
            h.steps = 4; h.hits = 4;
            h.prePad = 12; h.postPad = 12; h.insertLength = 8;
            const auto pat = h.getPattern();
            expectEquals ((int) pat.size(), 4);
            int hitCount = 0;
            for (bool b : pat) hitCount += b ? 1 : 0;
            expectEquals (hitCount, 1);   // pre-pad takes 3 of 4 steps; one Euclid step remains
        }

        beginTest ("Pad knob maxima: each knob reaches what the budget leaves after the other two");
        {
            HitGenerator h;
            h.steps = 16; h.prePad = 4; h.postPad = 3; h.insertLength = 2;   // budget 15
            auto m = h.padKnobMaxima();
            expectEquals (m.prePad, 10);        // 15 - 3 - 2
            expectEquals (m.postPad, 9);        // 15 - 4 - 2
            expectEquals (m.insertLength, 8);   // min(8, 15 - 4 - 3)

            h.steps = 4; h.prePad = 12; h.postPad = 12; h.insertLength = 8;   // over budget: pre takes all 3
            m = h.padKnobMaxima();
            expectEquals (m.prePad, 3);
            expectEquals (m.postPad, 0);
            expectEquals (m.insertLength, 0);

            h.steps = 1;   // no padding budget at all
            m = h.padKnobMaxima();
            expectEquals (m.prePad + m.postPad + m.insertLength, 0);
        }

        beginTest ("Insert Start can't land in a Mute-mode pre-pad or run into the post-pad");
        {
            HitGenerator h;
            h.steps = 16; h.prePadMode = InsertMode::Mute;
            auto lay = h.clampLayout({ 0, 0, 4, 2, 1, 3 });
            expectEquals (lay.insertStart, 4);            // pulled out of the 4-step pre gap
            lay = h.clampLayout({ 0, 0, 4, 2, 15, 3 });
            expectEquals (lay.insertStart, 16 - 2 - 3);   // insert ends where the post gap begins
        }

        beginTest ("Pad-mode pre-pad: Insert Start counts from the end of the gap");
        {
            HitGenerator h;
            h.steps = 16;   // all Pad: Euclid section = 16 - 4 - 2 - 3 = 7 steps
            const auto [lo, hi] = HitGenerator::insertStartBounds (16, 4, 2, 3, InsertMode::Pad);
            expectEquals (lo, 0);
            expectEquals (hi, 7);
            const auto types = h.getStepTypes ({ 0, 0, 4, 2, 0, 3 });
            for (int i = 0; i < 4; ++i)  expect (types[(size_t) i] == StepType::PrePad);
            for (int i = 4; i < 7; ++i)  expect (types[(size_t) i] == StepType::InsertPad);
        }

        // ── The ring shows exactly as many hits as the Hits setting ──────────
        beginTest ("Hit count in getStepTypes / getPattern equals Hits (capped to the Euclid section)");
        {
            int checked = 0, wrong = 0;
            juce::String firstWrong;
            for (int steps = 1; steps <= 24; ++steps)
             for (int hits = 0; hits <= steps; ++hits)
              for (int pre = 0; pre <= 3; ++pre)
               for (int post = 0; post <= 3; ++post)
                for (int len = 0; len <= 2; ++len)
                 for (int modes = 0; modes < 8; ++modes)
                 {
                     HitGenerator h;
                     h.steps = steps; h.hits = hits; h.prePad = pre; h.postPad = post;
                     h.insertLength = len; h.insertStart = 1;
                     h.prePadMode  = (modes & 1) ? InsertMode::Mute : InsertMode::Pad;
                     h.postPadMode = (modes & 2) ? InsertMode::Mute : InsertMode::Pad;
                     h.insertMode  = (modes & 4) ? InsertMode::Mute : InsertMode::Pad;

                     const auto types = h.getStepTypes();
                     const auto pat   = h.getPattern();
                     int typeHits = 0, patHits = 0;
                     for (auto t : types) typeHits += (t == StepType::Hit) ? 1 : 0;
                     for (bool b : pat)   patHits  += b ? 1 : 0;

                     // Pad-mode reserves shrink the Euclid section; Mute zones may silence hits.
                     const auto lay = h.clampLayout({ hits, 0, pre, post, 1, len });
                     const int active = steps - (h.prePadMode == InsertMode::Pad ? lay.prePad : 0)
                                              - (h.postPadMode == InsertMode::Pad ? lay.postPad : 0)
                                              - (h.insertMode == InsertMode::Pad ? lay.insertLength : 0);
                     const int maxHits = std::min(hits, std::max(active, 0));
                     ++checked;
                     if (typeHits > maxHits || patHits > maxHits || typeHits != patHits
                         || (int) types.size() != steps)
                     {
                         if (wrong++ == 0)
                             firstWrong << "steps " << steps << " hits " << hits << " pre " << pre << " post " << post
                                        << " len " << len << " modes " << modes << " -> types " << typeHits
                                        << " pattern " << patHits << " size " << (int) types.size();
                     }
                 }
            expect (wrong == 0, juce::String (wrong) + " of " + juce::String (checked) + " layouts wrong; first: " + firstWrong);
        }
    }
};

static HitGeneratorTest hitGeneratorTest;
