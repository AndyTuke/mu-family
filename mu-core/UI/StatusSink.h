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

// ── Switches: every juce::Button reports by itself ────────────────────────────────────────────────────────
//
// A button needs no wiring. The shell finds every button under it (bindButtonsIn, run from the shell's refresh timer) and gives it a
// reporter, so a button added tomorrow in any product reports the same way. Its status name is, in order: the "muStatusName" component
// property (setStatusName below), its text if that has two or more letters, its tooltip, its component name. A button with none of
// those cannot report, and the ui-audit lists it: give it a name.
inline constexpr const char* kStatusNameProperty  = "muStatusName";
inline constexpr const char* kStatusBoundProperty = "muStatusBound";

// Names any control for the status bar (the one common property the bar reads); a button's own text/tooltip is the fallback.
inline void setStatusName(juce::Component& control, const juce::String& name) { control.getProperties().set(kStatusNameProperty, name); }

// A component that takes mouse clicks but is not a control (a display, a meter) says so, with the reason, so the ui-audit leaves it
// out. Anything the audit finds that neither reports nor says this is a defect.
inline constexpr const char* kStatusExemptProperty = "muStatusExempt";
inline void exemptFromStatus(juce::Component& display, const juce::String& reason) { display.getProperties().set(kStatusExemptProperty, reason); }

inline juce::String statusNameOf(juce::Component& control)
{
    auto hasWord = [](const juce::String& s) { int letters = 0; for (auto c : s) letters += juce::CharacterFunctions::isLetter(c) ? 1 : 0; return letters >= 2; };

    if (const auto named = control.getProperties()[kStatusNameProperty].toString(); named.isNotEmpty())
        return named;
    if (auto* button = dynamic_cast<juce::Button*>(&control))
    {
        if (hasWord(button->getButtonText())) return button->getButtonText();
        if (hasWord(button->getTooltip()))    return button->getTooltip();
        if (hasWord(button->getName()))       return button->getName();
    }
    return {};
}

// What a click on `button` should show as its value: On / Off for a switch, Selected for a radio choice, Clicked for an action.
inline juce::String statusValueOf(const juce::Button& button)
{
    if (button.getRadioGroupId() != 0) return button.getToggleState() ? "Selected" : "Off";
    if (button.getClickingTogglesState()) return button.getToggleState() ? "On" : "Off";
    return "Clicked";
}

// Listens to one button and reports each click to the nearest StatusSink; deletes itself with the button.
class ButtonStatusReporter final : private juce::Button::Listener, private juce::ComponentListener
{
public:
    explicit ButtonStatusReporter(juce::Button& b) : button(b) { button.addListener(this); button.addComponentListener(this); }

private:
    void buttonClicked(juce::Button* b) override
    {
        const auto name = statusNameOf(*b);
        if (name.isNotEmpty()) reportStatus(*b, name, statusValueOf(*b));
    }
    void componentBeingDeleted(juce::Component&) override { delete this; }

    juce::Button& button;
};

// Gives every button under `root` a reporter (once each). Cheap: a walk over the child pointers.
inline void bindButtonsIn(juce::Component& root)
{
    for (auto* child : root.getChildren())
    {
        if (auto* button = dynamic_cast<juce::Button*>(child))
            if (! button->getProperties().contains(kStatusBoundProperty))
            {
                button->getProperties().set(kStatusBoundProperty, true);
                new ButtonStatusReporter(*button);
            }
        bindButtonsIn(*child);
    }
}

} // namespace mu_ui
