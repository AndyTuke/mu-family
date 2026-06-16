#pragma once

#include <atomic>
#include <juce_audio_processors/juce_audio_processors.h>
#include "Plugin/ProcessorBase.h"
#include "UI/Components/MuLookAndFeel.h"
#include "UI/Components/StatusBar.h"
#include "UI/TransportBar.h"
#include "UI/AboutPanel.h"
#include "UI/ActivationPanel.h"
#include "UI/SaveDialog.h"
#include "UI/PresetBrowser.h"
#include "UI/MidiPresetsPanel.h"
#include "UI/MidiFullPresetsPanel.h"

// EditorShellBase — shared editor shell for every mu-family plugin.
//
// Owns the chrome (LookAndFeel, TransportBar, StatusBar, About / Save / Preset
// Browser / MIDI-Preset overlays, demo banner), the overlay state machine, the
// main-area layout (transport / sidebar / main / status / banner), key
// handling, and the UI-scale change plumbing.
//
// The product derives from this and supplies its own sidebar + main panel via
// setMainArea(), plus an optional mixer overlay + settings overlay. When no
// settings overlay is registered the gear button is hidden. Callbacks the
// shell exposes (preset save / new / load / settings toggle) are pre-wired —
// the product overrides the virtual on...() hooks to refresh its own panels.
class EditorShellBase : public juce::AudioProcessorEditor,
                        public juce::KeyListener,
                        private juce::ValueTree::Listener
{
public:
    explicit EditorShellBase(ProcessorBase& proc);
    ~EditorShellBase() override;

    // Product calls these in its constructor before children render. The shell
    // takes pointers, not ownership; the product owns the component instances.
    void setMainArea(juce::Component* sidebar, juce::Component* mainPanel);
    void setMixerOverlay(juce::Component* overlay);
    // Pass nullptr to hide the gear button (mu-tant has no settings yet).
    void setSettingsOverlay(juce::Component* overlay);

    // Direct access for the product to wire chrome (logo, About credits, etc.)
    // and additional callbacks.
    TransportBar&         getTransportBar()         { return transportBar; }
    StatusBar&            getStatusBar()            { return statusBar; }
    AboutPanel&           getAboutPanel()           { return aboutPanel; }
    ActivationPanel&      getActivationPanel()      { return activationPanel; }
    SaveDialog&           getSaveDialog()           { return saveDialog; }
    PresetBrowser&        getPresetBrowser()        { return presetBrowser; }
    MidiPresetsPanel&     getMidiPresetsPanel()     { return midiPresetsPanel; }
    MidiFullPresetsPanel& getMidiFullPresetsPanel() { return midiFullPresetsPanel; }

    // Overlay control — used by the shell's own callbacks and by the product
    // when it needs to drive overlay state from its own panels.
    void showMixer(bool show);
    void showAbout(bool show);
    void showActivation(bool show);
    void showSaveDialog(bool show);
    void showPresetBrowser(bool show);
    void showSettings(bool show);
    void showMidiPresets(bool show);
    void showMidiFullPresets(bool show);
    void hideAllOverlays();

    bool isMixerVisible() const noexcept { return mixerVisible; }

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;

    // juce::KeyListener — receives key events in standalone mode.
    bool keyPressed(const juce::KeyPress& key, juce::Component* originator) override;
    bool keyStateChanged(bool isKeyDown, juce::Component* originator) override;
    void parentHierarchyChanged() override;

    // Preset-dirty tracking — set whenever the APVTS tree changes; cleared
    // by the product after a successful load / save / new.
    // Atomic because JUCE can invoke ValueTree listeners on the audio thread
    // (DAW automation writes APVTS on the audio thread); read on message thread.
    bool isPresetDirty() const noexcept { return presetDirty.load(std::memory_order_relaxed); }
    void clearPresetDirty() noexcept   { presetDirty.store(false, std::memory_order_relaxed); }

    // ─── Product hooks (override to refresh product-specific UI) ─────────────
    // Fired after a preset is loaded from the browser, transport dropdown, or
    // the "New" flow's default-restore path.
    virtual void onPresetLoaded(const juce::File& /*file*/)  {}
    // Fired after a successful save — `file` is the newly-written preset path.
    virtual void onPresetSaved(const juce::File& /*file*/)   {}
    // Fired when "New" is clicked and no default preset exists (the shell
    // notifies via status bar); also fired after a successful default-restore.
    virtual void onPresetNew()                               {}
    // Categories known to the product's panels — merged into the SaveDialog
    // category dropdown.
    virtual juce::StringArray getProductKnownCategories() const { return {}; }
    // Called after the SaveDialog gathers + saves categories so the product
    // can refresh its own category-aware panels.
    virtual void onCategoriesRefreshed(const juce::StringArray& /*merged*/) {}

protected:
    ProcessorBase& processorRef;

    MuLookAndFeel        lookAndFeel;
    TransportBar         transportBar;
    AboutPanel           aboutPanel;
    ActivationPanel      activationPanel;
    SaveDialog           saveDialog;
    PresetBrowser        presetBrowser;
    MidiPresetsPanel     midiPresetsPanel;
    MidiFullPresetsPanel midiFullPresetsPanel;
    StatusBar            statusBar;
    juce::Label          demoBanner;
    juce::Label          upgradeBanner;   // shown when a newer release is available online

    juce::Component* sidebar         = nullptr;
    juce::Component* mainPanel       = nullptr;
    juce::Component* mixerOverlay    = nullptr;
    juce::Component* settingsOverlay = nullptr;

    static constexpr int kDemoBannerH    = 20;
    static constexpr int kUpgradeBannerH = 20;

    // Set true (message thread) once an online version check finds a newer release.
    std::atomic<bool> upgradeAvailable { false };
    // Kicks off the async GitHub "latest release" check; fail-silent + off-thread.
    void startVersionCheck();
    // The page opened when the upgrade banner is clicked.
    static constexpr const char* kDownloadUrl = "https://transwarp.me/download";

    bool mixerVisible           = false;
    bool aboutVisible           = false;
    bool activationVisible      = false;
    bool saveVisible            = false;
    bool browserVisible         = false;
    bool settingsVisible        = false;
    bool midiPresetsVisible     = false;
    bool midiFullPresetsVisible = false;

    juce::ComponentAnimator animator;
    void fadeSwitch(juce::Component* outgoing, juce::Component* incoming, int durationMs = 80);

    juce::TooltipWindow tooltipWindow { this, 700 };

    bool isStandalone   = false;
    bool needsFocusGrab = false;
    std::atomic<bool> presetDirty { false };
    std::function<void()> pendingQuitCallback;
    juce::KeyPress keybindPlayStop { juce::KeyPress::spaceKey };
    void loadKeybindings();

    // juce::ValueTree::Listener — tracks preset-dirty state.
    void valueTreePropertyChanged(juce::ValueTree&, const juce::Identifier&) override { presetDirty.store(true, std::memory_order_relaxed); }
    void valueTreeChildAdded    (juce::ValueTree&, juce::ValueTree&)          override { presetDirty.store(true, std::memory_order_relaxed); }
    void valueTreeChildRemoved  (juce::ValueTree&, juce::ValueTree&, int)     override { presetDirty.store(true, std::memory_order_relaxed); }

private:
    // Internal: actually run the save against the processor + refresh chrome.
    void doSavePreset(const juce::String& name, const juce::String& desc,
                      const juce::String& category, bool embedSamples);
    void doNewPreset();
};
