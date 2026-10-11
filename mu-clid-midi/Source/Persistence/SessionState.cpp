#include "Persistence/SessionState.h"
#include "Persistence/LayerState.h"         // mu-core: mu_pp::readRowValue
#include "Persistence/PresetHelpers.h"      // mu-Clid: readKindedPropertyAsActualV2 (shared until Rhythm moves to mu-core)
#include "Persistence/RhythmParamTable.h"   // mu-Clid: kRhythmParamDefs
#include "Plugin/ParameterLayout.h"

namespace mu_clid_midi::session {

juce::String toXmlString(juce::AudioProcessorValueTreeState& apvts)
{
    return apvts.copyState().toXmlString();
}

// Sets a parameter to an actual (de-normalised) value, notifying the host.
static void setActual(juce::AudioProcessorValueTreeState& apvts, const juce::String& id, float actual)
{
    if (auto* p = apvts.getParameter(id))
        p->setValueNotifyingHost(p->convertTo0to1(actual));
}

void applyLegacySession(const juce::ValueTree& root, juce::AudioProcessorValueTreeState& apvts)
{
    // Phase 1: the first rhythm's Euclid values, written per the shared rhythm parameter table.
    const auto rhythm = root.getChildWithName("Rhythm");
    if (rhythm.isValid())
    {
        for (int j = 0; j < mu_clid::kRhythmParamCount; ++j)
        {
            const auto& def = mu_clid::kRhythmParamDefs[j];
            const juce::String id = juce::String(kRhythmIdPrefix) + def.suffix;
            if (apvts.getParameter(id) == nullptr || ! rhythm.hasProperty(def.suffix)) continue;

            const float v = mu_clid::readKindedPropertyAsActualV2(rhythm, def.suffix, def.kind, def.algorithmNames);
            if (! std::isnan(v))
                setActual(apvts, id, v);
        }
    }

    // Phase 2: the master loop, from the preset's global state.
    const auto globals = root.getChildWithName("GlobalState");
    if (globals.isValid() && globals.hasProperty(kMasterLoopId))
        setActual(apvts, kMasterLoopId, (float) (int) globals.getProperty(kMasterLoopId));

    // Phase 3: the rows a session adds for every parameter the preset tree does not carry
    // (the MIDI note and accent live here).
    const auto rows = root.getChildWithName("SessionParams");
    for (int i = 0; i < rows.getNumChildren(); ++i)
    {
        const auto row = rows.getChild(i);
        if (auto* p = apvts.getParameter(row.getProperty("id").toString()))
        {
            float normalised = 0.0f;
            if (mu_pp::readRowValue(row, *p, normalised) && p->getValue() != normalised)
                p->setValueNotifyingHost(normalised);
        }
    }
}

bool restoreFromXmlString(const juce::String& xml, juce::AudioProcessorValueTreeState& apvts, juce::String& error)
{
    const auto parsed = juce::parseXML(xml);
    if (parsed == nullptr)
    {
        error = "Host state restore failed: could not parse XML";
        return false;
    }

    const auto tree = juce::ValueTree::fromXml(*parsed);
    if (tree.hasType("MuClidPreset"))   // a session mu-Clid Lite saved
    {
        applyLegacySession(tree, apvts);
        return true;
    }
    if (tree.isValid() && tree.hasType(apvts.state.getType()))
    {
        apvts.replaceState(tree);
        return true;
    }

    error = "Host state restore failed: invalid tree";
    return false;
}

} // namespace mu_clid_midi::session
