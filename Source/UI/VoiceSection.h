#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "Components/KnobWithLabel.h"
#include "Components/SegmentControl.h"
#include "Components/MuClidLookAndFeel.h"

// Compact single-row voice controls: amp envelope, filter, filter envelope, output mode.
// All in one horizontal strip. Does not connect to audio engine yet (Stage 10 wires APVTS).
class VoiceSection : public juce::Component
{
public:
    VoiceSection();

    std::function<void(const juce::String& name, const juce::String& value)> onStatusUpdate;

    void resized() override;
    void paint(juce::Graphics&) override;

private:
    using Id = MuClidLookAndFeel::ColourIds;

    // Amp envelope (amber)
    KnobWithLabel ampAttack  { "ATK", Id::knobLevel };
    KnobWithLabel ampDecay   { "DEC", Id::knobLevel };
    KnobWithLabel ampSustain { "SUS", Id::knobLevel };
    KnobWithLabel ampRelease { "REL", Id::knobLevel };

    // Filter (teal)
    KnobWithLabel filterCutoff { "CUT", Id::knobPadding };
    KnobWithLabel filterRes    { "RES", Id::knobPadding };

    // Filter envelope (teal)
    KnobWithLabel fEnvAttack { "ATK", Id::knobPadding };
    KnobWithLabel fEnvDecay  { "DEC", Id::knobPadding };
    KnobWithLabel fEnvDepth  { "DEP", Id::knobPadding };

    // Output mode
    SegmentControl outputMode { {"SMPL", "MIDI"} };

    void wireStatusCallbacks();
};
