#pragma once
#include "UI/ParamChoices.h"   // mu-core: selector items from choice parameters
#include <juce_gui_basics/juce_gui_basics.h>
#include "UI/ArpHost.h"                 // what the content needs from the layer's host
#include "Sequencer/Arp.h"                // the layer: its modulation matrix
#include "Persistence/LayerParamIds.h"   // layerParamPrefix / layerParamId
#include "Modulation/MuToniModDest.h"    // the modulation target table
#include "UI/Components/MuLookAndFeel.h"
#include "UI/Components/KnobWithLabel.h"
#include "UI/Components/DropdownSelect.h"
#include "UI/Components/StepEditor.h"     // mu-core: the accent pattern cells
#include "UI/ConfirmDialog.h"             // mu-core: shared confirm / name dialogs
#include "Persistence/PresetFiles.h"      // mu-core: listPresetFiles
#include "UI/Voice/InsertSubsection.h"
#include "UI/Voice/VoiceBand.h"            // mu-core: shared voice band (mu-Clid layout)
#include "Audio/Scales.h"
#include "Audio/Chords.h"
#include <array>
#include <memory>
#include <vector>

class ProcessorBase;

namespace mu_toni
{

// μ-Toni's content band inside the shared LayerPanel (kLayerContentH tall): Oscillator 1/2/
// X-Mod/Mix (the two-osc source) on top, the shared mu-core VoiceBand (mu-Clid's layout,
// Pitch·Filter·Amp·Effects) in the middle, and the Appergater below, which takes whatever
// height the other two leave. The header strip and modulators belong to the LayerPanel.
// All sizes come from MuLookAndFeel / VoiceBand — no arbitrary values.
class ArpContent : public juce::Component,
                   private juce::Timer
{
public:
    enum Group { G_OSC1, G_OSC2, G_XMOD, G_MIX, G_PITCH, G_FILTER, G_AMP, G_INSERT, G_ARP, G_COUNT };

    // `base` is handed to the shared mu-core sections that take the processor (the insert section);
    // everything the content itself reads comes through `host`.
    ArpContent(ProcessorBase& base, ArpHost& host);

    ~ArpContent() override { stopTimer(); }

    void setLayer(int idx);

    // Let go of every knob's modulation binding; called before a layer is removed or moved, because the
    // arcs hold the layer's matrix. setLayer rebinds.
    void releaseModulationArcs() { for (auto& k : knobs) k.comp->clearModBinding(); }

    int getLayer() const noexcept { return currentLayer; }

    void lookAndFeelChanged() override { resized(); repaint(); }   // metal style / screws change the layout

    void paintOverChildren(juce::Graphics& g) override;

    void paint(juce::Graphics& g) override;

    void resized() override;

private:
    // The constructor in parts, top of the panel to bottom.
    void addSourceControls();
    void addVoiceControls();
    void addArpControls();
    void setStepLevel(int step, int level);  // write one step's Off / On / Accented (0 / 1 / 2) into the layer's pattern
    void refreshAccentSteps();               // show the layer's pattern + the arp's place in it
    void buildVoiceBand();
    void bindModulationArcs();               // live modulation arcs on the knobs the target table drives

    static constexpr Group kSourceGroups[4] = { G_OSC1, G_OSC2, G_XMOD, G_MIX };

    // Unscaled width a boxed section's controls need: its top row (dropdowns + toggles) or its
    // knob row, whichever is wider (the widths layoutBox uses).
    int sourceContentW(int group) const;

    // The source row: Osc 1 | Osc 2 | X-Mod | Mix, each as wide as its controls need plus an
    // equal share of what's left.
    void layoutSourceRow(juce::Rectangle<int> row, int gap);


    // Layout constants (unscaled; wrap in mu_ui::s at use). kLabelGap keeps a control label off the
    // panel border (design-ui-family §"Control label gap").
    static constexpr int kBoxPad    = MuLookAndFeel::kSpaceS;
    static constexpr int kLabelGap  = MuLookAndFeel::kSpaceS;
    // A boxed section's top row: dropdown / toggle widths and the gap after each.
    static constexpr int kBoxComboW  = 108;
    static constexpr int kBoxToggleW = 64;
    static constexpr int kBoxGap     = MuLookAndFeel::kSpaceXS;

    struct KnobDef   { std::unique_ptr<KnobWithLabel>  comp; juce::String suffix, prefix; int group;
                       std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>   att; };
    struct ComboDef  { std::unique_ptr<DropdownSelect> comp; juce::String suffix; int group;
                       std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> att; };
    struct ToggleDef { std::unique_ptr<juce::ToggleButton> comp; juce::String suffix; int group;
                       std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>   att; };

    juce::Colour layerColour() const;

    KnobWithLabel*  findKnob (const char* s) { for (auto& k : knobs)  if (k.suffix == s) return k.comp.get(); return nullptr; }
    DropdownSelect* findCombo(const char* s) { for (auto& c : combos) if (c.suffix == s) return c.comp.get(); return nullptr; }

    // Boxed section (Osc 1/2/Mix, Appergater): dropdowns/toggles on top, Size-2 knobs flow.
    void layoutBox(int group, juce::Rectangle<int> rect);

    void addKnob(int group, const char* suffix, const juce::String& label,
                 MuLookAndFeel::ColourIds colour, const char* prefix = kLayerParamLetter)
    {
        KnobDef d;
        d.comp = std::make_unique<KnobWithLabel>(label, colour);
        d.suffix = suffix; d.prefix = prefix; d.group = group;
        d.comp->onStatusUpdate = [this](const juce::String& n, const juce::String& val)
        { if (onStatusUpdate) onStatusUpdate(n, val); };
        addAndMakeVisible(*d.comp);
        knobs.push_back(std::move(d));
    }

    void addCombo(int group, const char* suffix, const juce::StringArray& items);

    void addToggle(int group, const char* suffix, const juce::String& label);

    static juce::StringArray scaleItems()
    { juce::StringArray a; for (int i = 0; i < kNumScales; ++i) a.add(kScales[(size_t) i].name); return a; }
    static juce::StringArray chordItems()
    { juce::StringArray a; for (int i = 0; i < kNumChords; ++i) a.add(kChords[(size_t) i].name); return a; }
    juce::StringArray waveItems() const { return host.getWavetableNames(); }
    static juce::StringArray rateItems()
    { return { "1/4","1/4.","1/4T","1/8","1/8.","1/8T","1/16","1/16.","1/16T","1/32","1/32.","1/32T" }; }
    static juce::StringArray filterItems()
    { return { "LP12","HP12","BP12","Notch","LP24","HP24","BP24","LP6",
               "Comb+","AP12","Notch24","HP6","Peak","LoShf","HiShf","Comb-" }; }

    void timerCallback() override;

public:
    std::function<void(const juce::String&, const juce::String&)> onStatusUpdate;

private:
    ArpHost& host;
    juce::AudioProcessorValueTreeState& apvts;   // host.getArpParameters(), cached
    int currentLayer = 0;
    std::vector<KnobDef>   knobs;
    std::vector<ComboDef>  combos;
    std::vector<ToggleDef> toggles;
    std::array<juce::Rectangle<int>, 4> oscR;   // Osc 1, Osc 2, X-Mod, Mix
    juce::Rectangle<int> voiceR, arpR;
    // The three metal panels filling the band: source, voice band, Appergater.
    juce::Rectangle<int> srcR, voicePanelR, arpPanelR;

    StepEditor accentSteps;                       // the Appergater's step pattern (Off / On / Accented cells)
    int shownAccentLen = -1, shownAccentPat = -1, shownStepMask = -1; // what accentSteps shows (redrawn on change)
    int accentParamsLayer = -1;                    // layer the three pointers below were looked up for
    std::atomic<float>* accLenParam  = nullptr;
    std::atomic<float>* accPatParam  = nullptr;
    std::atomic<float>* stepMaskParam = nullptr;
    std::atomic<float>* arpRateParam = nullptr;

    InsertSubsection insertSub;
    // The voice band and its Pitch / Filter / Amp sections (built from the controls above).
    VoiceBandSection pitchBox  { 4, VoiceBand::kCols };
    VoiceBandSection filterBox { 6, VoiceBand::kFilterColW };
    VoiceBandSection ampBox    { 4, VoiceBand::kCols };
    VoiceBand        voiceBand;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ArpContent)
};

} // namespace mu_toni
