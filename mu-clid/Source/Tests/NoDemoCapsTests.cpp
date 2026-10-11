// No-demo-cap regression tests (data layer).
//
// With no licence key, mu-Clid is fully functional: all eight rhythms, step counts
// up to 64 and per-layer / full-preset saves must work. These tests exercise the
// data the preset code carries (an 8-rhythm full-preset tree, 64-step rhythms) through
// the same XML text a saved file holds, and prove the family limits are not lowered.
// A processor-level load (PresetIO against a live PluginProcessor) is not reachable
// from this console target, so the numRhythms clamp itself is covered by the
// listening / smoke plan.

#include <juce_core/juce_core.h>
#include <juce_data_structures/juce_data_structures.h>
#include "MuLimits.h"
#include "Sequencer/GeneratorAliases.h"
#include "Sequencer/GeneratorAliases.h"
#include "Persistence/RhythmParamTable.h"
#include "Persistence/PresetHelpers.h"

using namespace mu_clid;

class NoDemoCapsTest : public juce::UnitTest
{
public:
    NoDemoCapsTest() : juce::UnitTest ("No demo caps (unlicensed is fully functional)", "Preset") {}

    // Find a kRhythmParamDefs row by suffix.
    static const RhythmParamDef* findDef (const char* suffix)
    {
        for (int i = 0; i < kRhythmParamCount; ++i)
            if (juce::String (kRhythmParamDefs[i].suffix) == suffix)
                return &kRhythmParamDefs[i];
        return nullptr;
    }

    // Write steps into a Rhythm and return what push() reads back.
    static float setSteps (Rhythm& r, float steps)
    {
        const auto* def = findDef ("stepsA");
        bool pd = false, vd = false;
        def->apply (steps, r, pd, vd);
        return def->push (r);
    }

    // Serialise a tree to XML text and parse it back (what a saved file does).
    static juce::ValueTree throughXmlText (const juce::ValueTree& t)
    {
        const auto text = t.createXml()->toString();
        auto xml = juce::XmlDocument::parse (text);
        return xml != nullptr ? juce::ValueTree::fromXml (*xml) : juce::ValueTree();
    }

    void runTest() override
    {
        beginTest ("limits: eight layers and 64 steps are the family maximums");
        {
            expectEquals (mu_limits::kMaxLayers, 8);
            expectEquals ((int) SequencerEngine::MaxRhythms, 8);
            expectEquals ((int) HitGenerator::kMaxSteps, 64);
        }

        beginTest ("step counts 1..64 are accepted by the stepsA parameter (no 16-step clamp)");
        {
            expect (findDef ("stepsA") != nullptr, "stepsA row exists");
            for (int s : { 1, 15, 16, 17, 24, 32, 48, 63, 64 })
            {
                Rhythm r;
                expectWithinAbsoluteError (setSteps (r, (float) s), (float) s, 1e-4f,
                                           "steps " + juce::String (s) + " accepted");
            }
        }

        beginTest ("step counts above 16 survive the kinded-property XML codec");
        {
            const auto* def = findDef ("stepsA");
            for (int s : { 17, 32, 64 })
            {
                Rhythm r1;
                setSteps (r1, (float) s);

                juce::ValueTree t ("RhythmPreset");
                writeKindedProperty (t, "r0_stepsA", def->push (r1), def->kind, def->algorithmNames);
                const auto back = throughXmlText (t);

                Rhythm r2;
                bool pd = false, vd = false;
                def->apply (readKindedPropertyAsActualV2 (back, "r0_stepsA", def->kind, def->algorithmNames),
                            r2, pd, vd);
                expectWithinAbsoluteError (def->push (r2), (float) s, 1e-4f,
                                           "steps " + juce::String (s) + " round-trip");
            }
        }

        beginTest ("full preset: eight <Rhythm> children with 64 steps survive save/load as text");
        {
            const auto* def = findDef ("stepsA");
            juce::ValueTree preset ("MuClidPreset");
            preset.setProperty ("numRhythms", mu_limits::kMaxLayers, nullptr);

            // One child per rhythm, each with a distinct step count up to 64.
            for (int i = 0; i < mu_limits::kMaxLayers; ++i)
            {
                Rhythm r;
                setSteps (r, (float) (64 - i * 3));
                juce::ValueTree node ("Rhythm");
                writeKindedProperty (node, "stepsA", def->push (r), def->kind, def->algorithmNames);
                preset.addChild (node, -1, nullptr);
            }

            const auto loaded = throughXmlText (preset);
            expect (loaded.isValid(), "preset text parses");
            expectEquals ((int) loaded.getProperty ("numRhythms"), 8, "numRhythms == 8 survives");
            expectEquals (loaded.getNumChildren(), 8, "all 8 rhythms survive");

            for (int i = 0; i < loaded.getNumChildren(); ++i)
            {
                Rhythm r;
                bool pd = false, vd = false;
                def->apply (readKindedPropertyAsActualV2 (loaded.getChild (i), "stepsA", def->kind,
                                                          def->algorithmNames), r, pd, vd);
                expectWithinAbsoluteError (def->push (r), (float) (64 - i * 3), 1e-4f,
                                           "rhythm " + juce::String (i) + " steps");
            }
        }

        beginTest ("save: full preset and per-layer preset files write and read back");
        {
            const auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                 .getChildFile ("mu-clid-tests-nodemo");
            dir.createDirectory();
            const auto def = findDef ("stepsA");

            Rhythm r;
            setSteps (r, 64.0f);

            // Per-layer preset (.muRhythm root, prefix r0_).
            juce::ValueTree layer ("RhythmPreset");
            writeKindedProperty (layer, "r0_stepsA", def->push (r), def->kind, def->algorithmNames);
            const auto layerFile = dir.getChildFile ("layer.muRhythm");
            expect (layerFile.replaceWithText (layer.createXml()->toString()), "per-layer file written");

            // Full preset (.muClid root) holding eight rhythms.
            juce::ValueTree full ("MuClidPreset");
            full.setProperty ("numRhythms", 8, nullptr);
            for (int i = 0; i < 8; ++i)
            {
                juce::ValueTree node ("Rhythm");
                writeKindedProperty (node, "stepsA", def->push (r), def->kind, def->algorithmNames);
                full.addChild (node, -1, nullptr);
            }
            const auto fullFile = dir.getChildFile ("full.muClid");
            expect (fullFile.replaceWithText (full.createXml()->toString()), "full-preset file written");

            auto lx = juce::XmlDocument::parse (layerFile);
            auto fx = juce::XmlDocument::parse (fullFile);
            expect (lx != nullptr && fx != nullptr, "both files parse");
            if (lx != nullptr && fx != nullptr)
            {
                const auto lt = juce::ValueTree::fromXml (*lx);
                const auto ft = juce::ValueTree::fromXml (*fx);
                expectWithinAbsoluteError (readKindedPropertyAsActualV2 (lt, "r0_stepsA", def->kind,
                                                                         def->algorithmNames), 64.0f, 1e-4f);
                expectEquals (ft.getNumChildren(), 8);
            }
            dir.deleteRecursively();
        }
    }
};

static NoDemoCapsTest noDemoCapsTest;
