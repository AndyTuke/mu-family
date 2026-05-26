#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "MixerChannel.h"
#include "UI/FXAlgoDefaults.h"
#include "FXRow.h"
#include "DelayRow.h"
#include "UI/Components/SegmentControl.h"
#include "Plugin/ProcessorBase.h"
#include "Audio/MixerEngine.h"
#include <atomic>
#include <vector>
#include <memory>

// Full mixer view: rhythm channel strips | Effect/Delay/Reverb returns | Master
// + three FX rows (Effect, Delay, Reverb) below the strips.
// Replaces the RhythmPanel in the editor when the mixer button is toggled.
class MixerOverlay : public juce::Component,
                     public juce::AudioProcessorValueTreeState::Listener,
                     private juce::Timer
{
public:
    explicit MixerOverlay(ProcessorBase& proc, MixerEngine& mixer);
    ~MixerOverlay() override;

    // status-bar forwarder — plugin editor wires this to the global StatusBar.
    std::function<void(const juce::String& name,
                       const juce::String& value,
                       juce::Colour channelColour)> onStatusUpdate;

    // Fired whenever the effect-slot algorithm changes (or initial sync). The
    // editor uses this to push the algorithm's display name into the voice
    // section's Amp "Effect" send knob label.
    std::function<void(const juce::String& algorithmName)> onEffectAlgorithmNameChanged;

    // Rebuild channel strips to match the current rhythm count.
    void refresh();

    // Reload all FX-row and mixer-channel UI from current APVTS values.
    void loadFromAPVTS();

    void resized() override;
    void paint(juce::Graphics&) override;

private:
    ProcessorBase& proc;
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

    static constexpr int kHeaderH    = 22;  // thin strip above channel strips for meter mode selector
    static constexpr int kFXGap     = 6;
    static constexpr int kFXPad     = 6;
    static constexpr int kDivW      = 4;
    static constexpr int kChanGap   = 3;
    static constexpr int kMasterW   = 80;  // strip only; component is kMasterW + MixerChannel::kInsertPanelW
    static constexpr int kLabelPanelW = 38; // narrow panel left of channels with section row labels

    // Cached layout values updated each resized() call — read by paint().
    int lastChanW   = 64;
    int lastDivX1   = 0;
    int lastDivX2   = 0;
    int lastFXAreaH = 278;
    int lastFXRowH  = 82;
    int lastStripH  = 400;

    SegmentControl meterModeCtrl { {"Peak", "VU", "K-12", "K-14"} };

    void propagateMeterMode(VUMeter::MeterMode m);

    void buildRhythmChannels();
    void wireReturns();
    void wireFXRows();
    void updateEffectSendLabels();
    void refreshSidechainSources();

    // juce::AudioProcessorValueTreeState::Listener — sets dirty flag for deferred reload.
    void parameterChanged(const juce::String& parameterID, float newValue) override;
    void visibilityChanged() override;
    void timerCallback() override;

    // Set by parameterChanged (any thread, including audio thread under host
    // automation) and consumed by timerCallback / visibilityChanged on the
    // message thread. Atomic + test-and-clear via exchange() so a write between
    // the read and the clear is never silently dropped.
    std::atomic<bool> apvtsDirty { false };

    // Per-algorithm snapshots for the effect row (A/B-style recall on algo switch).
    // Indexed by eff_algo (0..3). snapValid[i] is false until first departure from algo i.
    EffectAlgoDefaults effSnaps[4];
    bool               effSnapValid[4] = {};
};
