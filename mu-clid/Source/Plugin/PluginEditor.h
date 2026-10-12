#pragma once

#include "PluginProcessor.h"
#include "UI/EditorShellBase.h"
#include "UI/MasterLoopSection.h"
#include "UI/LayerSidebar.h"
#include "UI/RhythmMiniVisual.h"
#include "UI/LayerPanel.h"        // mu-core shared per-layer panel shell
#include "UI/RhythmContent.h"
#include "Modulation/MuClidModDest.h"
#include "UI/MixerOverlay.h"
#include "UI/SettingsOverlay.h"

namespace mu_clid {

// μ-Clid editor: extends the shared mu-core shell with rhythm-specific
// components (sidebar, RhythmContent in the shared LayerPanel, MixerOverlay, SettingsOverlay) and the
// rhythm-set refresh wiring that drives them after preset loads / hot-swaps.
class PluginEditor : public EditorShellBase,
                     private juce::AudioProcessorValueTreeState::Listener
{
public:
    explicit PluginEditor(PluginProcessor&);
    ~PluginEditor() override;

    // ── EditorShellBase hooks ───────────────────────────────────────────────
    void onPresetLoaded(const juce::File& file) override;
    void onPresetSaved (const juce::File& file) override;
    void onPresetNew()                          override;
    juce::StringArray getProductKnownCategories() const override;
    void onCategoriesRefreshed(const juce::StringArray& merged) override;

private:
    PluginProcessor& proc;

    // mu-clid-specific product components — registered with the shell via
    // setMainArea / setMixerOverlay / setSettingsOverlay in the ctor.
    MasterLoopSection  masterLoop;
    LayerSidebar       sidebar;
    juce::String       effectSendName;   // mixer effect bus's current effect — names the effect-send target
    // mu-clid's modulation destinations, wrapping the kTable + populate logic; the LayerPanel's
    // shared modulator panel stays product-agnostic.
    ModDestProvider    modDestProvider = mu_clid::makeModDestProvider([this] { return effectSendName; });
    RhythmContent      rhythmContent;
    LayerPanel         layerPanel;
    MixerOverlay       mixerOverlay;
    SettingsOverlay    settingsOverlay;

    // consolidates the "refresh chrome after a rhythm-set mutation" boilerplate
    // that was repeated across 4 callbacks (preset load, new preset, sidebar reorder,
    // add rhythm). Each had its own subtly-different combination of refreshItems /
    // setSelectedIndex / setRhythm / mixerOverlay.refresh / mixerOverlay.loadFromApvts,
    // and missing any one (especially the mixer reload) was a silent-stale-state bug
    // waiting to happen.
    enum class MixerRefresh { Skip, RefreshOnly, FullReload };
    void selectRhythmAndRefresh(int idx,
                                bool fullSidebarRefresh,
                                MixerRefresh mixerRefresh);

    // APVTS listener — drives voice-section Amp "Effect" send label off the
    // global eff_algo parameter so host automation updates the label even
    // when the mixer overlay is hidden.
    void parameterChanged(const juce::String& parameterID, float newValue) override;
    void syncVoiceEffectSendLabel();
    void applyEffectSendLabel(const juce::String& name);   // voice band label + the effect-send mod target
    bool isRhythmMidiOutput(int r) const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginEditor)
};

} // namespace mu_clid
