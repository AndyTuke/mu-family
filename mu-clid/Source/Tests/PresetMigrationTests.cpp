// C1 + C2 - preset format-migration tests.
//
// C1: migrateInsertSlotsV3 translates the legacy 9-field per-rhythm insert
//     (drvDrv/drvOut/drvDit/drvTon/drvBits/drvRate/eq*) into the 4 generic
//     normalised slots (insP1..insP4), per active algorithm, via mu_ui::actualToNorm.
// C2: migrateLegacyHostState rescales pre-v2 host-state ADSR times from the old
//     0..100 display range to 0..10 seconds, preserving the aEnvRel End sentinel.
//
// Both are pure ValueTree transforms - these guard the relocated PresetMigrations
// module against a silent regression in the on-disk -> in-memory translation.

#include <juce_data_structures/juce_data_structures.h>
#include "Persistence/PresetMigrations.h"
#include "Audio/InsertSlotConfig.h"   // mu_ui::actualToNorm
#include "Audio/AlgorithmNames.h"     // algorithm index reference

class PresetMigrationTest : public juce::UnitTest
{
public:
    PresetMigrationTest() : juce::UnitTest ("Preset format migrations", "PresetMigrations") {}

    // read a double property back as float (migrations store slots as doubles).
    static float prop (const juce::ValueTree& t, const char* name)
    {
        return (float) (double) t.getProperty (name);
    }

