#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "UI/Components/DropdownSelect.h"

// A selector's items taken from its choice parameter, so the list shown can never drift from
// the list the parameter stores (and the host shows).
namespace mu_ui
{

// The display names of choice parameter `id` (empty when it is not an AudioParameterChoice).
inline juce::StringArray choiceNames(juce::AudioProcessorValueTreeState& apvts, const juce::String& id)
{
    if (auto* c = dynamic_cast<juce::AudioParameterChoice*>(apvts.getParameter(id)))
        return c->choices;
    jassertfalse;   // not a choice parameter — the caller wants the list from somewhere else
    return {};
}

// Fill `dd` with choice parameter `id`'s names; item id = choice index + 1, the
// ComboBoxAttachment contract.
inline void addChoiceItems(DropdownSelect& dd, juce::AudioProcessorValueTreeState& apvts, const juce::String& id)
{
    const auto names = choiceNames(apvts, id);
    for (int i = 0; i < names.size(); ++i)
        dd.addItem(names[i], i + 1);
}

} // namespace mu_ui
