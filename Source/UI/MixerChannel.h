#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "Components/KnobWithLabel.h"
#include "Components/VUMeter.h"
#include "Components/MuClidLookAndFeel.h"
#include "../Audio/MixerEngine.h"

class PluginProcessor;

// One vertical channel strip.
// Layout (top→bottom): colour bar | name | [sends] | pan | fader+VU | dB | [mute/solo]
class MixerChannel : public juce::Component
{
public:
    enum class Type { Rhythm, EffectReturn, DelayReturn, ReverbReturn, Master };

    MixerChannel(Type type, const juce::String& name, juce::Colour colour);

    // Bind to engine state + VU peak. Pass proc+prefix to route mutations through APVTS.
    void bindRhythm(MixerEngine::ChannelState& state, juce::Atomic<float>& peak,
                    PluginProcessor* proc = nullptr, const juce::String& apvtsPrefix = {});
    void bindReturn(MixerEngine::ReturnState& state, juce::Atomic<float>& peak,
                    PluginProcessor* proc = nullptr, const juce::String& apvtsPrefix = {});
    void bindMaster(MixerEngine& engine,
                    PluginProcessor* proc = nullptr);

    // Wire intra-FX send knobs for EffectReturn / DelayReturn.
    // dlySendParam / revSendParam are APVTS IDs; pass empty to skip that send.
    void bindReturnSends(juce::AudioProcessorValueTreeState& apvts,
                         const juce::String& dlySendParam,
                         const juce::String& revSendParam);

    // Reload UI from APVTS after external state change (e.g. preset load).
    void loadFromAPVTS(juce::AudioProcessorValueTreeState& apvts,
                       const juce::String& prefix);

    void setEffectSendLabel(const juce::String& name);
    void setActive(bool a) { active = a; repaint(); }

    void resized() override;
    void paint(juce::Graphics&) override;

private:
    using Id = MuClidLookAndFeel::ColourIds;

    Type         channelType;
    juce::String channelName;
    juce::Colour channelColour;
    bool         active = true;

    KnobWithLabel sendEffect { "Effect", Id::knobFxSend };
    KnobWithLabel sendDelay  { "Delay",  Id::knobFxSend };
    KnobWithLabel sendReverb { "Reverb", Id::knobFxSend };
    KnobWithLabel panKnob    { "Pan",    Id::knobPan    };

    juce::Slider      fader;
    VUMeter           vuMeter;
    juce::Label       dbLabel;
    juce::TextButton  muteBtn { "M" };
    juce::TextButton  soloBtn { "S" };

    bool hasSends()    const { return channelType == Type::Rhythm
                                       || channelType == Type::EffectReturn
                                       || channelType == Type::DelayReturn; }
    bool hasMuteSolo() const { return channelType != Type::Master; }

    void updateDbLabel(float level);

    static constexpr int kColourBarH = 3;
    static constexpr int kNameH      = 22;
    static constexpr int kSendH      = 52;
    static constexpr int kPanH       = 52;
    static constexpr int kTopAreaH   = kSendH * 3 + kPanH;  // fixed for all channel types
    static constexpr int kDbH        = 14;
    static constexpr int kButtonH    = 22;
    static constexpr int kVUW        = 10;
};
