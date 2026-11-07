#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Plugin/PluginProcessor.h"
#include "UI/TransportBar.h"
#include "UI/Components/StatusBar.h"
#include "UI/Components/MuLookAndFeel.h"
#include "UI/Components/DropdownSelect.h"
#include "UI/Components/KnobWithLabel.h"
#include "UI/AboutOverlay.h"
#include "UI/UpdateNotice.h"

#include <memory>
#include <vector>

namespace mu_toni_midi
{

// The mu-Toni MIDI editor: the transport bar over one panel holding the arp's controls (dropdowns for the choices, knobs for the
// amounts, two switches) and a MIDI row (In / Out channel, Velocity). No sidebar, voice section or mixer: it makes no sound.
// Every control reads and writes its a0_* / MIDI parameter directly; a 30 Hz timer pulls host automation back into the controls.
class PluginEditor : public juce::AudioProcessorEditor,
                     private juce::Timer
{
public:
    explicit PluginEditor(PluginProcessor&);
    ~PluginEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    struct Choice  { juce::String id; juce::String label; DropdownSelect box; juce::Label caption; int firstValue = 0; };
    struct Amount  { juce::String id; std::unique_ptr<KnobWithLabel> knob; };
    struct Switch  { juce::String id; juce::ToggleButton button; std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> attach; };

    void addChoice(const juce::String& id, const juce::String& label, const juce::StringArray& items, int firstValue);
    void addAmount(const juce::String& id, const juce::String& label, double lo, double hi, double step, MuLookAndFeel::ColourIds colourId);
    void addSwitch(const juce::String& id, const juce::String& label);
    void timerCallback() override;
    float valueOf(const juce::String& id) const;
    void  writeValue(const juce::String& id, float value);

    PluginProcessor& proc;
    MuLookAndFeel lookAndFeel;
    TransportBar  transportBar;
    StatusBar     statusBar;
    mu_ui::UpdateNotice updateNotice { proc, statusBar };
    AboutOverlay  aboutOverlay;
    DropdownSelect sizeDropdown;

    std::vector<std::unique_ptr<Choice>> choices;
    std::vector<std::unique_ptr<Amount>> amounts;
    std::vector<std::unique_ptr<Switch>> switches;

    static constexpr int kWindowW = 980;
    static constexpr int kWindowH = 330;
    static constexpr int kTransportH = 40;
    static constexpr int kStatusH = 22;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginEditor)
};

} // namespace mu_toni_midi
