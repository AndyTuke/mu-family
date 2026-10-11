#include "Persistence/SessionState.h"
#include "Persistence/LayerState.h"         // mu-core: mu_pp::readRowValue
#include "Persistence/RhythmPresetHelpers.h"   // readKindedPropertyAsActualV2
#include "Persistence/RhythmParamTable.h"   // kRhythmParamDefs
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
        for (int j = 0; j < mu_core::kRhythmParamCount; ++j)
        {
            const auto& def = mu_core::kRhythmParamDefs[j];
            const juce::String id = juce::String(kRhythmIdPrefix) + def.suffix;
            if (apvts.getParameter(id) == nullptr || ! rhythm.hasProperty(def.suffix)) continue;

            const float v = mu_core::readKindedPropertyAsActualV2(rhythm, def.suffix, def.kind, def.algorithmNames);
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

// The rhythm name (the preset name over the older r0_name) and colour a state's root carries.
static Identity readIdentity(const juce::ValueTree& root)
{
    Identity id;
    const auto presetName = root.getProperty("presetName");
    const auto rhythmName = root.getProperty("r0_name");
    if (presetName.isString() && presetName.toString().isNotEmpty())      id.name = presetName.toString();
    else if (rhythmName.isString() && rhythmName.toString().isNotEmpty()) id.name = rhythmName.toString();
    if (root.hasProperty("r0_colour"))
        id.colourIndex = juce::jlimit(0, 255, (int) root.getProperty("r0_colour"));
    return id;
}

void resetToDefaults(juce::AudioProcessorValueTreeState& apvts)
{
    for (auto* param : apvts.processor.getParameters())
        if (auto* p = dynamic_cast<juce::RangedAudioParameter*>(param))
            p->setValueNotifyingHost(p->getDefaultValue());
}

bool restoreFromXmlString(const juce::String& xml, juce::AudioProcessorValueTreeState& apvts, juce::String& error,
                          Identity* identity)
{
    const auto parsed = juce::parseXML(xml);
    if (parsed == nullptr)
    {
        error = "Host state restore failed: could not parse XML";
        return false;
    }

    auto tree = juce::ValueTree::fromXml(*parsed);
    if (tree.hasType("MuClidPreset"))   // a session mu-Clid Lite saved
    {
        resetToDefaults(apvts);
        applyLegacySession(tree, apvts);
        if (identity != nullptr) *identity = readIdentity(tree);
        return true;
    }
    if (tree.isValid() && tree.hasType(apvts.state.getType()))
    {
        // Drop parameter nodes this product has no parameter for (an older layout), so they are not
        // carried in the state and written back out on every save.
        for (int i = tree.getNumChildren(); --i >= 0;)
        {
            const auto child = tree.getChild(i);
            if (child.hasType("PARAM") && apvts.getParameter(child.getProperty("id").toString()) == nullptr)
                tree.removeChild(i, nullptr);
        }

        resetToDefaults(apvts);
        apvts.replaceState(tree);
        if (identity != nullptr) *identity = readIdentity(tree);
        return true;
    }

    error = "Host state restore failed: invalid tree";
    return false;
}

} // namespace mu_clid_midi::session
