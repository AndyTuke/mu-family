// Partial-class TU split from PluginProcessor.cpp: the parameter listener and the two-way sync
// between the parameters and the one Rhythm. Mirrors mu-Clid's PluginProcessor_APVTS.cpp for the
// Euclid parameters it shares (the layout itself is in ParameterLayout.h).

#include "Plugin/PluginProcessor.h"
#include "Plugin/ParameterLayout.h"
#include "Audio/SpinLock.h"                      // mu-core: spin lock helpers
#include "Persistence/ScopedApvtsLoading.h"      // mu-core: raises apvtsLoading for a bulk push
#include "Persistence/RhythmParamTable.h"        // mu-Clid: kRhythmParamDefs, the shared suffix table

#include <cstring>

namespace mu_clid_midi {

void PluginProcessor::parameterChanged(const juce::String& id, float v)
{
    // Rhythm values: r0_<suffix>
    if (id.startsWith(kRhythmIdPrefix))
    {
        syncRhythmParam(id.substring((int) std::strlen(kRhythmIdPrefix)), v);
        return;
    }
    if (id == kMasterLoopId)
        sequencer.setMasterLoopSteps(juce::jmin((int) v * 16, 256));   // 0 = free
}

void PluginProcessor::syncRhythmParam(const juce::String& suffix, float v)
{
    const auto* def = mu_clid::findRhythmParamDef(suffix);
    if (def == nullptr) return;

    Rhythm& r = getRhythm();
    bool patternDirty = false;
    bool voiceDirty   = false;

    // Hold the voice-params lock for the field write so the audio thread cannot see a torn value.
    mu_core::spinLock(r.voiceParamsLock);
    def->apply(v, r, patternDirty, voiceDirty);
    mu_core::spinUnlock(r.voiceParamsLock);

    if (patternDirty && ! apvtsLoading.load(std::memory_order_acquire))
        sequencer.updatePattern(0);
}

// Rebuilds the rhythm from the current parameter values, whether or not parameterChanged fired.
void PluginProcessor::syncRhythmFromApvts()
{
    Rhythm& r = getRhythm();
    const juce::String prefix(kRhythmIdPrefix);
    bool patternDirty = false;
    bool voiceDirty   = false;

    // One suffix at a time, so the audio thread can take its brief lock between the writes.
    for (int j = 0; j < mu_clid::kRhythmParamCount; ++j)
        if (auto* raw = apvts.getRawParameterValue(prefix + mu_clid::kRhythmParamDefs[j].suffix))
        {
            mu_core::spinLock(r.voiceParamsLock);
            mu_clid::kRhythmParamDefs[j].apply(raw->load(), r, patternDirty, voiceDirty);
            mu_core::spinUnlock(r.voiceParamsLock);
        }

    sequencer.updatePattern(0);
    sequencer.setMasterLoopSteps(juce::jmin((int) apvts.getRawParameterValue(kMasterLoopId)->load() * 16, 256));
}

// Mirrors the Rhythm's values into the parameters (used once, for the default rhythm).
void PluginProcessor::pushRhythmToApvts()
{
    mu_core::ScopedApvtsLoading guard(apvtsLoading);
    const Rhythm& r = getRhythm();
    const juce::String prefix(kRhythmIdPrefix);

    for (int i = 0; i < mu_clid::kRhythmParamCount; ++i)
    {
        const auto& def = mu_clid::kRhythmParamDefs[i];
        if (auto* p = apvts.getParameter(prefix + def.suffix))
            p->setValueNotifyingHost(p->convertTo0to1(def.push(r)));
    }
}

} // namespace mu_clid_midi
