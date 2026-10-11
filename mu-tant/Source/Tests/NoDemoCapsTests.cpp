// No-demo-cap regression tests (data layer).
//
// With no licence key, mu-Tant is fully functional: eight voices, gate patterns
// longer than 16 cells, and per-layer / full-preset saves. These tests push eight
// voices' worth of long gate patterns through serialiseGate -> XML text ->
// deserialiseGate (the default, uncapped deserialise a licence-free load now uses)
// and prove the family layer limit is still eight. A processor-level load is not
// reachable from this console target (PluginProcessor drags in the UI tree), so the
// numVoices clamp itself is covered by the manual smoke plan.

#include <juce_core/juce_core.h>
#include <juce_data_structures/juce_data_structures.h>
#include "MuLimits.h"
#include "Sequencer/GatePattern.h"
#include "Sequencer/GatePatternSerialise.h"

class NoDemoCapsTest : public juce::UnitTest
{
public:
    NoDemoCapsTest() : juce::UnitTest ("mu-tant no demo caps", "mu-tant") {}

    // Serialise a tree to XML text and parse it back (what a saved file does).
    static juce::ValueTree throughXmlText (const juce::ValueTree& t)
    {
        auto xml = juce::XmlDocument::parse (t.createXml()->toString());
        return xml != nullptr ? juce::ValueTree::fromXml (*xml) : juce::ValueTree();
    }

    // A pattern 8 bars of 1/16 (128 cells) with envelopes well past cell 16.
    static void fillLongPattern (mu_tant::GatePattern& g, int voice)
    {
        using namespace mu_tant;
        g.subdivision = GatePattern::Subdivision::Sixteenth;
        g.patternLengthBars = 8;
        g.envelopes.clear();
        for (int k = 0; k < 4; ++k)
        {
            GateEnvelope e;
            e.startCell   = 20 + k * 20 + voice;   // all beyond the old 16-cell cap
            e.lengthCells = 5 + k;
            e.split       = 0.25f * (float) k;
            g.envelopes.push_back (e);
        }
        g.hasEnvelopes.store (true, std::memory_order_relaxed);
    }

    void runTest() override
    {
        using namespace mu_tant;

        beginTest ("limit: eight voices is the family maximum");
        expectEquals (mu_limits::kMaxLayers, 8);

        beginTest ("gate longer than 16 cells survives deserialise uncapped");
        {
            GatePattern src;
            fillLongPattern (src, 0);
            expect (src.totalCells() > 16, "source pattern is longer than 16 cells");

            GatePattern dst;
            deserialiseGate (throughXmlText (serialiseGate (src, "Gate")), dst);

            expectEquals (dst.totalCells(), src.totalCells(), "pattern length kept");
            expectEquals ((int) dst.envelopes.size(), 4, "all envelopes kept");
            for (size_t i = 0; i < dst.envelopes.size(); ++i)
            {
                expectEquals (dst.envelopes[i].startCell,   src.envelopes[i].startCell);
                expectEquals (dst.envelopes[i].lengthCells, src.envelopes[i].lengthCells);
            }
        }

        beginTest ("full preset: eight voices x three long gates survive save/load as text");
        {
            const char* tags[] = { "Gate", "FilterGate", "PitchGate" };

            juce::ValueTree preset ("MuTantPreset");
            preset.setProperty ("numVoices", mu_limits::kMaxLayers, nullptr);
            for (int v = 0; v < mu_limits::kMaxLayers; ++v)
            {
                juce::ValueTree voice ("Voice");
                for (const char* tag : tags)
                {
                    GatePattern g;
                    fillLongPattern (g, v);
                    voice.addChild (serialiseGate (g, tag), -1, nullptr);
                }
                preset.addChild (voice, -1, nullptr);
            }

            const auto loaded = throughXmlText (preset);
            expectEquals ((int) loaded.getProperty ("numVoices"), 8, "numVoices == 8 survives");
            expectEquals (loaded.getNumChildren(), 8, "all 8 voices survive");

            for (int v = 0; v < loaded.getNumChildren(); ++v)
                for (int p = 0; p < 3; ++p)
                {
                    GatePattern expected, got;
                    fillLongPattern (expected, v);
                    deserialiseGate (loaded.getChild (v).getChild (p), got);
                    expectEquals (got.totalCells(), expected.totalCells(),
                                  "voice " + juce::String (v) + " pattern " + juce::String (p) + " length");
                    expectEquals ((int) got.envelopes.size(), 4);
                    expectEquals (got.envelopes.back().startCell, expected.envelopes.back().startCell);
                }
        }

        beginTest ("save: full-preset and per-layer files write and read back");
        {
            const auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                 .getChildFile ("mu-tant-tests-nodemo");
            dir.createDirectory();

            GatePattern g;
            fillLongPattern (g, 0);

            // Per-layer preset (.muPattern): a single Gate tree.
            const auto layerFile = dir.getChildFile ("layer.muPattern");
            expect (layerFile.replaceWithText (serialiseGate (g, "Gate").createXml()->toString()),
                    "per-layer file written");

            // Full preset (.muTant): eight voices.
            juce::ValueTree full ("MuTantPreset");
            full.setProperty ("numVoices", 8, nullptr);
            for (int v = 0; v < 8; ++v)
            {
                juce::ValueTree voice ("Voice");
                voice.addChild (serialiseGate (g, "Gate"), -1, nullptr);
                full.addChild (voice, -1, nullptr);
            }
            const auto fullFile = dir.getChildFile ("full.muTant");
            expect (fullFile.replaceWithText (full.createXml()->toString()), "full-preset file written");

            auto lx = juce::XmlDocument::parse (layerFile);
            auto fx = juce::XmlDocument::parse (fullFile);
            expect (lx != nullptr && fx != nullptr, "both files parse");
            if (lx != nullptr && fx != nullptr)
            {
                GatePattern back;
                deserialiseGate (juce::ValueTree::fromXml (*lx), back);
                expectEquals (back.totalCells(), g.totalCells(), "per-layer gate length");
                expectEquals (juce::ValueTree::fromXml (*fx).getNumChildren(), 8, "full preset voices");
            }
            dir.deleteRecursively();
        }
    }
};

static NoDemoCapsTest noDemoCapsTest;
