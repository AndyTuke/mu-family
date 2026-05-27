#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include "UI/Components/KnobWithLabel.h"
#include "UI/Components/DropdownSelect.h"
#include "UI/ModulatorPanel.h"
#include "Modulation/MuTantModDest.h"
#include "UI/GatingDesigner.h"

namespace mu_tant
{

class PluginProcessor;

// Voice editor page — same layout shape as mu-clid's RhythmPanel (header strip /
// central wavetable synth UI / bottom row of PITCH-equivalent FILTER LEVEL knobs).
// `setVoice(idx)` rebinds every APVTS attachment to the v{idx}_* param family so
// clicking a different voice in the sidebar swaps the editor's target slot.
// Root + scale are shared (global APVTS params) and stay bound across voice
// changes.
class VoicePanel : public juce::Component
{
public:
    explicit VoicePanel(PluginProcessor& proc);
    ~VoicePanel() override;

    // Rebind every per-voice attachment to the v{idx}_* family. Shared params
    // (root, scale) are unaffected.
    void setVoice(int voiceIndex);
    int  getVoice() const noexcept { return currentVoice; }

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

    // ── Oscillator 1 ────────────────────────────────────────────────────────
    KnobWithLabel o1OctKnob  { "Osc1 Oct" };
    KnobWithLabel o1ToneKnob { "Osc1 Tone" };
    KnobWithLabel o1FineKnob { "Osc1 Fine" };
    KnobWithLabel o1PosKnob  { "Osc1 Pos" };
    std::unique_ptr<APVTS::SliderAttachment> o1OctAttachment;
    std::unique_ptr<APVTS::SliderAttachment> o1ToneAttachment;
    std::unique_ptr<APVTS::SliderAttachment> o1FineAttachment;
    std::unique_ptr<APVTS::SliderAttachment> o1PosAttachment;

    // ── Oscillator 2 ────────────────────────────────────────────────────────
    KnobWithLabel o2OctKnob  { "Osc2 Oct" };
    KnobWithLabel o2ToneKnob { "Osc2 Tone" };
    KnobWithLabel o2FineKnob { "Osc2 Fine" };
    KnobWithLabel o2PosKnob  { "Osc2 Pos" };
    std::unique_ptr<APVTS::SliderAttachment> o2OctAttachment;
    std::unique_ptr<APVTS::SliderAttachment> o2ToneAttachment;
    std::unique_ptr<APVTS::SliderAttachment> o2FineAttachment;
    std::unique_ptr<APVTS::SliderAttachment> o2PosAttachment;

    // ── Cross-mod + balance ─────────────────────────────────────────────────
    KnobWithLabel  xmodKnob  { "X-Mod" };
    juce::Label    xmodLabel;
    DropdownSelect xmodModeDropdown;
    KnobWithLabel  mixKnob   { "Mix" };
    std::unique_ptr<APVTS::SliderAttachment>   xmodAttachment;
    std::unique_ptr<APVTS::ComboBoxAttachment> xmodModeAttachment;
    std::unique_ptr<APVTS::SliderAttachment>   mixAttachment;

    // ── Filter ──────────────────────────────────────────────────────────────
    juce::Label    fltTypeLabel;
    DropdownSelect fltTypeDropdown;
    KnobWithLabel  fltCutKnob { "Cutoff" };
    KnobWithLabel  fltResKnob { "Resonance" };
    std::unique_ptr<APVTS::ComboBoxAttachment> fltTypeAttachment;
    std::unique_ptr<APVTS::SliderAttachment>   fltCutAttachment;
    std::unique_ptr<APVTS::SliderAttachment>   fltResAttachment;

    // ── Output level ────────────────────────────────────────────────────────
    KnobWithLabel levelKnob { "Level" };
    std::unique_ptr<APVTS::SliderAttachment> levelAttachment;

    // ── Gating designer (full-width 2-bar gate strip) ───────────────────────
    GatingDesigner   gatingDesigner;

    // ── Modulator section (mu-core ModulatorPanel + mu-tant destinations) ──
    // Rebound to the current voice's VoiceSlot whenever setVoice() runs.
    ::ModulatorPanel modulatorPanel;
    ModDestProvider  modDestProvider;

    // Voice indicator strip (top of panel). Mirrors the active voice's
    // palette colour so users always see which slot the panel is editing.
    juce::Label voiceTag;
    void refreshVoiceTag();

    void rebindAttachments();

    // Band geometry — populated by resized(), consumed by paint() for the
    // band outlines + labels so layout + decoration stay in sync.
    int band1Y = 0, band1H = 0;   // Oscillators
    int band2Y = 0, band2H = 0;   // Filter
    int band3Y = 0, band3H = 0;   // Gating

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VoicePanel)
};

} // namespace mu_tant
