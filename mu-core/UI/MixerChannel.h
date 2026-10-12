#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <array>
#include <atomic>
#include <functional>
#include "UI/Components/KnobWithLabel.h"
#include "UI/Components/VUMeter.h"
#include "UI/Components/GRMeter.h"
#include "UI/Components/MuLookAndFeel.h"
#include "Audio/MixerEngine.h"
#include "Audio/InsertSlotConfig.h"

class ProcessorBase;

// One vertical channel strip.
// Layout (top→bottom): colour bar | name | [sidechain] | sends+pan | fader+VU+GR | [outBus] | dB | [mute/solo]
// Master channel: the strip alone; its insert column is mu_ui::MasterInsertPanel, placed beside it by the owner.
class MixerChannel : public juce::Component
{
public:
    // A per-layer channel strip (Channel) vs the FX-return / master strips. The
    // product's layer is a rhythm in mu-clid, a voice in mu-tant.
    // Client is an external-app strip (mu-link): name LCD, four-knob EQ, fader, no sends or sidechain.
    enum class Type { Channel, EffectReturn, DelayReturn, ReverbReturn, Master, Client };

    MixerChannel(Type type, const juce::String& name, juce::Colour colour);

    // status-bar coverage for outBus + scSource dropdowns. Colour is the
    // channel's own colour (used as the status-bar rhythm tag for context).
    std::function<void(const juce::String& name,
                       const juce::String& value,
                       juce::Colour channelColour)> onStatusUpdate;

