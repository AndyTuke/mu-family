#include "PluginEditor.h"
#include "Audio/FX/Slots/FXAlgorithmDef.h"
#include <BinaryData.h>

namespace mu_clid {

PluginEditor::PluginEditor(PluginProcessor& p)
    : EditorShellBase(p),
      proc(p),
      masterLoop(p),
      sidebar(p, "Rhythm"),
      rhythmContent(p, p),
      layerPanel(p, "Rhythm"),
      layerEdit(p, sidebar, layerPanel,
                { [this] { rhythmContent.setLayer(-1); },   // nothing stays bound into a layer that is about to move
                  [this](int idx) { selectRhythmAndRefresh(idx, /*fullSidebarRefresh=*/false, MixerRefresh::FullReload); } }),
      mixerOverlay(p, p.mixerEngine),
      settingsOverlay(p)
{
    // ── Product chrome on shared overlays ───────────────────────────────────
    setProductIdentity(juce::String(juce::CharPointer_UTF8("\xce\xbc-Clid")),
                       juce::StringArray { juce::String(juce::CharPointer_UTF8("Bj\xc3\xb6rklund algorithm \xe2\x80\x94 public domain")) });
    getSaveDialog().setLogoImage(juce::ImageCache::getFromMemory(BinaryData::muclid_png,
                                                                  BinaryData::muclid_pngSize));
    // mu-Clid has no bar-line rule, so it offers the two loop rules only.
    masterLoop.setSwapTimingChoices({ { mu_hotswap::SwapTiming::OnMasterLoop, "Master loop" },
                                      { mu_hotswap::SwapTiming::OnLayerLoop,  "Rhythm loop" } });
    getTransportBar().setLoopSection(&masterLoop, masterLoop.getPreferredWidth());
    masterLoop.onStatusUpdate = [this](const juce::String& name, const juce::String& val)
    {
        getStatusBar().showParam(name, val);
    };

    // ── Product main area + overlays ────────────────────────────────────────
    setMainArea(&sidebar, &layerPanel);
    setMixerOverlay(&mixerOverlay);
    setSettingsOverlay(&settingsOverlay);

    // ── The shared layer panel: header strip + the rhythm band + modulators ──
    // mu-Clid keeps its own categorised preset list and save card in the header, so the shell's
    // list is off; a layer change rebinds the band, and a reset / preset load asks for a full rebind.
    layerPanel.setOwnsPresetList(false);
    layerPanel.setContent(&rhythmContent);
    layerPanel.setDestProvider(&modDestProvider);
    layerPanel.setBeatSource([this] { return proc.lastBeatPos.load(); });
    layerPanel.addOverlay(rhythmContent.getSaveDialog());
    rhythmContent.attachHeader(layerPanel.getHeader());
    layerPanel.onLayerChanged = [this](int idx) { rhythmContent.setLayer(idx); };
    rhythmContent.requestRebind = [this](int idx) { layerPanel.setLayer(idx); };
    rhythmContent.onInsertAlgorithmChanged = [this](int charId)
    {
        layerPanel.getModulatorPanel().setInsertAlgorithm(charId);
    };

    // ── Sidebar callbacks ───────────────────────────────────────────────────
    // Per-rhythm mini-graphic: a RhythmCircle whose step hit flashes the sidebar item.
    sidebar.createMiniVisual = [this](int i, std::function<void()> pulse) -> std::unique_ptr<juce::Component>
    {
        auto mv = std::make_unique<RhythmMiniVisual>(proc, i);
        mv->onHit = std::move(pulse);
        return mv;
    };
    sidebar.refreshItems();

    sidebar.onLayerSelected = [this](int idx)
    {
        layerPanel.setLayer(idx);
        if (!isMixerVisible())
        {
            hideAllOverlays();
            layerPanel.setVisible(true);
        }
    };

    // Add, delete and reorder are wired by layerEdit (the shared LayerEditController).

    // ── RhythmContent callbacks ─────────────────────────────────────────────
    rhythmContent.onStatusUpdate = [this](const juce::String& name,
                                        const juce::String& val,
                                        juce::Colour col)
    {
        getStatusBar().showParam(name, val, col);
    };

    // Mirror the mixer's effect-slot algorithm name on the voice section's
    // Amp "Effect" send knob so the user can read it without opening the
    // mixer overlay. Two paths feed this: (a) the mixer's own dropdown
    // (synchronous from updateEffectSendLabels), (b) host automation of
    // eff_algo (asynchronous via the APVTS listener registered below — fires
    // regardless of mixer visibility).
    mixerOverlay.onEffectAlgorithmNameChanged = [this](const juce::String& name)
    {
        applyEffectSendLabel(name);
    };
    proc.apvts.addParameterListener("eff_algo", this);

    // A rhythm in MIDI output mode makes no audio, so its mixer strip is drawn inactive. The strip is
    // refreshed when the shown rhythm's mode changes and, for host automation of any rhythm, below.
    mixerOverlay.isChannelSilent = [this](int r) { return isRhythmMidiOutput(r); };
    rhythmContent.onOutputModeChanged = [this] { if (isMixerVisible()) mixerOverlay.loadFromApvts(); };
    for (int i = 0; i < mu_limits::kMaxLayers; ++i)
        proc.apvts.addParameterListener("r" + juce::String(i) + "_outMode", this);
    startTimerHz(mu_ui::kUiRefreshHz);   // drains the flags the parameter listener raises

    rhythmContent.onRhythmRenamed = [this]
    {
        sidebar.repaintItems();
        mixerOverlay.refreshSidechainSources();
        if (isMixerVisible()) mixerOverlay.refresh();
    };

    proc.onLayerPresetCommitted = [this](int r)
    {
        if (r == layerPanel.getLayer())
            layerPanel.setLayer(r);
        sidebar.repaintItems();
        mixerOverlay.refreshSidechainSources();
        if (isMixerVisible()) mixerOverlay.refresh();
    };

    // ── Settings overlay ────────────────────────────────────────────────────
    settingsOverlay.onContentDirChanged = [this] { getTransportBar().refreshPresets(); };

    // ── Startup ─────────────────────────────────────────────────────────────
    layerPanel.setLayer(0);
    sidebar.setSelectedIndex(0);

    // Sync mixer UI from APVTS — in standalone, state is restored before the editor
    // is created, so without this call the scSourceBox and other controls would show
    // defaults rather than the restored values.
    mixerOverlay.loadFromApvts();
    clearPresetDirty();

    // The family metal look, in mu-Clid's purple (globalAccent).
    setMetalStyle(true, MuLookAndFeel::colour(MuLookAndFeel::appPurple));
    setScrews(true);
}

PluginEditor::~PluginEditor()
{
    // Clear product-owned processor callbacks before teardown — same UAF
    // contract as the shell base's destructor.
    stopTimer();
    proc.apvts.removeParameterListener("eff_algo", this);
    for (int i = 0; i < mu_limits::kMaxLayers; ++i)
        proc.apvts.removeParameterListener("r" + juce::String(i) + "_outMode", this);
    proc.onLayerPresetCommitted = nullptr;
}

void PluginEditor::selectRhythmAndRefresh(int idx,
                                          bool fullSidebarRefresh,
                                          MixerRefresh mixerRefresh)
{
    if (fullSidebarRefresh)
        sidebar.refreshItems();
    sidebar.setSelectedIndex(idx);
    layerPanel.setLayer(idx);
    if (isMixerVisible())
    {
        if (mixerRefresh != MixerRefresh::Skip)
            mixerOverlay.refresh();
        if (mixerRefresh == MixerRefresh::FullReload)
            mixerOverlay.loadFromApvts();
    }
}

// ── EditorShellBase hooks ───────────────────────────────────────────────────
void PluginEditor::onPresetLoaded(const juce::File& /*file*/)
{
    selectRhythmAndRefresh(0, /*fullSidebarRefresh=*/true, MixerRefresh::FullReload);
}

void PluginEditor::onPresetSaved(const juce::File& /*file*/)
{
    // Save doesn't change the rhythm set — no refresh needed beyond what the
    // shell already does (transport preset dropdown + dirty flag).
}

void PluginEditor::onPresetNew()
{
    selectRhythmAndRefresh(0, /*fullSidebarRefresh=*/true, MixerRefresh::FullReload);
}

juce::StringArray PluginEditor::getProductKnownCategories() const
{
    return rhythmContent.getKnownCategories();
}

void PluginEditor::onCategoriesRefreshed(const juce::StringArray& merged)
{
    rhythmContent.setKnownCategories(merged);
}

// ── APVTS listener — host automation of eff_algo and the rhythms' output modes ──
// Fires from any thread, the audio thread under DAW automation: no allocation, no UI work. It raises a
// flag and timerCallback refreshes the UI on the message thread.
void PluginEditor::parameterChanged(const juce::String& parameterID, float)
{
    // A rhythm's output mode (Sample / MIDI) decides whether its mixer strip is drawn active.
    if (parameterID.endsWith("_outMode"))
        outModeDirty.store(true, std::memory_order_release);
    else if (parameterID == "eff_algo")
        effAlgoDirty.store(true, std::memory_order_release);
}

void PluginEditor::timerCallback()
{
    if (outModeDirty.exchange(false, std::memory_order_acq_rel) && isMixerVisible())
        mixerOverlay.loadFromApvts();
    if (effAlgoDirty.exchange(false, std::memory_order_acq_rel))
        syncVoiceEffectSendLabel();
}

// True when rhythm `r` is set to send MIDI notes instead of playing its sample.
bool PluginEditor::isRhythmMidiOutput(int r) const
{
    if (auto* raw = proc.apvts.getRawParameterValue("r" + juce::String(r) + "_outMode"))
        return raw->load() > 0.5f;
    return false;
}

void PluginEditor::syncVoiceEffectSendLabel()
{
    const auto& algos = FXAlgorithmRegistry::effectAlgorithms();
    const int ai = proc.fxChain.effectSlot().getAlgorithmIndex();
    const juce::String name = (ai >= 0 && ai < (int) algos.size())
                              ? algos[(size_t) ai].name
                              : juce::String("Effect");
    applyEffectSendLabel(name);
}

// The voice band's Amp "Effect" send label, and the effect-send modulation target's name (the
// destination dropdowns are rebuilt so the target carries the new name).
void PluginEditor::applyEffectSendLabel(const juce::String& name)
{
    rhythmContent.setVoiceEffectSendLabel(name);
    if (name == effectSendName) return;
    effectSendName = name;
    layerPanel.setDestProvider(&modDestProvider);
}

juce::AudioProcessorEditor* PluginProcessor::createEditor()
{
    return new PluginEditor(*this);
}

} // namespace mu_clid
