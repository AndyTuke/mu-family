#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include "UI/Components/KnobWithLabel.h"
#include "UI/Components/MuLookAndFeel.h"
#include "UI/Components/DropdownSelect.h"
#include "UI/ChannelHeaderBar.h"
#include "UI/ModulatorPanel.h"
#include "UI/Voice/InsertSubsection.h"   // shared mu-core insert panel
#include "Modulation/MuTantModDest.h"
#include "UI/GatingDesigner.h"

#include <vector>

namespace mu_tant
{

// Round toggle button for filter Series / Parallel routing.
// Always renders in the family purple (knobEuclidean). Clicking animates a 90° rotation:
//   Parallel (off) = 0°  — two horizontal lines (═), two signal paths side by side
//   Series   (on)  = 90° — two vertical lines (‖)
class FilterRoutingButton : public juce::Button,
                            private juce::Timer,
                            private juce::Value::Listener
{
public:
    FilterRoutingButton();
    ~FilterRoutingButton() override;
    void paintButton(juce::Graphics&, bool highlighted, bool) override;

private:
    float animAngle   = 0.0f;    // current rendered angle (0=horizontal, 90=vertical)
    float targetAngle = 0.0f;
    bool  angleInit   = false;   // snap to state on first sync, animate thereafter

    // Sync the glyph to the toggle state from ANY source (user click, preset load,
    // DAW automation, voice rebind) — not just user clicks.
    void valueChanged(juce::Value&) override;
    void timerCallback() override;
};

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

    // Null out the modulator slot pointer so no timer or paint callback can
    // dereference it during the voice-remove → setVoice rebind window.
    void clearModulatorSlot() { modulatorPanel.setVoiceSlot(nullptr); }

    // Clear raw ModulationMatrix pointers from all KnobWithLabel mod-binding arcs.
    // Call before resetVoiceSlot / removeVoice destroys the VoiceSlot so the
    // 30 Hz timer cannot dereference freed matrix memory.
    void clearAllModBindings();

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
    KnobWithLabel o1OctKnob      { "Oct",  MuLookAndFeel::knobEuclidean };
    KnobWithLabel o1SemiKnob     { "Semi", MuLookAndFeel::knobEuclidean };
    KnobWithLabel o1FineKnob     { "Fine", MuLookAndFeel::knobEuclidean };
    KnobWithLabel o1PosKnob      { "Pos",  MuLookAndFeel::knobEuclidean };
    KnobWithLabel o1PenvDepthKnob{ "PEnv", MuLookAndFeel::knobEuclidean };
    std::unique_ptr<APVTS::SliderAttachment> o1OctAttachment;
    std::unique_ptr<APVTS::SliderAttachment> o1SemiAttachment;
    std::unique_ptr<APVTS::SliderAttachment> o1FineAttachment;
    std::unique_ptr<APVTS::SliderAttachment> o1PosAttachment;
    std::unique_ptr<APVTS::SliderAttachment> o1PenvDepthAttachment;

    // ── Oscillator 2 ────────────────────────────────────────────────────────
    KnobWithLabel o2OctKnob      { "Oct",  MuLookAndFeel::knobEuclidean };
    KnobWithLabel o2SemiKnob     { "Semi", MuLookAndFeel::knobEuclidean };
    KnobWithLabel o2FineKnob     { "Fine", MuLookAndFeel::knobEuclidean };
    KnobWithLabel o2PosKnob      { "Pos",  MuLookAndFeel::knobEuclidean };
    KnobWithLabel o2PenvDepthKnob{ "PEnv", MuLookAndFeel::knobEuclidean };
    std::unique_ptr<APVTS::SliderAttachment> o2OctAttachment;
    std::unique_ptr<APVTS::SliderAttachment> o2SemiAttachment;
    std::unique_ptr<APVTS::SliderAttachment> o2FineAttachment;
    std::unique_ptr<APVTS::SliderAttachment> o2PosAttachment;
    std::unique_ptr<APVTS::SliderAttachment> o2PenvDepthAttachment;

    // ── Wavetable selection (placeholder dropdowns — no engine wiring yet) ────
    DropdownSelect osc1WaveDropdown;
    DropdownSelect osc2WaveDropdown;

    // ── Cross-mod (FM / AM / Ring — all simultaneously at individual depths) ────
    KnobWithLabel xmodFmKnob   { "FM",   MuLookAndFeel::knobEuclidean };
    KnobWithLabel xmodAmKnob   { "AM",   MuLookAndFeel::knobEuclidean };
    KnobWithLabel xmodRingKnob { "Ring", MuLookAndFeel::knobEuclidean };
    juce::TextButton syncButton { "Sync" };
    std::unique_ptr<APVTS::SliderAttachment> xmodFmAttachment;
    std::unique_ptr<APVTS::SliderAttachment> xmodAmAttachment;
    std::unique_ptr<APVTS::SliderAttachment> xmodRingAttachment;
    std::unique_ptr<APVTS::ButtonAttachment> syncAttachment;

