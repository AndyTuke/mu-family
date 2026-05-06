#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "Components/KnobWithLabel.h"
#include "Components/VUMeter.h"
#include "Components/MuClidLookAndFeel.h"
#include "../Audio/MixerEngine.h"

class PluginProcessor;

// One vertical channel strip.
// Layout (top→bottom): colour bar | name | [sidechain section] | [sends] | pan | fader+VU | dB | [mute/solo]
// Sidechain section only visible on Rhythm channels.
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
    void bindReturnSends(juce::AudioProcessorValueTreeState& apvts,
                         const juce::String& dlySendParam,
                         const juce::String& revSendParam);

    // Populate the sidechain source dropdown with other active channel names.
    // ownChannelIndex is excluded from the list. Call whenever rhythm count/names change.
    void setSidechainSources(int ownChannelIndex, const juce::StringArray& channelNames);

    // Reload UI from APVTS after external state change (e.g. preset load).
    void loadFromAPVTS(juce::AudioProcessorValueTreeState& apvts,
                       const juce::String& prefix);

    void setEffectSendLabel(const juce::String& name);
    void setChannelName(const juce::String& n) { channelName = n; repaint(); }
    void setActive(bool a) { active = a; repaint(); }
    void setMeterMode(VUMeter::MeterMode m) { vuMeter.setMode(m); }

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

    // Sidechain controls (Rhythm channels only)
    juce::ComboBox    scSourceBox;
    KnobWithLabel     scAmount  { "Amt", Id::knobFxSend };
    KnobWithLabel     scAttack  { "/",   Id::knobFxSend };
    KnobWithLabel     scRelease { "\\",  Id::knobFxSend };

    bool hasSends()    const { return channelType == Type::Rhythm
                                       || channelType == Type::EffectReturn
                                       || channelType == Type::DelayReturn; }
    bool hasMuteSolo() const { return channelType != Type::Master; }
    bool hasSidechain() const { return channelType == Type::Rhythm; }

    void updateDbLabel(float level);

    static constexpr int kColourBarH = 3;
    static constexpr int kNameH      = 22;
    static constexpr int kSendH      = 44;   // 15% smaller than original 52
    static constexpr int kPanH       = 52;
    static constexpr int kTopAreaH   = kSendH * 3 + kPanH;  // fixed for all channel types
    static constexpr int kDbH        = 14;
    static constexpr int kButtonH    = 22;
    static constexpr int kVUW        = 10;
    // Sidechain section heights (Rhythm only)
    static constexpr int kScSrcH     = 20;
    static constexpr int kScAmtH     = 44;
    static constexpr int kScEnvH     = 40;
    static constexpr int kSidechainH = kScSrcH + kScAmtH + kScEnvH;  // 104px
};
