#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "Components/KnobWithLabel.h"
#include "Components/VUMeter.h"
#include "Components/MuClidLookAndFeel.h"
#include "../Audio/MixerEngine.h"

// One vertical channel strip.
// Layout (top→bottom): colour bar | name | [sends] | pan | fader+VU | dB | [mute/solo]
class MixerChannel : public juce::Component
{
public:
    enum class Type { Rhythm, EffectReturn, DelayReturn, ReverbReturn, Master };

    MixerChannel(Type type, const juce::String& name, juce::Colour colour);

    void bindRhythm (MixerEngine::ChannelState& state, juce::Atomic<float>& peak);
    void bindReturn (MixerEngine::ReturnState&  state, juce::Atomic<float>& peak);
    void bindMaster (MixerEngine& engine);

    void resized() override;
    void paint(juce::Graphics&) override;

private:
    using Id = MuClidLookAndFeel::ColourIds;

    Type         channelType;
    juce::String channelName;
    juce::Colour channelColour;

    KnobWithLabel sendEffect { "Effect", Id::knobFxSend };
    KnobWithLabel sendDelay  { "Delay",  Id::knobFxSend };
    KnobWithLabel sendReverb { "Reverb", Id::knobFxSend };
    KnobWithLabel panKnob    { "Pan",    Id::knobPan    };

    juce::Slider      fader;
    VUMeter           vuMeter;
    juce::Label       dbLabel;
    juce::TextButton  muteBtn { "M" };
    juce::TextButton  soloBtn { "S" };

    bool hasSends()   const { return channelType == Type::Rhythm; }
    bool hasMuteSolo() const { return channelType != Type::Master; }

    void updateDbLabel(float level);

    static constexpr int kColourBarH = 3;
    static constexpr int kNameH      = 22;
    static constexpr int kSendH      = 52;   // per send knob — matches pan/other knob height
    static constexpr int kPanH       = 52;
    static constexpr int kDbH        = 14;
    static constexpr int kButtonH    = 22;
    static constexpr int kVUW        = 10;
    static constexpr int kFaderMaxH  = 200;  // cap fader at 50% of natural full-height
};
