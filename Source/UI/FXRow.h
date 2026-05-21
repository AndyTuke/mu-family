#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../Audio/FX/Slots/FXAlgorithmDef.h"
#include "Components/MuClidLookAndFeel.h"
#include "Components/KnobWithLabel.h"
#include "Components/DropdownSelect.h"

// One horizontal row in the Mixer Overlay FX section.
// Layout: [on/off] [name] [algorithm dropdown] [param knobs…]
// FXRow is a pure UI component — the parent (MixerOverlay, Stage 9) wires it
// to the actual FXSlotBase via the callbacks below.
class FXRow : public juce::Component
{
public:
    std::function<void(bool)>                        onEnabledChanged;
    std::function<void(int algorithmIndex)>          onAlgorithmChanged;
    std::function<void(const juce::String&, float)>  onParamChanged;
    std::function<void(const juce::String&, const juce::String&)> onStatusUpdate;

    FXRow(const juce::String& slotName,
          const std::vector<FXAlgorithmDef>& algorithms,
          MuClidLookAndFeel::ColourIds knobColour = MuClidLookAndFeel::knobFxSend);

    void setEnabled(bool enabled, juce::NotificationType n = juce::dontSendNotification);
    void setSelectedAlgorithm(int index, juce::NotificationType n = juce::dontSendNotification);
    void setParamValue(const juce::String& id, float value);

    // Hide a parameter from the UI (e.g. "mix" when this row drives a send/return slot).
    // Call before setSelectedAlgorithm() / when the row is first wired up.
    void hideParameter(const juce::String& id);

    // Show/hide the parameter knobs while keeping the On button + name + dropdown visible.
    // Used when Echo mode is active so the algo dropdown remains accessible.
    void setKnobsVisible(bool visible);

    void resized() override;
    void paint(juce::Graphics& g) override;

    // Width of the fixed header area (On + name + dropdown) = 4+36+4+60+4+120+4 = 232.
    // Used by MixerOverlay to position echoRow after the header when Echo mode is active.
    static constexpr int kHeaderWidth = 232;

private:
    void rebuildKnobs(int algorithmIndex);

    juce::String slotName;
    std::vector<FXAlgorithmDef> algorithmDefs;
    MuClidLookAndFeel::ColourIds knobColour;

    juce::TextButton             enableButton{ "On" };
    DropdownSelect               algorithmDropdown;
    std::vector<std::unique_ptr<KnobWithLabel>> knobs;

    int  currentAlgorithm = 0;
    bool isEnabled        = true;
    bool knobsVisible     = true;
    juce::StringArray hiddenParamIds;

    static constexpr int kToggleW   = 36;
    static constexpr int kNameW     = 60;
    static constexpr int kDropdownW = 120;
    static constexpr int kPadding   = 4;
    // Knob width is computed at resize time via MuLookAndFeel::knobSizeLargeFor
    // so it tracks the plugin window. Use that helper rather than a constant
    // here — see FXRow::resized().
};
