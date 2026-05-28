#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include "UI/Components/KnobWithLabel.h"
#include "UI/Components/DropdownSelect.h"
#include "UI/ChannelHeaderBar.h"
#include "UI/ModulatorPanel.h"
#include "Modulation/MuTantModDest.h"
#include "UI/GatingDesigner.h"

#include <vector>

namespace mu_tant
{

class PluginProcessor;

// Voice editor page — same layout shape as mu-clid's RhythmPanel (header strip /
// central wavetable synth UI / bottom row of PITCH-equivalent FILTER LEVEL knobs).
// `setVoice(idx)` rebinds every APVTS attachment to the v{idx}_* param family so
// clicking a different voice in the sidebar swaps the editor's target slot.
// Root + scale are shared (global APVTS params) and stay bound across voice
// changes.
class VoicePanel : public juce::Component,
                   private juce::Timer
{
public:
    explicit VoicePanel(PluginProcessor& proc);
    ~VoicePanel() override;

    // Rebind every per-voice attachment to the v{idx}_* family. Shared params
    // (root, scale) are unaffected.
    void setVoice(int voiceIndex);
    int  getVoice() const noexcept { return currentVoice; }

    // Fired when the header Delete button is clicked (editor removes the voice).
    // Mirrors mu-clid's RhythmPanel delete affordance.
    std::function<void()> onDeleteVoice;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    PluginProcessor& proc;
    int currentVoice = 0;

    using APVTS = juce::AudioProcessorValueTreeState;

    // ── Tonal centre (shared across voices) ─────────────────────────────────
    juce::Label    rootLabel;
    DropdownSelect rootDropdown;
    juce::Label    scaleLabel;
    DropdownSelect scaleDropdown;
    std::unique_ptr<APVTS::ComboBoxAttachment> rootAttachment;
    std::unique_ptr<APVTS::ComboBoxAttachment> scaleAttachment;

    // Knob colours follow mu-clid's category logic: oscillator/pitch = purple
    // (knobEuclidean), filter = teal (knobPostPad), levels = amber (knobLevel),
    // gate/FX = coral (knobFxSend).
    // ── Oscillator 1 ────────────────────────────────────────────────────────
    KnobWithLabel o1OctKnob  { "Oct",  MuLookAndFeel::knobEuclidean };
    KnobWithLabel o1SemiKnob { "Semi", MuLookAndFeel::knobEuclidean };
    KnobWithLabel o1FineKnob { "Fine", MuLookAndFeel::knobEuclidean };
    KnobWithLabel o1PosKnob  { "Pos",  MuLookAndFeel::knobEuclidean };
    std::unique_ptr<APVTS::SliderAttachment> o1OctAttachment;
    std::unique_ptr<APVTS::SliderAttachment> o1SemiAttachment;
    std::unique_ptr<APVTS::SliderAttachment> o1FineAttachment;
    std::unique_ptr<APVTS::SliderAttachment> o1PosAttachment;

    // ── Oscillator 2 ────────────────────────────────────────────────────────
    KnobWithLabel o2OctKnob  { "Oct",  MuLookAndFeel::knobEuclidean };
    KnobWithLabel o2SemiKnob { "Semi", MuLookAndFeel::knobEuclidean };
    KnobWithLabel o2FineKnob { "Fine", MuLookAndFeel::knobEuclidean };
    KnobWithLabel o2PosKnob  { "Pos",  MuLookAndFeel::knobEuclidean };
    std::unique_ptr<APVTS::SliderAttachment> o2OctAttachment;
    std::unique_ptr<APVTS::SliderAttachment> o2SemiAttachment;
    std::unique_ptr<APVTS::SliderAttachment> o2FineAttachment;
    std::unique_ptr<APVTS::SliderAttachment> o2PosAttachment;

    // ── Wavetable selection (placeholder dropdowns — no engine wiring yet) ────
    DropdownSelect osc1WaveDropdown;
    DropdownSelect osc2WaveDropdown;

    // ── Cross-mod ─────────────────────────────────────────────────────────────
    KnobWithLabel  xmodKnob  { "X-Mod", MuLookAndFeel::knobEuclidean };
    juce::Label    xmodLabel;
    DropdownSelect xmodModeDropdown;
    std::unique_ptr<APVTS::SliderAttachment>   xmodAttachment;
    std::unique_ptr<APVTS::ComboBoxAttachment> xmodModeAttachment;

    // ── Mixer levels (osc1 / osc2 / noise — Size-3 knobs in the right panel) ──
    KnobWithLabel  osc1LevelKnob  { "Osc 1", MuLookAndFeel::knobLevel };
    KnobWithLabel  osc2LevelKnob  { "Osc 2", MuLookAndFeel::knobLevel };
    KnobWithLabel  noiseLevelKnob { "Noise", MuLookAndFeel::knobLevel };
    juce::Label    noiseTypeLabel;
    DropdownSelect noiseTypeDropdown;
    std::unique_ptr<APVTS::SliderAttachment>   osc1LevelAttachment;
    std::unique_ptr<APVTS::SliderAttachment>   osc2LevelAttachment;
    std::unique_ptr<APVTS::SliderAttachment>   noiseLevelAttachment;
    std::unique_ptr<APVTS::ComboBoxAttachment> noiseTypeAttachment;

    // ── Filter ──────────────────────────────────────────────────────────────
    juce::Label    fltTypeLabel;
    DropdownSelect fltTypeDropdown;
    KnobWithLabel  fltCutKnob { "Cutoff",    MuLookAndFeel::knobPostPad };
    KnobWithLabel  fltResKnob { "Resonance", MuLookAndFeel::knobPostPad };
    std::unique_ptr<APVTS::ComboBoxAttachment> fltTypeAttachment;
    std::unique_ptr<APVTS::SliderAttachment>   fltCutAttachment;
    std::unique_ptr<APVTS::SliderAttachment>   fltResAttachment;

    // ── Output level ────────────────────────────────────────────────────────
    KnobWithLabel levelKnob { "Level", MuLookAndFeel::knobLevel };
    std::unique_ptr<APVTS::SliderAttachment> levelAttachment;

    // ── Gating designer + Gap knob + Gater bypass ───────────────────────────
    GatingDesigner   gatingDesigner;
    KnobWithLabel    gapKnob { "Gap", MuLookAndFeel::knobFxSend };
    std::unique_ptr<APVTS::SliderAttachment> gapAttachment;
    juce::TextButton gateBypassButton { "Bypass" };
    std::unique_ptr<APVTS::ButtonAttachment> gateBypassAttachment;

    // ── Modulator section (mu-core ModulatorPanel + mu-tant destinations) ──
    // Rebound to the current voice's VoiceSlot whenever setVoice() runs.
    ::ModulatorPanel modulatorPanel;
    ModDestProvider  modDestProvider;

    // Shared per-layer header bar (name / reset / delete / preset / save) —
    // identical across mu-products. Replaces the bespoke voice tag + Delete btn.
    ChannelHeaderBar headerBar;
    void refreshHeader();              // name + colour + preset list for the active voice
    void refreshVoicePresetList();
    std::vector<juce::File> voicePresetFiles;   // dropdown id (1-based) → file

    void rebindAttachments();

    // 30 Hz timer — drives the gating-grid playhead + modulator playhead from
    // the processor's transport beat position.
    void timerCallback() override;

    // Sub-panel geometry — populated by resized(), consumed by paint() for the
    // bordered sub-panels + their titles so layout + decoration stay in sync.
    juce::Rectangle<int> osc1PanelR, osc2PanelR, modNoisePanelR, filterPanelR, mixerPanelR;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VoicePanel)
};

} // namespace mu_tant
