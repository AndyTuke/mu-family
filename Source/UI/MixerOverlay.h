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
    DelayRow delayRow;
    FXRow    reverbRow;

    static constexpr int kFXRowH   = 52;
    static constexpr int kDivW     = 4;
    static constexpr int kChanW    = 80;
    static constexpr int kMasterW  = 96;

    void buildRhythmChannels();
    void wireReturns();
    void wireFXRows();
};
