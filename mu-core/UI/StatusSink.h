#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// The status bar shows the name and value of the last control touched, in every app, without fail (Andy's hard rule).
//
// An editor (or any ancestor that owns the status bar) is a StatusSink. A control that has been given a status name
// (DropdownSelect::setStatusName, NudgeInput::setStatusName) reports its change to the nearest StatusSink above it, so a product does
// not have to wire a hook for each one and a new control cannot silently report to nobody. Knobs report through their own
// onStatusUpdate (ShellChrome::forwardKnobStatus, LayerPanel::onStatusUpdate); this covers the controls that have no such hook.
namespace mu_ui
{

struct StatusSink
{
    virtual ~StatusSink() = default;
    virtual void showControlStatus(const juce::String& name, const juce::String& value) = 0;
};

// Report `name` / `value` from `control` to the nearest StatusSink above it (nothing if there is none).
inline void reportStatus(juce::Component& control, const juce::String& name, const juce::String& value)
{
    if (auto* sink = control.findParentComponentOfClass<StatusSink>())
        sink->showControlStatus(name, value);
}

// A parameter's name as the status bar should show it: the product's per-slot prefix ("V1 ", "T3 ", "A2 ") is dropped.
inline juce::String statusNameOfParameter(const juce::String& parameterName)
{
    const auto space = parameterName.indexOfChar(' ');
    if (space > 1 && space <= 3 && juce::CharacterFunctions::isLetter(parameterName[0]) && parameterName.substring(1, space).containsOnly("0123456789"))
        return parameterName.substring(space + 1);
    return parameterName;
}

} // namespace mu_ui
