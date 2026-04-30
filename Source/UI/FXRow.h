#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../FX/FXAlgorithmDef.h"
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

    void resized() override;
    void paint(juce::Graphics& g) override;

private:
    void rebuildKnobs(int algorithmIndex);

    juce::String slotName;
    std::vector<FXAlgorithmDef> algorithmDefs;
    MuClidLookAndFeel::ColourIds knobColour;

    juce::TextButton             enableButton{ "On" };
    DropdownSelect               algorithmDropdown;
    std::vector<std::unique_ptr<KnobWithLabel>> knobs;

    int currentAlgorithm = 0;
    bool isEnabled       = true;

    static constexpr int kToggleW   = 36;
    static constexpr int kNameW     = 60;
    static constexpr int kDropdownW = 120;
    static constexpr int kKnobW     = 64;
    static constexpr int kPadding   = 4;
};
