#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include "UI/Components/KnobWithLabel.h"
#include "UI/Components/DropdownSelect.h"

namespace mu_tant
{

class PluginProcessor;

// First-stab voice page for mu-Tant. Three rows of hand-laid mu-core knobs
// bound to APVTS via juce::SliderAttachment so the audio engine stays the
// single source of truth. Layout is deliberately simple — when the real synth
// UI lands (multi-layer + drawable gate sequencer) this gets replaced.
class VoicePanel : public juce::Component
{
public:
    explicit VoicePanel(PluginProcessor& proc);
    ~VoicePanel() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    PluginProcessor& proc;

    using APVTS = juce::AudioProcessorValueTreeState;

    // ── Tonal centre ────────────────────────────────────────────────────────
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

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VoicePanel)
};

} // namespace mu_tant