    // Bind to engine state + VU peak + optional GR atomic. Pass proc+prefix to route mutations through APVTS.
    void bindChannel(MixerEngine::ChannelState& state, std::atomic<float>& peak,
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

    // Update the intra-FX send knob display values directly. Used by MixerOverlay::loadFromApvts
    // to refresh eff2dly / eff2rev / dly2rev into the return strips; those params live outside
    // the ret_*_ prefix scheme and can't be found by the usual prefix-based reload.
    void setDelaySendValue(float v) { sendDelay .setValue(v, juce::dontSendNotification); }
    void setRevSendValue  (float v) { sendReverb.setValue(v, juce::dontSendNotification); }

    // Public getters for section bounds — used by MixerOverlay to align the row-label panel.
    juce::Rectangle<int> getSidechainPaneBounds() const { return sidechainPaneBounds; }
    juce::Rectangle<int> getSendsPaneBounds()     const { return sendsPaneBounds;     }
    juce::Rectangle<int> getFaderPaneBounds()     const { return faderPaneBounds;     }
    juce::Rectangle<int> getOutBusBounds()        const { return outBusBox.getBounds(); }

    // Reload UI from APVTS after external state change (e.g. preset load).
    void loadFromApvts(juce::AudioProcessorValueTreeState& apvts,
                       const juce::String& prefix);

    void setEffectSendLabel(const juce::String& name);
    void setChannelName(const juce::String& n) { channelName = n; repaint(); }
    void setChannelColour(juce::Colour c) { channelColour = c; repaint(); }
    void setActive(bool a) { active = a; repaint(); }

    // ─── Client strip (Type::Client only) ───────────────────────────────────
    // The strip is pure UI: the owner wires the callbacks and pushes state in with the setters
    // (which never fire a callback). An empty name shows the ghost "-" of an empty slot.
    enum class ClientEq { High, MidHz, Mid, Low };   // top to bottom, desk order
    std::function<void(float linearLevel)>       onClientLevel;   // 0 .. kClientFaderMax, unity 1
    std::function<void(ClientEq, float actual)>  onClientEq;      // dB, or Hz for MidHz
    std::function<void(bool muted)>              onClientMute;
    std::function<void(bool soloed)>             onClientSolo;

    // Slot position 0..7: the name LCD is lit in that slot's voice colour.
    void setClientSlot(int slotIndex);
    void setClientLevel(float linear);
    void setClientEq(ClientEq band, float actual);
    void setClientMuteSolo(bool muted, bool soloed);
    // The meter reads this peak (0..1 linear per block), like the other strips.
    void bindClientMeter(const std::atomic<float>& peak);
    KnobWithLabel& getClientEqKnob(ClientEq band) { return clientEq[(size_t) band]; }
    // Fader readout: one decimal, no unit ("0.0", "-6.0", "+3.5", "-inf").
    static juce::String formatClientDb(float linearLevel);
    void setMeterMode(VUMeter::MeterMode m) { vuMeter.setMode(m); }

    // ─── Callback-driven Master (Type::Master only; no processor) ───────────
    // mu-link's master column: the 80 px strip alone (name, fader, VU, dB readout; no pan, mute or
    // solo). The owner wires onMasterLevel and pushes state in with setMasterLevel, which never
    // fires it. The plugins' mixer uses bindMaster instead; both put a MasterInsertPanel beside it.
    std::function<void(float linearLevel)> onMasterLevel;   // 0 .. kClientFaderMax, unity 1
    void useCallbackMaster();                               // switch this Master to callback mode
    void setMasterLevel(float linear);
    void bindMasterMeter(const std::atomic<float>& peak) { bindClientMeter(peak); }
    // Top of the fader, in this strip's pixels, so the master fader lines up with the Client faders.
    // A negative value (the default) puts it just under the name.
    void setFaderTop(int topPx) { faderTopPx = topPx; resized(); }
    // Where the Client strips' faders start (scaled pixels), for the owner to pass to setFaderTop.
    static int clientFaderTop();

    void resized() override;
    void paint(juce::Graphics&) override;

private:
    // Construction and layout in parts (constructor / resized()).
    void addSidechainControls();
    int  layoutSidechain(int stripW, int nameBottom);    // returns the section's height
    int  layoutSends(int stripW, int sendY);             // returns the fader's top
    void layoutFaderAndButtons(int stripW, int faderY);
    void layoutClient();
    void layoutCallbackMaster();
    void addClientControls();
    void paintClientName(juce::Graphics& g) const;

    // Shared by bindChannel / bindReturn: level, pan, mute, solo + sidechain controls.
    void bindStripToApvts(ProcessorBase* proc, const juce::String& prefix);
    template <typename State> void bindStripToState(State& state);

    using Id = MuLookAndFeel::ColourIds;

    Type         channelType;
    juce::String channelName;
    juce::Colour channelColour;
    bool         active = true;

    KnobWithLabel sendEffect { "Effect", Id::knobFxSend };
    KnobWithLabel sendDelay  { "Delay",  Id::knobFxSend };
    KnobWithLabel sendReverb { "Reverb", Id::knobFxSend };
    KnobWithLabel panKnob    { "Pan",    Id::knobPan    };

    // Client strip: the four EQ knobs, indexed by ClientEq (High, Mid Hz, Mid, Low).
    std::array<KnobWithLabel, 4> clientEq { KnobWithLabel("High",   Id::knobInsertPad),
                                            KnobWithLabel("Mid Hz", Id::knobInsertPad),
                                            KnobWithLabel("Mid",    Id::knobInsertPad),
                                            KnobWithLabel("Low",    Id::knobInsertPad) };
    bool clientMuted = false, clientSoloed = false;
    bool callbackMaster  = false;   // Master driven by onMasterLevel, not bindMaster
    int  faderTopPx      = -1;      // callback Master: fader top, -1 = under the name

    juce::Slider      fader;
    VUMeter           vuMeter;
    GRMeter           grMeter;
    juce::Label       dbLabel;
    juce::TextButton  muteBtn { "M" };
    juce::TextButton  soloBtn { "S" };

    // Output bus dropdown (layer channels only): M = Master, 1..8 = direct out.
    juce::ComboBox    outBusBox;

    // Sidechain controls (layer channels only)
    juce::ComboBox    scSourceBox;
    KnobWithLabel     scAmount  { "Amt", Id::knobFxSend };
    KnobWithLabel     scAttack  { "/",   Id::knobFxSend };
    KnobWithLabel     scRelease { "\\",  Id::knobFxSend };

    bool hasSends()            const { return channelType == Type::Channel
                                             || channelType == Type::EffectReturn
                                             || channelType == Type::DelayReturn; }
    bool isClient()            const { return channelType == Type::Client; }
    bool hasMuteSolo()         const { return channelType != Type::Master; }
    bool hasSidechainControls() const { return channelType == Type::Channel
                                             || channelType == Type::EffectReturn
                                             || channelType == Type::DelayReturn
                                             || channelType == Type::ReverbReturn; }
    bool hasOutputBus()        const { return channelType == Type::Channel; }
    bool isMaster()            const { return channelType == Type::Master; }

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
    static constexpr int kSidechainH   = 112;  // clears the Attack/Release knob labels

    // Sends + pan area at Medium baseline. Previously the area was
    // max(144, h*0.35) of strip height, divided 4-up. Now fixed.
    static constexpr int kSendsAreaH   = 186;  // was max(144, h*0.35) → 186 at default
    static constexpr int kSendKnobH    = 46;   // was kSendsAreaH / 4
    static constexpr int kPanKnobH     = 48;   // was kSendsAreaH - 3 * kSendKnobH

    // Section pane bounds — computed in resized(), drawn as rounded rects in paint().
    juce::Rectangle<int> sidechainPaneBounds;
    juce::Rectangle<int> sendsPaneBounds;
    juce::Rectangle<int> faderPaneBounds;
};
