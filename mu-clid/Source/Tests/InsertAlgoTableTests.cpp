// InsertAlgoTable invariants test.
//
// Catches the class of bug where the per-algorithm UI driver presents an
// algorithm as "all knobs hidden" - either because the slot config table
// genuinely has every label==nullptr for that algo, or because the row
// count drifts away from kInsertAlgorithmCount and a non-existent row
// gets read with garbage labels.
//
// These are STRUCTURAL invariants: changes to either table that violate
// them will fail at unit-test startup before the runtime "knobs vanish"
// symptom hits the user.

#include <juce_core/juce_core.h>
#include <cstring>
#include "Audio/InsertSlotConfig.h"
#include "Audio/AlgorithmNames.h"
#include "Modulation/ModulationDestinations.h"

class InsertAlgoTableTest : public juce::UnitTest
{
public:
    InsertAlgoTableTest() : juce::UnitTest ("Insert algo table invariants", "InsertSlotConfig") {}

    void runTest() override
    {
        beginTest ("kInsertAlgoSlots row count matches kInsertAlgorithmCount");
        {
            const int rows = (int) (sizeof(mu_ui::kInsertAlgoSlots)
                                  / sizeof(mu_ui::kInsertAlgoSlots[0]));
            expectEquals (rows, mu_audio::kInsertAlgorithmCount,
                "kInsertAlgoSlots[] must have one row per algorithm in kInsertAlgorithmNames - drift will read garbage labels and cause the UI to present blank knobs");
        }

        beginTest ("kInsertAlgoDefaults row count matches kInsertAlgorithmCount");
        {
            const int rows = (int) (sizeof(mu_ui::kInsertAlgoDefaults)
                                  / sizeof(mu_ui::kInsertAlgoDefaults[0]));
            expectEquals (rows, mu_audio::kInsertAlgorithmCount,
                "kInsertAlgoDefaults[] must have one row per algorithm - first-visit restore would OOB without this");
        }

        beginTest ("every non-None algorithm has at least one visible slot");
        {
            // Guard against the "all slots null" failure mode: if a non-None
            // algo had all four slots labelled nullptr, configureInsertAlgorithm
            // would hide every knob and the algorithm would present as empty.
            // This guarantees the UI driver always has SOMETHING to show - even if some
            // future algorithm only needs one knob, at least one is visible.
            for (int algo = 1; algo < mu_audio::kInsertAlgorithmCount; ++algo)
            {
                int visible = 0;
                for (int slot = 0; slot < mu_ui::kInsertSlotCount; ++slot)
                    if (mu_ui::kInsertAlgoSlots[algo][slot].label != nullptr)
                        ++visible;
                expect (visible > 0,
                    "Algorithm '" + juce::String(mu_audio::kInsertAlgorithmNames[algo])
                    + "' (idx " + juce::String(algo) + ") has zero visible slots "
                    + "- UI would render as empty knobs");
            }
        }

        beginTest ("slot ranges are well-formed (min < max for visible slots)");
        {
            // A visible slot with min >= max would make normToActual / actualToNorm
            // collapse to zero, producing a knob that can't be moved.
            for (int algo = 0; algo < mu_audio::kInsertAlgorithmCount; ++algo)
            {
                for (int slot = 0; slot < mu_ui::kInsertSlotCount; ++slot)
                {
                    const auto& cfg = mu_ui::kInsertAlgoSlots[algo][slot];
                    if (cfg.label == nullptr) continue;   // hidden slot - range doesn't matter
                    expect (cfg.maxVal > cfg.minVal,
                        "Algorithm '" + juce::String(mu_audio::kInsertAlgorithmNames[algo])
                        + "' slot " + juce::String(slot) + " (label '" + juce::String(cfg.label)
                        + "') has invalid range minVal=" + juce::String(cfg.minVal)
                        + " >= maxVal=" + juce::String(cfg.maxVal));
                }
            }
        }

        beginTest ("ModDest::kTable insert.p1..p4 occupy indices 10..13");
        {
            // ModDest::populate computes insert-section IDs as `10 + slot + 1` so a
            // reorder of kTable would silently aim the dropdown at `_reserved.*`
            // placeholders - the exact bug that caused blank dropdown items
            // for clip / fold / bitcrusher / EQ / Karplus / Vocoder algorithms).
            expect (std::strcmp (ModDest::kTable[10].id, "insert.p1") == 0,
                "kTable[10] must be insert.p1 - see the backlog; if you reorder kTable, update populate()");
            expect (std::strcmp (ModDest::kTable[11].id, "insert.p2") == 0,
                "kTable[11] must be insert.p2 - see the backlog");
            expect (std::strcmp (ModDest::kTable[12].id, "insert.p3") == 0,
                "kTable[12] must be insert.p3 - see the backlog");
            expect (std::strcmp (ModDest::kTable[13].id, "insert.p4") == 0,
                "kTable[13] must be insert.p4 - see the backlog");
        }

        beginTest ("normToActual <-> actualToNorm round-trips for every visible slot");
        {
            // Catches a stale skew handler / formula divergence: if
            // normToActual and actualToNorm don't invert each other, the UI
            // value <-> APVTS storage round-trip silently corrupts presets.
            constexpr float kTestNorms[] = { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f };
            for (int algo = 0; algo < mu_audio::kInsertAlgorithmCount; ++algo)
            {
                for (int slot = 0; slot < mu_ui::kInsertSlotCount; ++slot)
                {
                    const auto& cfg = mu_ui::kInsertAlgoSlots[algo][slot];
                    if (cfg.label == nullptr) continue;
                    for (float norm : kTestNorms)
                    {
                        const float actual = mu_ui::normToActual(norm, algo, slot);
                        const float backNorm = mu_ui::actualToNorm(actual, algo, slot);
                        // IntStep snaps to nearest integer in [min, max], so
                        // for spans < 1 the round-trip is exact; for larger
                        // spans normToActual rounds to int and actualToNorm
                        // re-normalises - within 1/(maxVal - minVal).
                        const float tol = (cfg.skew == mu_ui::SkewMode::IntStep)
                            ? 1.5f / (cfg.maxVal - cfg.minVal)
                            : 1e-4f;
                        expectWithinAbsoluteError (backNorm, norm, tol,
                            "Algorithm '" + juce::String(mu_audio::kInsertAlgorithmNames[algo])
                            + "' slot " + juce::String(slot) + " (label '" + juce::String(cfg.label)
                            + "') failed norm round-trip at norm=" + juce::String(norm));
                    }
                }
            }
        }

        beginTest ("amp.release retired as a modulation destination (Finding 2)");
        {
            // A one-shot step trigger never note-offs during playback, so the amp
            // env never reaches release - modulating it is a no-op. It was retired
            // from kTable; verify it's no longer a valid target, the retire didn't
            // shift the insert.p1..p4 indices, and the other amp env destinations
            // stayed valid.
            expect (! ModDest::isValidDestinationId ("amp.release"),
                "amp.release must not be a valid modulation destination");
            expect (std::strcmp (ModDest::kTable[10].id, "insert.p1") == 0,
                "insert.p1 must remain at index 10 after the amp.release retire");
            expect (ModDest::isValidDestinationId ("amp.attack"),  "amp.attack still valid");
            expect (ModDest::isValidDestinationId ("amp.decay"),   "amp.decay still valid");
            expect (ModDest::isValidDestinationId ("amp.sustain"), "amp.sustain still valid");
        }
    }
};

static InsertAlgoTableTest insertAlgoTableTest;
