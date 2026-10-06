#pragma once

#include "Modulation/ModTarget.h"
#include "Modulation/ModulationMatrix.h"
#include "Sequencer/VoiceSlot.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <string>
#include <unordered_map>

// Checks every product's unit tests run against its modulation target table, so the
// family standard (ModTarget.h) can't silently drift.
namespace mu_mod::checks
{

// Every live target names a parameter that exists (under `prefix`, e.g. "v0_" / "r0_" / "").
// Returns the ids of any that don't (empty = pass).
template <std::size_t N>
juce::StringArray missingParams(const ModTarget (&table)[N], juce::AudioProcessorValueTreeState& apvts,
                                const juce::String& prefix)
{
    juce::StringArray missing;
    for (const auto& t : table)
        if (t.param != nullptr && apvts.getParameter(prefix + t.param) == nullptr)
            missing.add(t.id);
    return missing;
}

// Depth is a percentage of the knob's range: a 10% assignment from a full-scale unipolar
// source moves a target seeded at 0.5 to exactly 0.6. Returns the ids that don't (empty = pass).
template <std::size_t N>
juce::StringArray depthNotProportional(const ModTarget (&table)[N])
{
    juce::StringArray wrong;
    for (const auto& t : table)
    {
        if (t.param == nullptr) continue;
        VoiceSlot slot;
        auto& cs = slot.controlSequences[0];
        cs.mode       = ControlSequence::Mode::Stepped;
        cs.polarity   = ControlSequence::Polarity::Unipolar;
        cs.stepValues = { 100.0f };
        cs.loopNoteValue = NoteValue::Quarter; cs.loopNoteMod = NoteMod::None; cs.loopMultiplier = 1;
        cs.stepNoteValue = NoteValue::Quarter; cs.stepNoteMod = NoteMod::None; cs.stepMultiplier = 1;

        ModulationAssignment a;
        a.id = "check"; a.sourceId = "cs0_output"; a.destinationId = t.id; a.depth = 10.0f;
        if (! slot.modulationMatrix.addAssignment(a)) { wrong.add(t.id); continue; }

        std::unordered_map<std::string_view, float> pv;
        pv[t.id] = 0.5f;
        slot.modulationMatrix.process(slot.controlSequences, 0.0, pv);
        if (std::abs(pv[t.id] - 0.6f) > 1.0e-4f) wrong.add(t.id);
    }
    return wrong;
}

} // namespace mu_mod::checks
