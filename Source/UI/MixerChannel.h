#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "Components/KnobWithLabel.h"
#include "Components/VUMeter.h"
#include "Components/GRMeter.h"
#include "Components/MuClidLookAndFeel.h"
#include "../Audio/MixerEngine.h"
#include "InsertAlgoDefaults.h"

class PluginProcessor;

// One vertical channel strip.
// Layout (top→bottom): colour bar | name | [sidechain] | sends+pan | fader+VU+GR | [outBus] | dB | [mute/solo]
// Master channel: strip occupies left (getWidth()-kInsertPanelW) pixels; insert panel on the right.
class MixerChannel : public juce::Component
{
public:
    enum class Type { Rhythm, EffectReturn, DelayReturn, ReverbReturn, Master };

    // Width of the insert panel drawn to the right of the Master strip.
    // Both inserts share this width, stacked vertically (top = INS 1, bottom = INS 2).
    static constexpr int kInsertPanelW = 130;

    MixerChannel(Type type, const juce::String& name, juce::Colour colour);

    // status-bar coverage for outBus + scSource dropdowns. Colour is the
    // channel's own colour (used as the status-bar rhythm tag for context).
    std::function<void(const juce::String& name,
                       const juce::String& value,
                       juce::Colour channelColour)> onStatusUpdate;

    // Bind to engine state + VU peak + optional GR atomic. Pass proc+prefix to route mutations through APVTS.
    void bindRhythm(MixerEngine::ChannelState& state, std::atomic<float>& peak,
                    PluginProcessor* proc = nullptr, const juce::String& apvtsPrefix = {},
                    std::atomic<float>* grAtomic = nullptr);
    void bindReturn(MixerEngine::ReturnState& state, std::atomic<float>& peak,
                    PluginProcessor* proc = nullptr, const juce::String& apvtsPrefix = {},
                    std::atomic<float>* grAtomic = nullptr);
    void bindMaster(MixerEngine& engine,
                    PluginProcessor* proc = nullptr);

    // Wire intra-FX send knobs for EffectReturn / DelayReturn.
    void bindReturnSends(juce::AudioProcessorValueTreeState& apvts,
                         const juce::String& dlySendParam,
                         const juce::String& revSendParam);

    // Populate the sidechain source dropdown with rhythm channel names.
    // ownChannelIndex is excluded (pass -1 for return channels to include all rhythm channels).
    void setSidechainSources(int ownChannelIndex, const juce::StringArray& channelNames);

    // Public getters for section bounds — used by MixerOverlay to align the row-label panel.
    juce::Rectangle<int> getSidechainPaneBounds() const { return sidechainPaneBounds; }
    juce::Rectangle<int> getSendsPaneBounds()     const { return sendsPaneBounds;     }
    juce::Rectangle<int> getFaderPaneBounds()     const { return faderPaneBounds;     }
    juce::Rectangle<int> getOutBusBounds()        const { return outBusBox.getBounds(); }

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

    // Per-algorithm state snapshots for master inserts — enables A/B-ing between algorithms.
    // Indexed by driveChar (0..13); snapshotValid[i] is false until first visit.
    // struct + default table lifted to shared InsertAlgoDefaults.h.
    using InsertAlgoSnapshot = InsertAlgoDefaults;
    InsertAlgoSnapshot insertSnapshots[14];
    bool               insertSnapshotValid[14] = {};
    InsertAlgoSnapshot insertSnapshots2[14];
    bool               insertSnapshotValid2[14] = {};

    // Insert controls (Master channel only) — two slots stacked vertically.
    // insExtra / insExtra2 only visible in EQ mode.
    juce::ComboBox    insCharBox;
    KnobWithLabel     insDrive  { "Drive",  Id::knobInsertPad };
    KnobWithLabel     insOutput { "Output", Id::knobInsertPad };
    KnobWithLabel     insTone   { "Tone",   Id::knobInsertPad };
    KnobWithLabel     insExtra  { "Mid Hz", Id::knobInsertPad };

    juce::ComboBox    insCharBox2;
    KnobWithLabel     insDrive2  { "Drive",  Id::knobInsertPad };
    KnobWithLabel     insOutput2 { "Output", Id::knobInsertPad };
    KnobWithLabel     insTone2   { "Tone",   Id::knobInsertPad };
    KnobWithLabel     insExtra2  { "Mid Hz", Id::knobInsertPad };

    // Configure knob labels/ranges/callbacks for the selected algorithm on one insert slot.
    // slot=0 → first insert (mst_ins*), slot=1 → second insert (mst_ins2*).
    // proc non-null = write char param to APVTS; null = skip (called from loadFromAPVTS).
    void configureInsertAlgorithm(int charId, int slot, PluginProcessor* proc);

    // Captured by bindMaster so configureInsertAlgorithm's knob callbacks can
    // write to APVTS even when re-invoked from loadFromAPVTS with proc=nullptr.
    PluginProcessor* masterInsertProc = nullptr;

    bool hasSends()            const { return channelType == Type::Rhythm
                                             || channelType == Type::EffectReturn
                                             || channelType == Type::DelayReturn; }
    bool hasMuteSolo()         const { return channelType != Type::Master; }
    bool hasSidechainControls() const { return channelType == Type::Rhythm
                                             || channelType == Type::EffectReturn
                                             || channelType == Type::DelayReturn
                                             || channelType == Type::ReverbReturn; }
    bool hasOutputBus()        const { return channelType == Type::Rhythm; }
    bool hasInsert()           const { return channelType == Type::Master; }

    void updateDbLabel(float level);

    static constexpr int kColourBarH = 3;
    static constexpr int kNameH      = 22;
    static constexpr int kOutBusH    = 18;   // Out dropdown row (Rhythm channels only)
    static constexpr int kDbH        = 14;
    static constexpr int kButtonH    = 22;
    static constexpr int kVUW        = 10;
    static constexpr int kGRW        = 8;   // GR meter width (Rhythm channels only)
    // Sidechain minimum heights — proportional values are computed in resized().
    static constexpr int kScSrcH     = 20;
    static constexpr int kScAmtH     = 44;
    static constexpr int kScEnvH     = 40;
    static constexpr int kSidechainH = kScSrcH + kScAmtH + kScEnvH;  // 104px minimum

    // Insert section (Master channel only)
    static constexpr int kInsCharH  = 20;
    static constexpr int kInsKnobH  = 44;
    static constexpr int kInsertH   = kInsCharH + kInsKnobH;          // 64px

    // Section pane bounds — computed in resized(), drawn as rounded rects in paint().
    juce::Rectangle<int> sidechainPaneBounds;
    juce::Rectangle<int> sendsPaneBounds;
    juce::Rectangle<int> faderPaneBounds;

    // Y coordinate of the horizontal divider between INS 1 and INS 2 (Master only).
    int insertMidY = 0;
};
