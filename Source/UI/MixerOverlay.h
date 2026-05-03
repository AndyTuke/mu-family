#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "MixerChannel.h"
#include "FXRow.h"
#include "DelayRow.h"
#include "../PluginProcessor.h"
#include "../Audio/MixerEngine.h"
#include <vector>
#include <memory>

// Full mixer view: rhythm channel strips | Effect/Delay/Reverb returns | Master
// + three FX rows (Effect, Delay, Reverb) below the strips.
// Replaces the RhythmPanel in the editor when the mixer button is toggled.
class MixerOverlay : public juce::Component
{
public:
    explicit MixerOverlay(PluginProcessor& proc, MixerEngine& mixer);

    // Rebuild channel strips to match the current rhythm count.
    void refresh();

    // Reload all FX-row and mixer-channel UI from current APVTS values.
    void loadFromAPVTS();

    void resized() override;
    void paint(juce::Graphics&) override;

private:
    PluginProcessor& proc;
    MixerEngine&     mixer;

    std::vector<std::unique_ptr<MixerChannel>> rhythmChannels;

    MixerChannel effectReturn  { MixerChannel::Type::EffectReturn, "Effect",  juce::Colour(0xffD85A30) };
    MixerChannel delayReturn   { MixerChannel::Type::DelayReturn,  "Delay",   juce::Colour(0xffD85A30) };
    MixerChannel reverbReturn  { MixerChannel::Type::ReverbReturn, "Reverb",  juce::Colour(0xff378ADD) };
    MixerChannel masterChannel { MixerChannel::Type::Master,       "Master",  juce::Colours::white    };

    FXRow    effectRow;
    DelayRow echoRow;    // shown when effectRow algo == Echo
    DelayRow delayRow;
    FXRow    reverbRow;

    static constexpr int kFXRowH  = 82;
    static constexpr int kFXGap   = 6;    // gap between FX sub-panels
    static constexpr int kFXAreaH = kFXRowH * 3 + kFXGap * 2;  // 3-row base (no echo)
    static constexpr int kDivW     = 4;
    static constexpr int kChanW    = 64;
    static constexpr int kMasterW  = 96;

    void buildRhythmChannels();
    void wireReturns();
    void wireFXRows();
    void updateEffectSendLabels();
};