    // ── Mixer levels (osc1 / osc2 / noise — Size-2 knobs, horizontal MIXER row
    //    under the NOISE panel) + noise type (in its own NOISE panel) ─────────
    KnobWithLabel  osc1LevelKnob  { "Osc 1", MuLookAndFeel::knobLevel };
    KnobWithLabel  osc2LevelKnob  { "Osc 2", MuLookAndFeel::knobLevel };
    KnobWithLabel  noiseLevelKnob { "Noise", MuLookAndFeel::knobLevel };
    juce::Label    noiseTypeLabel;
    DropdownSelect noiseTypeDropdown;
    std::unique_ptr<APVTS::SliderAttachment>   osc1LevelAttachment;
    std::unique_ptr<APVTS::SliderAttachment>   osc2LevelAttachment;
    std::unique_ptr<APVTS::SliderAttachment>   noiseLevelAttachment;
    std::unique_ptr<APVTS::ComboBoxAttachment> noiseTypeAttachment;

    // ── Filter 1 ────────────────────────────────────────────────────────────
    juce::Label    fltTypeLabel;
    DropdownSelect fltTypeDropdown;
    KnobWithLabel  fltDrvKnob      { "Drive",     MuLookAndFeel::knobPostPad };
    KnobWithLabel  fltCutKnob      { "Cutoff",    MuLookAndFeel::knobPostPad };
    KnobWithLabel  fltResKnob      { "Resonance", MuLookAndFeel::knobPostPad };
    KnobWithLabel  fltEnvDepthKnob { "FEnv",      MuLookAndFeel::knobPostPad };
    KnobWithLabel  fltLoCutKnob    { "Low Cut",   MuLookAndFeel::knobPostPad };
    std::unique_ptr<APVTS::SliderAttachment>   fltDrvAttachment;
    std::unique_ptr<APVTS::SliderAttachment>   fltCutAttachment;
    std::unique_ptr<APVTS::SliderAttachment>   fltResAttachment;
    std::unique_ptr<APVTS::SliderAttachment>   fltEnvDepthAttachment;
    std::unique_ptr<APVTS::SliderAttachment>   fltLoCutAttachment;

    // ── Filter 2 ────────────────────────────────────────────────────────────
    juce::Label    flt2TypeLabel;
    DropdownSelect flt2TypeDropdown;
    KnobWithLabel  flt2DrvKnob      { "Drive",     MuLookAndFeel::knobPostPad };
    KnobWithLabel  flt2CutKnob      { "Cutoff",    MuLookAndFeel::knobPostPad };
    KnobWithLabel  flt2ResKnob      { "Resonance", MuLookAndFeel::knobPostPad };
    KnobWithLabel  flt2EnvDepthKnob { "FEnv",      MuLookAndFeel::knobPostPad };
    KnobWithLabel  flt2LoCutKnob    { "Low Cut",   MuLookAndFeel::knobPostPad };
    FilterRoutingButton fltSeriesBtn;
    std::unique_ptr<APVTS::SliderAttachment>   flt2DrvAttachment;
    std::unique_ptr<APVTS::SliderAttachment>   flt2CutAttachment;
    std::unique_ptr<APVTS::SliderAttachment>   flt2ResAttachment;
    std::unique_ptr<APVTS::SliderAttachment>   flt2EnvDepthAttachment;
    std::unique_ptr<APVTS::SliderAttachment>   flt2LoCutAttachment;
    std::unique_ptr<APVTS::ButtonAttachment>   fltSeriesAttachment;

    // ── Output level ────────────────────────────────────────────────────────
    KnobWithLabel levelKnob { "Level", MuLookAndFeel::knobLevel };
    std::unique_ptr<APVTS::SliderAttachment> levelAttachment;

    // ── Insert effect (shared mu-core InsertSubsection, "v" prefix) ──────────
    // Identical UI to mu-clid; rebound to the active voice in setVoice(). Sits in
    // the voice section: synth engine → insert → mixer (family signal flow).
    // Constructed in the .cpp init list — needs PluginProcessor's complete type
    // for the derived→ProcessorBase& conversion (only forward-declared here).
    InsertSubsection insertSub;

    // ── Gating designer (full-width; Gap slider + Bypass button live inside it) ──
    GatingDesigner gatingDesigner;
    std::unique_ptr<APVTS::SliderAttachment> gapAttachment;
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
    void bindModulationIndicators();

    // Wavetable selector dropdowns: rebuilt per voice (factory names + an optional
    // user-import item + "Load .wav…"). Selection routes through handleWtSelection.
    void refreshWavetableDropdowns();
    void handleWtSelection(int oscIdx, int itemId);
    std::unique_ptr<juce::FileChooser> wtChooser;   // kept alive during async load
    std::vector<juce::File> wtFolderFiles;          // dropdown id (kWtFolderBase+i) → Wavetables/ file

    // 30 Hz timer — drives the gating-grid playhead + modulator playhead from
    // the processor's transport beat position.
    void timerCallback() override;

    // Sub-panel geometry — populated by resized(), consumed by paint() for the
    // bordered sub-panels + their titles so layout + decoration stay in sync.
    juce::Rectangle<int> osc1PanelR, osc2PanelR, modNoisePanelR, filterPanelR, noisePanelR, mixerPanelR, insertPanelR;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VoicePanel)
};

} // namespace mu_tant