    void runTest() override
    {
        beginTest ("C1: migrateInsertSlotsV3 maps Bitcrusher's 4 fields to insP1..4");
        {
            // Bitcrusher (algo idx 4): bits->p1, rate->p2, dither->p3, tone->p4.
            constexpr int algo = 4;
            juce::ValueTree t ("Rhythm");
            t.setProperty ("r0_drvChar", "Bitcrusher", nullptr);
            t.setProperty ("r0_drvBits", 8.0,     nullptr);
            t.setProperty ("r0_drvRate", 24000.0, nullptr);
            t.setProperty ("r0_drvDit",  0.5,     nullptr);
            t.setProperty ("r0_drvTon",  5000.0,  nullptr);

            mu_pp_migrate::migrateInsertSlotsV3 (t, "r0_");

            expectWithinAbsoluteError (prop (t, "r0_insP1"), mu_ui::actualToNorm (8.0f,     algo, 0), 1e-4f, "bits -> p1");
            expectWithinAbsoluteError (prop (t, "r0_insP2"), mu_ui::actualToNorm (24000.0f, algo, 1), 1e-4f, "rate -> p2");
            expectWithinAbsoluteError (prop (t, "r0_insP3"), mu_ui::actualToNorm (0.5f,     algo, 2), 1e-4f, "dither -> p3");
            expectWithinAbsoluteError (prop (t, "r0_insP4"), mu_ui::actualToNorm (5000.0f,  algo, 3), 1e-4f, "tone -> p4");
        }

        beginTest ("C1: migrateInsertSlotsV3 maps SoftClip drive/output/tone");
        {
            // SoftClip (algo idx 1): drive->p1, output->p2, (p3 unused=0), tone->p4.
            constexpr int algo = 1;
            juce::ValueTree t ("Rhythm");
            t.setProperty ("r0_drvChar", "SoftClip", nullptr);
            t.setProperty ("r0_drvDrv", 0.7,    nullptr);
            t.setProperty ("r0_drvOut", 0.5,    nullptr);
            t.setProperty ("r0_drvTon", 8000.0, nullptr);

            mu_pp_migrate::migrateInsertSlotsV3 (t, "r0_");

            expectWithinAbsoluteError (prop (t, "r0_insP1"), mu_ui::actualToNorm (0.7f,    algo, 0), 1e-4f, "drive -> p1");
            expectWithinAbsoluteError (prop (t, "r0_insP2"), mu_ui::actualToNorm (0.5f,    algo, 1), 1e-4f, "output -> p2");
            expectWithinAbsoluteError (prop (t, "r0_insP3"), mu_ui::actualToNorm (0.0f,    algo, 2), 1e-4f, "unused -> p3 (0)");
            expectWithinAbsoluteError (prop (t, "r0_insP4"), mu_ui::actualToNorm (8000.0f, algo, 3), 1e-4f, "tone -> p4");
        }

        beginTest ("C1: migrateInsertSlotsV3 is idempotent (already-v3 left untouched)");
        {
            juce::ValueTree t ("Rhythm");
            t.setProperty ("r0_drvChar", "Bitcrusher", nullptr);
            t.setProperty ("r0_drvBits", 8.0,   nullptr);
            t.setProperty ("r0_insP1",   0.123, nullptr);   // a v3 slot is already present

            mu_pp_migrate::migrateInsertSlotsV3 (t, "r0_");

            expectWithinAbsoluteError (prop (t, "r0_insP1"), 0.123f, 1e-6f,
                "must not overwrite an existing v3 slot");
        }

        beginTest ("C1: migrateInsertSlotsV3 no-ops when there is nothing to migrate");
        {
            juce::ValueTree t ("Rhythm");   // no drvChar
            mu_pp_migrate::migrateInsertSlotsV3 (t, "r0_");
            expect (! t.hasProperty ("r0_insP1"), "no insert slots created from an empty tree");
        }

        beginTest ("C2: migrateLegacyHostState rescales ADSR 0..100 -> 0..10 s");
        {
            juce::ValueTree s ("PARAMETERS");
            auto add = [&s] (const char* id, double v)
            {
                juce::ValueTree p ("PARAM");
                p.setProperty ("id", id, nullptr);
                p.setProperty ("value", v, nullptr);
                s.addChild (p, -1, nullptr);
            };
            add ("r0_aEnvAtk", 100.0);   // -> jlimit(0,10, 100*0.03) = 3.0
            add ("r0_fEnvDec", 50.0);    // -> 1.5
            add ("r0_aEnvRel", 100.0);   // -> End-mode sentinel = 10.0
            add ("r0_fltCut",  800.0);   // non-ADSR -> unchanged
            add ("global_x",   100.0);   // non-r{0-7}_ -> unchanged

            mu_pp_migrate::migrateLegacyHostState (s);

            auto val = [&s] (const char* id) -> float
            {
                for (int i = 0; i < s.getNumChildren(); ++i)
                    if (s.getChild (i).getProperty ("id").toString() == id)
                        return (float) (double) s.getChild (i).getProperty ("value");
                return -999.0f;
            };
            expectWithinAbsoluteError (val ("r0_aEnvAtk"), 3.0f,   1e-4f, "attack 100 -> 3.0 s");
            expectWithinAbsoluteError (val ("r0_fEnvDec"), 1.5f,   1e-4f, "filter decay 50 -> 1.5 s");
            expectWithinAbsoluteError (val ("r0_aEnvRel"), 10.0f,  1e-4f, "release 100 -> End sentinel 10.0");
            expectWithinAbsoluteError (val ("r0_fltCut"),  800.0f, 1e-4f, "non-ADSR param untouched");
            expectWithinAbsoluteError (val ("global_x"),   100.0f, 1e-4f, "non-rhythm param untouched");
            expectEquals ((int) s.getProperty ("formatVersion"),
                          mu_pp_migrate::kCurrentStateFormatVersion,
                          "formatVersion bumped to current after migration");
        }

        beginTest ("C2: migrateLegacyHostState is a no-op on already-current state");
        {
            juce::ValueTree s ("PARAMETERS");
            s.setProperty ("formatVersion", mu_pp_migrate::kCurrentStateFormatVersion, nullptr);
            juce::ValueTree p ("PARAM");
            p.setProperty ("id", "r0_aEnvAtk", nullptr);
            p.setProperty ("value", 5.0, nullptr);   // already in seconds
            s.addChild (p, -1, nullptr);

            mu_pp_migrate::migrateLegacyHostState (s);

            expectWithinAbsoluteError ((float) (double) s.getChild (0).getProperty ("value"), 5.0f, 1e-6f,
                "current-version state must not be rescaled");
        }
    }
};

static PresetMigrationTest presetMigrationTest;
