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
    KnobWithLabel ampAttack  { "Attack",  Id::knobLevel   };
    KnobWithLabel ampDecay   { "Decay",   Id::knobLevel   };
    KnobWithLabel ampSustain { "Sustain", Id::knobLevel   };
    KnobWithLabel ampRelease { "Release", Id::knobLevel   };

    // Filter (teal)
    KnobWithLabel filterCutoff { "Cutoff",    Id::knobPostPad };
    KnobWithLabel filterRes    { "Resonance", Id::knobPostPad };

    // Filter envelope (teal)
    KnobWithLabel fEnvAttack { "Attack", Id::knobPostPad };
    KnobWithLabel fEnvDecay  { "Decay",  Id::knobPostPad };
    KnobWithLabel fEnvDepth  { "Depth",  Id::knobPostPad };

    // Output mode
    SegmentControl outputMode { {"Sample", "MIDI"} };

    void wireStatusCallbacks();
};
