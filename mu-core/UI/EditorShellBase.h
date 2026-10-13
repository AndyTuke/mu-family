#pragma once

#include <atomic>
#include <juce_audio_processors/juce_audio_processors.h>
#include "Plugin/ProcessorBase.h"
#include "UI/Components/MuLookAndFeel.h"
#include "UI/Components/StatusBar.h"
#include "UI/TransportBar.h"
#include "UI/AboutOverlay.h"
#include "UI/ActivationOverlay.h"
#include "UI/SaveDialog.h"
#include "UI/PresetBrowser.h"
#include "UI/MidiPresetsOverlay.h"
#include "UI/MidiFullPresetsOverlay.h"
#include "UI/ShellChrome.h"
#include "UI/LayerSidebar.h"

// EditorShellBase — shared editor shell for every mu-family plugin.
//
// The processor-free chrome (LookAndFeel, TransportBar, StatusBar, About / Save / Preset
// Browser, overlay state machine, main-area layout, upgrade banner, UI-scale plumbing) is a
// mu_ui::ShellChrome helper parented to this editor; this class adds what needs a processor:
// the activation panel, the MIDI program-change overlays, the support prompt, the preset
// flow and the dirty tracking.
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
    // A mu-core MixerOverlay also gets its status messages forwarded to the StatusBar.
    void setMixerOverlay(juce::Component* overlay);

    // Show a panel's controls on the status bar (name + value, when hovered or turned): every
    // shared knob (KnobWithLabel), insert subsection and knob grid under `root` that has no
    // status handler of its own. Call once the panel's children exist.
    void forwardKnobStatus(juce::Component& root);
    // Pass nullptr to hide the gear button (mu-tant has no settings yet).
    void setSettingsOverlay(juce::Component* overlay);

    // The product's name on the shared chrome: transport logo, About panel (the family's
    // third-party credits plus `extraCredits`) and, for a licensed product, the activation panel.
    void setProductIdentity(const juce::String& displayName, const juce::StringArray& extraCredits = {});

    // The family's metal look (docs/design-ui-family.md §11): LCD dropdowns, engraved labels,
    // and the shared sidebar / modulation section as raised boxes on metal panels. Call at the
    // end of the product's constructor, once its children exist. `appAccent` is the product's
    // colour; leave it transparent to keep globalAccent.
    void setMetalStyle(bool metal, juce::Colour appAccent = juce::Colours::transparentBlack);
    // Screws style (metal only): cross-head screws in the corners of panels and sub-panels.
    void setScrews(bool on);

    // Direct access for the product to wire chrome (logo, About credits, etc.)
    // and additional callbacks.
    TransportBar&         getTransportBar()         { return chrome.getTransportBar(); }
    StatusBar&            getStatusBar()            { return chrome.getStatusBar(); }
    AboutOverlay&           getAboutPanel()           { return chrome.getAboutOverlay(); }
    ActivationOverlay&      getActivationPanel()      { return activationOverlay; }
    SaveDialog&           getSaveDialog()           { return chrome.getSaveDialog(); }
    PresetBrowser&        getPresetBrowser()        { return chrome.getPresetBrowser(); }
    MidiPresetsOverlay&     getMidiPresetsPanel()     { return midiPresetsOverlay; }
    MidiFullPresetsOverlay& getMidiFullPresetsPanel() { return midiFullPresetsOverlay; }

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

    // The polite pay-what-you-want prompt for an unlicensed copy (docs/design-ui-family.md).
    // `userRequested` is true from the About panel / status-bar link, which always show it
    // (without the "Don't show again" tick); false for the once-per-session auto-show.
    void showSupportPrompt(bool userRequested);

    bool isMixerVisible() const noexcept { return chrome.isMixerVisible(); }

    void paint(juce::Graphics& g) override;
    void resized() override;

    // juce::KeyListener — receives key events in standalone mode.
    bool keyPressed(const juce::KeyPress& key, juce::Component* originator) override;
    bool keyStateChanged(bool isKeyDown, juce::Component* originator) override;
    void parentHierarchyChanged() override;

    // Preset-dirty tracking — set whenever the APVTS tree changes; cleared
    // by the chrome after a successful load / save / new, or by the product.
    bool isPresetDirty() const noexcept { return chrome.isDirty(); }
    void clearPresetDirty() noexcept   { chrome.clearDirty(); }

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

    // The processor-free half of the shell, and reference aliases to its pieces under the names
    // the products have always used.
    mu_ui::ShellChrome chrome;
    MuLookAndFeel&  lookAndFeel;
    TransportBar&   transportBar;
    AboutOverlay&   aboutOverlay;
    SaveDialog&     saveDialog;
    PresetBrowser&  presetBrowser;
    StatusBar&      statusBar;

    // Performance mode: the shared sidebar (when the product mounts one), whose selection the mode
    // seeds and follows, and the interim "Performances" file chooser.
    juce::Component::SafePointer<LayerSidebar> layerSidebar;
    std::unique_ptr<juce::FileChooser>         performanceChooser;

    // The plugin-only screens, registered with the chrome as extra screens.
    ActivationOverlay      activationOverlay;
    MidiPresetsOverlay     midiPresetsOverlay;
    MidiFullPresetsOverlay midiFullPresetsOverlay;

    // Support prompt state. The first-show check runs once per editor, when it first shows.
    juce::String productDisplayName;
    bool supportDialogOpen    = false;
    bool supportPromptChecked = false;
    // Shows the status-bar Support link for an unlicensed copy and hides it for a supporter.
    void refreshSupportAffordances();
    void maybeShowSupportPromptOnStart();

    // juce::ValueTree::Listener — feeds the chrome's preset-dirty flag. JUCE can invoke these on
    // the audio thread (DAW automation writes APVTS there); the flag is atomic.
    void valueTreePropertyChanged(juce::ValueTree&, const juce::Identifier&) override { chrome.markDirty(); }
    void valueTreeChildAdded    (juce::ValueTree&, juce::ValueTree&)          override { chrome.markDirty(); }
    void valueTreeChildRemoved  (juce::ValueTree&, juce::ValueTree&, int)     override { chrome.markDirty(); }

private:
    // The constructor in parts.
    void wirePresetHooks();
    void wireOverlays();
    void wirePerformanceChooser();
    void setupBanners();
};
