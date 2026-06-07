#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "Plugin/ProcessorBase.h"
#include "Plugin/MuOnChannels.h"

#include <memory>
#include <vector>

// EnginePanel — the controls for the currently-selected instrument lane. On setChannel()
// it rebuilds a row of knobs (and a combo for choice params) bound to that engine's APVTS
// params via attachments. The shell + mixer handle everything else, so this is purely the
// product engine surface.
namespace mu_on
{

class EnginePanel : public juce::Component
{
public:
    explicit EnginePanel(ProcessorBase& processor);

    void setChannel(int idx);
    int  getChannel() const noexcept { return currentChannel; }

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    struct Control
    {
        juce::String label;
        std::unique_ptr<juce::Slider>   knob;
        std::unique_ptr<juce::ComboBox> combo;
        juce::Label                     name;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>   knobAtt;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> comboAtt;
    };

    void rebuild();

    ProcessorBase& proc;
    int currentChannel = 0;
    std::vector<std::unique_ptr<Control>> controls;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EnginePanel)
};

} // namespace mu_on
