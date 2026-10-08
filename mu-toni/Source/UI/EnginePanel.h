#pragma once
#include "UI/ParamChoices.h"   // mu-core: selector items from choice parameters
#include <juce_gui_basics/juce_gui_basics.h>
#include "Plugin/PluginProcessor.h"
#include "UI/Components/MuLookAndFeel.h"
#include "UI/Components/KnobWithLabel.h"
#include "UI/Components/DropdownSelect.h"
#include "UI/ModulatorPanel.h"
#include "UI/ChannelHeaderBar.h"          // mu-core: shared per-layer header (name / reset / presets / save)
#include "UI/Components/StepEditor.h"     // mu-core: the accent pattern cells
#include "UI/ConfirmDialog.h"             // mu-core: shared confirm / name dialogs
#include "Persistence/PresetFiles.h"      // mu-core: listPresetFiles
#include "UI/Voice/InsertSubsection.h"
#include "UI/Voice/VoiceBand.h"            // mu-core: shared voice band (mu-Clid layout)
#include "Modulation/MuToniModDest.h"
#include "Audio/Scales.h"
#include "Audio/Chords.h"
#include "Audio/Wavetable/WavetableBank.h"   // mu-core: the wavetable names
#include <array>
#include <memory>
#include <vector>

namespace mu_toni
{

// μ-Toni engine panel. The shared per-layer header bar sits on top (layer presets + reset).
// The Pitch·Filter·Amp·Effects voice band is the shared mu-core VoiceBand (mu-Clid's layout),
// its sections built from this panel's controls. Oscillator 1/2/Mix (mu-toni's two-osc
// source) sits above; the Appergater band + shared modulator section below. All sizes come
// from MuLookAndFeel / VoiceBand — no arbitrary values.
class EnginePanel : public juce::Component,
                    private juce::Timer
{
public:
    enum Group { G_OSC1, G_OSC2, G_XMOD, G_MIX, G_PITCH, G_FILTER, G_AMP, G_INSERT, G_ARP, G_COUNT };

    explicit EnginePanel(PluginProcessor& processor);

    ~EnginePanel() override
    {
        stopTimer();
        modulatorPanel.setVoiceSlot(nullptr);
    }

    void setLayer(int idx);

    int getLayer() const noexcept { return currentLayer; }

    // Rescan the layer-preset folder into the header's preset list (after a save).
    void refreshPresetList();

    void lookAndFeelChanged() override { resized(); repaint(); }   // metal style / screws change the layout

    void paintOverChildren(juce::Graphics& g) override;

    void paint(juce::Graphics& g) override;

    void resized() override;

private:
    // The constructor in parts, top of the panel to bottom.
    void addSourceControls();
    void addVoiceControls();
    void addArpControls();
    void setAccentStep(int step, bool on);   // write one accent cell into the layer's pattern
    void refreshAccentSteps();               // show the layer's pattern + the arp's place in it
    void buildVoiceBand();
    void setupHeaderAndModulators();

    static constexpr Group kSourceGroups[4] = { G_OSC1, G_OSC2, G_XMOD, G_MIX };

    // Unscaled width a boxed section's controls need: its top row (dropdowns + toggles) or its
    // knob row, whichever is wider (the widths layoutBox uses).
    int sourceContentW(int group) const;

    // The source row: Osc 1 | Osc 2 | X-Mod | Mix, each as wide as its controls need plus an
    // equal share of what's left.
    void layoutSourceRow(juce::Rectangle<int> row, int gap);

    // Metal style, as mu-Clid: panels edge to edge — preset strip, source (Osc 1 / Osc 2 / Mix),
    // voice band, Appergater, modulators — content inset clear of the panels' corner screws,
    // each section a raised box with its name plate in the band above it.
    void layoutMetal();

    // Layout constants (unscaled; wrap in mu_ui::s at use). kDropdownH is the
    // family-standard dropdown height; kLabelGap keeps a control label off the
    // panel border (design-ui-family §"Control label gap").
    static constexpr int kBoxPad    = MuLookAndFeel::kSpaceS;
    static constexpr int kDropdownH = 24;
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
                 MuLookAndFeel::ColourIds colour, const char* prefix = "v")
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
    static juce::StringArray waveItems() { return mu_wavetable::WavetableBank::factoryTableNames(); }
    static juce::StringArray rateItems()
    { return { "1/4","1/4.","1/4T","1/8","1/8.","1/8T","1/16","1/16.","1/16T","1/32","1/32.","1/32T" }; }
    static juce::StringArray filterItems()
    { return { "LP12","HP12","BP12","Notch","LP24","HP24","BP24","LP6",
               "Comb+","AP12","Notch24","HP6","Peak","LoShf","HiShf","Comb-" }; }

    void timerCallback() override;

public:
    std::function<void(const juce::String&, const juce::String&)> onStatusUpdate;

private:
    PluginProcessor& proc;
    int currentLayer = 0;
    std::vector<KnobDef>   knobs;
    std::vector<ComboDef>  combos;
    std::vector<ToggleDef> toggles;
    std::array<juce::Rectangle<int>, 4> oscR;   // Osc 1, Osc 2, X-Mod, Mix
    juce::Rectangle<int> voiceR, arpR, headerR;
    // Metal style: the panels.
    juce::Rectangle<int> srcR, voicePanelR, arpPanelR, modR;

    ChannelHeaderBar header;

    StepEditor accentSteps;                       // the Appergater's accent pattern (on/off cells)
    int shownAccentLen = -1, shownAccentPat = -1; // what accentSteps shows (redrawn on change)
    int accentParamsLayer = -1;                    // layer the three pointers below were looked up for
    std::atomic<float>* accLenParam  = nullptr;
    std::atomic<float>* accPatParam  = nullptr;
    std::atomic<float>* arpRateParam = nullptr;

    InsertSubsection insertSub;
    // The voice band and its Pitch / Filter / Amp sections (built from the controls above).
    VoiceBandSection pitchBox  { 4, VoiceBand::kCols };
    VoiceBandSection filterBox { 6, VoiceBand::kFilterColW };
    VoiceBandSection ampBox    { 4, VoiceBand::kCols };
    VoiceBand        voiceBand;
    ::ModulatorPanel modulatorPanel;
    ModDestProvider  modDestProvider = makeModDestProvider();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EnginePanel)
};

} // namespace mu_toni
