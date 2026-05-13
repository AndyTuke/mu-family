#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "Components/KnobWithLabel.h"
#include "Components/VUMeter.h"
#include "Components/GRMeter.h"
#include "Components/MuClidLookAndFeel.h"
#include "../Audio/MixerEngine.h"

class PluginProcessor;

// One vertical channel strip.
// Layout (top→bottom): colour bar | name | [sidechain] | sends+pan | fader+VU+GR | [outBus] | dB | [mute/solo]
// Master channel: strip occupies left (getWidth()-kInsertPanelW) pixels; insert panel on the right.
class MixerChannel : public juce::Component
{
public:
    enum class Type { Rhythm, EffectReturn, DelayReturn, ReverbReturn, Master };

    // Width of the insert panel drawn to the right of the Master strip.
    // MixerOverlay adds this to kMasterW when sizing masterChannel.
    static constexpr int kInsertPanelW = 130;

    MixerChannel(Type type, const juce::String& name, juce::Colour colour);

    // Bind to engine state + VU peak + optional GR atomic. Pass proc+prefix to route mutations through APVTS.
    void bindRhythm(MixerEngine::ChannelState& state, juce::Atomic<float>& peak,
                    PluginProcessor* proc = nullptr, const juce::String& apvtsPrefix = {},
                    juce::Atomic<float>* grAtomic = nullptr);
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
    GRMeter           grMeter;
    juce::Label       dbLabel;
    juce::TextButton  muteBtn { "M" };
    juce::TextButton  soloBtn { "S" };

    // Output bus dropdown (Rhythm channels only): M = Master, 1..8 = direct out.
    juce::ComboBox    outBusBox;

    // Sidechain controls (Rhythm channels only)
    juce::ComboBox    scSourceBox;
    KnobWithLabel     scAmount  { "Amt", Id::knobFxSend };
    KnobWithLabel     scAttack  { "/",   Id::knobFxSend };
    KnobWithLabel     scRelease { "\\",  Id::knobFxSend };

    // Insert controls (Master channel only) — 4-knob strip (insExtra only shown in EQ mode)
    juce::ComboBox    insCharBox;
    KnobWithLabel     insDrive  { "Drive",  Id::knobInsertPad };
    KnobWithLabel     insOutput { "Output", Id::knobInsertPad };
    KnobWithLabel     insTone   { "Tone",   Id::knobInsertPad };
    KnobWithLabel     insExtra  { "Mid Hz", Id::knobInsertPad };  // #248: EQ mid frequency

    // Configure knob labels/ranges/callbacks for the selected algorithm.
    // proc is used only to decide whether to write `mst_insChar` back to APVTS
    // (non-null = write, null = skip — used when called from loadFromAPVTS to
    // avoid feedback loops). The knob `onValueChanged` lambdas always use
    // `masterInsertProc` (the proc captured by bindMaster) so they keep
    // working after a reload — #243.
    void configureInsertAlgorithm(int charId, PluginProcessor* proc);

    // Captured by bindMaster so configureInsertAlgorithm's knob callbacks can
    // write to APVTS even when re-invoked from loadFromAPVTS with proc=nullptr.
    PluginProcessor* masterInsertProc = nullptr;

    bool hasSends()    const { return channelType == Type::Rhythm
                                       || channelType == Type::EffectReturn
                                       || channelType == Type::DelayReturn; }
    bool hasMuteSolo() const { return channelType != Type::Master; }
    bool hasSidechain() const { return channelType == Type::Rhythm; }
    bool hasInsert()    const { return channelType == Type::Master; }

    void updateDbLabel(float level);

    static constexpr int kColourBarH = 3;
    static constexpr int kNameH      = 22;
    static constexpr int kOutBusH    = 18;   // Out dropdown row (Rhythm channels only)
    static constexpr int kSendH      = 44;   // 15% smaller than original 52
    static constexpr int kPanH       = 52;
    static constexpr int kTopAreaH   = kSendH * 3 + kPanH;  // fixed for all channel types
    static constexpr int kDbH        = 14;
    static constexpr int kButtonH    = 22;
    static constexpr int kVUW        = 10;
    static constexpr int kGRW        = 8;   // GR meter width (Rhythm channels only)
    // Sidechain section heights (Rhythm only)
    static constexpr int kScSrcH     = 20;
    static constexpr int kScAmtH     = 44;
    static constexpr int kScEnvH     = 40;
    static constexpr int kSidechainH = kScSrcH + kScAmtH + kScEnvH;  // 104px

    // Insert section (Master channel only)
    static constexpr int kInsCharH  = 20;
    static constexpr int kInsKnobH  = 44;
    static constexpr int kInsertH   = kInsCharH + kInsKnobH;          // 64px

    // Section pane bounds — computed in resized(), drawn as rounded rects in paint().
    juce::Rectangle<int> sidechainPaneBounds;
    juce::Rectangle<int> sendsPaneBounds;
    juce::Rectangle<int> faderPaneBounds;
};
