#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "Components/MuClidLookAndFeel.h"
#include "Components/NudgeInput.h"
#include "Components/KnobWithLabel.h"

class PluginProcessor;

// Settings panel overlay.  Appears in place of the main area.
// Active settings write to ApplicationProperties; the rest are placeholders.
class SettingsOverlay : public juce::Component
{
public:
    std::function<void()> onClose;

    explicit SettingsOverlay(PluginProcessor& proc);

    void resized() override;
    void paint(juce::Graphics& g) override;

private:
    PluginProcessor& proc;
    const bool isStandalone;

    juce::TextButton closeBtn { "Close" };

    // Active: standalone BPM default
    NudgeInput defaultBpmInput { "Default BPM", 20, 300, 120 };

    // Active: master volume knob (reads/writes from APVTS)
    KnobWithLabel masterVolKnob { "Master Vol", MuClidLookAndFeel::knobLevel };

    static constexpr int kHeaderH = 36;
    static constexpr int kPad     = 12;
    static constexpr int kRowH    = 40;
};
