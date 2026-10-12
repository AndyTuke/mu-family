#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include "UI/Components/KnobWithLabel.h"
#include "UI/Components/MuLookAndFeel.h"
#include "UI/Components/DropdownSelect.h"
#include "UI/Components/SegmentControl.h"
#include "UI/Voice/InsertSubsection.h"   // shared mu-core insert panel
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

// μ-Tant's content band inside the shared LayerPanel (kLayerContentH tall): Root / Scale, the
// oscillator / noise row, the X-Mod / Filter / Effects row and the gate editor, which is the flex
// panel and takes the height the rows leave. The header strip and modulators belong to the
// LayerPanel. `setLayer(idx)` rebinds every APVTS attachment to the v{idx}_* param family so
// clicking a different voice in the sidebar swaps the editor's target slot. Root + scale are
// shared (global APVTS params) and stay bound across voice changes.
class PatternContent : public juce::Component,
                   private juce::Timer
{
public:
    explicit PatternContent(PluginProcessor& proc);
    ~PatternContent() override;

    // Rebind every per-voice attachment to the v{idx}_* family. Shared params
    // (root, scale) are unaffected.
    void setLayer(int layerIndex);
    int  getLayer() const noexcept { return currentVoice; }

    // The insert algorithm changed: the editor re-labels the modulators' insert destinations.
    std::function<void(int)> onInsertAlgorithmChanged;

    // Same for the gate editor's three envelope patterns, which belong to the voice's Pattern.
    void clearPatternBindings()
    {
        gatingDesigner.setPattern(nullptr);
        gatingDesigner.setFilterPattern(nullptr);
        gatingDesigner.setPitchPattern(nullptr);
    }

    // Clear raw ModulationMatrix pointers from all KnobWithLabel mod-binding arcs.
    // Call before removeLayer destroys the Layer so the
    // 30 Hz timers cannot dereference freed matrix or insert-meter memory.
    void clearAllModBindings();

    void paint(juce::Graphics& g) override;
    void resized() override;
    void paintOverChildren(juce::Graphics& g) override;   // panel screws, over the content
    void lookAndFeelChanged() override { resized(); repaint(); }   // metal style / screws change the layout

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

    // ── Cross-mod — 2-lane bus model (mu-tant-xmod-design.md) ───────────────────
    // Lane A: Index knob + Phase mode (FM/PM/TZFM) + Sync + Feedback toggles.
    // Lane B: Depth knob (drives Depth in Mult mode, SSB shift in SSB mode — the
    //         attachment + range swap with the mode) + Amp mode (Mult/SSB).
    KnobWithLabel    xmodIndexKnob { "Index", MuLookAndFeel::knobEuclidean };
    KnobWithLabel    xmodDepthKnob { "Depth", MuLookAndFeel::knobEuclidean };
    SegmentControl   phaseModeCtrl { { "FM", "PM", "TZFM" } };
    SegmentControl   ampModeCtrl   { { "AM", "RM", "SSB" } };
    juce::TextButton syncButton { "Sync" };
    juce::TextButton fdbkButton { "Fdbk" };
    std::unique_ptr<APVTS::SliderAttachment> xmodIndexAttachment;
    std::unique_ptr<APVTS::SliderAttachment> xmodDepthAttachment;   // rebound to depth/ssb on mode change
    std::unique_ptr<APVTS::ButtonAttachment> syncAttachment;
    std::unique_ptr<APVTS::ButtonAttachment> fdbkAttachment;
    // Rebinds the Depth knob to xmod_depth (Mult) or xmod_ssb (SSB) for the active voice.
    void bindAmpDepthKnob(bool ssbMode);
    int  currentAmpMode = 0;   // cached so a mode change can rebind the depth knob

    // ── Source levels — each at the right-hand end of its own box (Osc 1 / Osc 2 / Noise),
    //    the noise type selector to the left of the noise level ─────────────────
    KnobWithLabel  osc1LevelKnob  { "Level", MuLookAndFeel::knobLevel };
    KnobWithLabel  osc2LevelKnob  { "Level", MuLookAndFeel::knobLevel };
    KnobWithLabel  noiseLevelKnob { "Level", MuLookAndFeel::knobLevel };
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
    // Identical UI to mu-clid; rebound to the active voice in setLayer(). Sits in
    // the voice section: synth engine → insert → mixer (family signal flow).
    // Constructed in the .cpp init list — needs PluginProcessor's complete type
    // for the derived→ProcessorBase& conversion (only forward-declared here).
    InsertSubsection insertSub;

    // ── Gating designer (full-width; Gap slider + Bypass button live inside it) ──
    GatingDesigner gatingDesigner;
    std::unique_ptr<APVTS::SliderAttachment> gapAttachment;
    std::unique_ptr<APVTS::ButtonAttachment> gateBypassAttachment;

    // The constructor in parts, top of the panel to bottom.
    void addLabel(juce::Label& l, const juce::String& text);
    void setupTonalCentre();
    void setupSourceControls();
    void setupFilterControls();
    void setupGateAndInsert();

    void rebindAttachments();

    // The current voice's manually-wired selector params, resolved on each rebind so the 30 Hz
    // timer reads them without building id strings / hash lookups (automation reverse sync).
    std::atomic<float>* fltTypeParam  = nullptr;
    std::atomic<float>* flt2TypeParam = nullptr;
    std::atomic<float>* o1WtParam     = nullptr;
    std::atomic<float>* o2WtParam     = nullptr;
    void bindModulationIndicators();

    // Wavetable selector dropdowns: rebuilt per voice (factory names + an optional
    // user-import item + "Load .wav…"). Selection routes through handleWtSelection.
    void refreshWavetableDropdowns();
    void handleWtSelection(int oscIndex, int itemId);
    std::unique_ptr<juce::FileChooser> wtChooser;   // kept alive during async load
    std::vector<juce::File> wtFolderFiles;          // dropdown id (kWtFolderBase+i) → Wavetables/ file

    // 30 Hz timer — drives the gating-grid playhead from the processor's transport beat position.
    void timerCallback() override;

    // Sub-panel geometry — populated by resized(), consumed by paint() for the
    // bordered sub-panels + their titles so layout + decoration stay in sync.
    juce::Rectangle<int> osc1PanelR, osc2PanelR, modNoisePanelR, filterPanelR, noisePanelR, insertPanelR;
    // The two metal panels filling the band: the voice (sections + Root / Scale) and the gate editor.
    juce::Rectangle<int> voiceR, gateR;

    // resized() in parts: the shared measures, then each band top to bottom.
    struct Grid { bool metal, screwed; int w, h, pad, inset, plateH, boxClear, gap, ddH, s2W, s2H, right; };
    Grid layoutGrid() const;
    int  layoutTonal(const Grid& g);                                   // Root / Scale; returns the section boxes' top
    void layoutSourceRow(const Grid& g, int top, int rowH);            // Osc 1 | Osc 2 | Noise
    void layoutShapingRow(const Grid& g, int top, int rowH);           // X-Mod | Filter | Effects
    void layoutXMod(const Grid& g, juce::Rectangle<int> box);
    void layoutFilter(const Grid& g, juce::Rectangle<int> box);
    void layoutGate(const Grid& g, int top);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PatternContent)
};

} // namespace mu_tant
