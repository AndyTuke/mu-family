#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "UI/Components/KnobWithLabel.h"
#include "UI/Components/VUMeter.h"
#include "UI/Components/GRMeter.h"
#include "UI/Components/MuLookAndFeel.h"
#include "Audio/MixerEngine.h"
#include "Audio/InsertSlotConfig.h"

class ProcessorBase;

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
                    ProcessorBase* proc = nullptr, const juce::String& apvtsPrefix = {},
                    std::atomic<float>* grAtomic = nullptr);
    void bindReturn(MixerEngine::ReturnState& state, std::atomic<float>& peak,
                    ProcessorBase* proc = nullptr, const juce::String& apvtsPrefix = {},
                    std::atomic<float>* grAtomic = nullptr);
    void bindMaster(MixerEngine& engine,
                    ProcessorBase* proc = nullptr);

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
    using Id = MuLookAndFeel::ColourIds;

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

    // Per-algorithm slot snapshots for master inserts — A/B-style: cycling
    // back to a previously-edited algo restores its slot values. Stored as
    // ACTUAL (denormalised) per slot so the restore path normalises back via
    // mu_ui::actualToNorm. 4 slots per algo, 14 algos, 2 master slots.
    float insertSnapshots     [14][4] = {{0.0f}};
    bool  insertSnapshotValid [14]    = {};
    float insertSnapshots2    [14][4] = {{0.0f}};
    bool  insertSnapshotValid2[14]    = {};

    // Master insert: 4 generic Param knobs per slot. Labels / ranges /
    // formatters are driven each algo switch by mu_ui::configureKnobFromSlot.
    juce::ComboBox    insCharBox;
    KnobWithLabel     insParam1  { "P1", Id::knobInsertPad };
    KnobWithLabel     insParam2  { "P2", Id::knobInsertPad };
    KnobWithLabel     insParam3  { "P3", Id::knobInsertPad };
    KnobWithLabel     insParam4  { "P4", Id::knobInsertPad };

    juce::ComboBox    insCharBox2;
    KnobWithLabel     insParam1_2 { "P1", Id::knobInsertPad };
    KnobWithLabel     insParam2_2 { "P2", Id::knobInsertPad };
    KnobWithLabel     insParam3_2 { "P3", Id::knobInsertPad };
    KnobWithLabel     insParam4_2 { "P4", Id::knobInsertPad };
    KnobWithLabel     insExtra2  { "Mid Hz", Id::knobInsertPad };

    // Configure knob labels/ranges/callbacks for the selected algorithm on one insert slot.
    // slot=0 → first insert (mst_ins*), slot=1 → second insert (mst_ins2*).
    // proc non-null = write char param to APVTS; null = skip (called from loadFromAPVTS).
    void configureInsertAlgorithm(int charId, int slot, ProcessorBase* proc);

    // Captured by bindMaster so configureInsertAlgorithm's knob callbacks can
    // write to APVTS even when re-invoked from loadFromAPVTS with proc=nullptr.
    ProcessorBase* masterInsertProc = nullptr;

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

    // Padding around the rhythm-name pill — used as the inset on all four
    // sides of the kNameH-tall name area, so the rhythm-colour border has
    // breathing room from the strip's outer edges and from the section below.
    // Single source of truth — adjust here to change the spacing for every
    // channel strip in lockstep.
    static constexpr int kNamePadding = 4;
    static constexpr int kNameH       = 22;
    static constexpr int kOutBusH    = 18;   // Out dropdown row (Rhythm channels only)
    static constexpr int kDbH        = 14;
    static constexpr int kButtonH    = 22;
    static constexpr int kVUW        = 10;
    static constexpr int kGRW        = 8;   // GR meter width (Rhythm channels only)
    // Sidechain section heights at Medium baseline. Previously these were the
    // floor on a proportional `h * 0.20f` calculation; now the section is
    // fixed at the values the calculation produced at the default strip
    // height (531 px) so the layout no longer depends on the parent.
    static constexpr int kScSrcH       = 20;
    static constexpr int kScAmtH       = 47;   // was max(44, scRemain*0.55) → 47 at default
    static constexpr int kScEnvH       = 39;   // was scH - kScSrcH - kScAmtH → 39 at default
    static constexpr int kSidechainH   = 106;  // was max(104, h*0.20) → 106 at default

    // Sends + pan area at Medium baseline. Previously the area was
    // max(144, h*0.35) of strip height, divided 4-up. Now fixed.
    static constexpr int kSendsAreaH   = 186;  // was max(144, h*0.35) → 186 at default
    static constexpr int kSendKnobH    = 46;   // was kSendsAreaH / 4
    static constexpr int kPanKnobH     = 48;   // was kSendsAreaH - 3 * kSendKnobH

    // Insert section (Master channel only)
    static constexpr int kInsCharH      = 20;
    static constexpr int kInsKnobH      = 44;
    static constexpr int kInsertH       = kInsCharH + kInsKnobH;          // 64px
    static constexpr int kInsertLabelW  = 18;   // rotated "Main Insert N" label strip on the left of each slot

    // Section pane bounds — computed in resized(), drawn as rounded rects in paint().
    juce::Rectangle<int> sidechainPaneBounds;
    juce::Rectangle<int> sendsPaneBounds;
    juce::Rectangle<int> faderPaneBounds;

    // Y coordinate of the horizontal divider between INS 1 and INS 2 (Master only).
    int insertMidY = 0;
};
