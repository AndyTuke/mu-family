#include "EditorShellBase.h"
#include "UI/Components/KnobWithLabel.h"
#include "UI/Voice/InsertSubsection.h"
#include "UI/ParamKnobGrid.h"
#include "UI/LayerPanel.h"              // its clip strip reports to the status bar
#include "UI/StandardSettingsOverlay.h" // wires its program-change buttons
#include "UI/MixerOverlay.h"            // forwards its status messages
#include "UI/ModalDialog.h"             // the support prompt

EditorShellBase::EditorShellBase(ProcessorBase& proc)
    : AudioProcessorEditor(&proc),
      processorRef(proc),
      // The chrome applies the stored UI scale first, before any child it owns is built.
      chrome(*this, proc),
      lookAndFeel(chrome.getLookAndFeel()),
      transportBar(chrome.getTransportBar()),
      aboutOverlay(chrome.getAboutOverlay()),
      saveDialog(chrome.getSaveDialog()),
      presetBrowser(chrome.getPresetBrowser()),
      statusBar(chrome.getStatusBar()),
      activationOverlay(proc),
      midiPresetsOverlay(proc),
      midiFullPresetsOverlay(proc)
{
    // The plugin-only screens join the chrome's state machine and layout, at their original z-order.
    chrome.addExtraScreen(activationOverlay,      mu_ui::ShellChrome::ExtraKind::Modal, &saveDialog);
    chrome.addExtraScreen(midiPresetsOverlay,     mu_ui::ShellChrome::ExtraKind::Page,  &statusBar);
    chrome.addExtraScreen(midiFullPresetsOverlay, mu_ui::ShellChrome::ExtraKind::Page,  &statusBar);

    wirePresetHooks();
    wireOverlays();
    setupBanners();
    chrome.applyWindowSize();

    processorRef.apvts.state.addListener(this);
    chrome.clearDirty();

    chrome.startVersionCheck();
}

// The product's reactions to the chrome's preset flow, fired at the moments the chrome reaches them.
void EditorShellBase::wirePresetHooks()
{
    chrome.onPresetLoaded        = [this](const juce::File& f) { onPresetLoaded(f); };
    chrome.onPresetSaved         = [this](const juce::File& f) { onPresetSaved(f); };
    chrome.onPresetNew           = [this] { onPresetNew(); };
    chrome.getProductCategories  = [this] { return getProductKnownCategories(); };
    chrome.onCategoriesRefreshed = [this](const juce::StringArray& merged) { onCategoriesRefreshed(merged); };
}

// The overlays only a processor-backed editor has (About's support state, Activation, the MIDI
// program-change tables) and the settings page links to them.
void EditorShellBase::wireOverlays()
{
    // ── About panel ─────────────────────────────────────────────────────────
    chrome.onAboutOpening = [this]
    {
        aboutOverlay.setSupportState(! processorRef.hasLicensing() ? AboutOverlay::SupportState::None
                                     : processorRef.isSupporter()   ? AboutOverlay::SupportState::Supporter
                                                                    : AboutOverlay::SupportState::Unlicensed);
    };
    aboutOverlay.onSupport  = [this] { showAbout(false); showSupportPrompt(true); };
    aboutOverlay.onEnterKey = [this] { showAbout(false); showActivation(true); };
    statusBar.onSupportClicked = [this] { showSupportPrompt(true); };

    // ── Activation overlay (non-blocking — Demo until activated) ──────────────
    activationOverlay.onDismiss   = [this] { showActivation(false); };
    activationOverlay.onActivated = [this]
    {
        // A key was just activated — show the supporter affordances and relayout.
        showActivation(false);
        refreshSupportAffordances();
        resized();
        repaint();
    };

    // The Save card, the preset browser and the processor's load / swap notifications are the chrome's.
    midiPresetsOverlay.onClose     = [this] { showMidiPresets(false);     showSettings(true); };
    midiFullPresetsOverlay.onClose = [this] { showMidiFullPresets(false); showSettings(true); };

    // Deferred full-preset swaps commit at the loop boundary — refresh chrome
    // once they land. Product overrides onPresetLoaded to refresh its panels.
    processorRef.onPresetSwapCommitted = [this]
    {
        onPresetLoaded(transportBar.getLoadedPresetFile());
    };
}

// The status-bar Support link (unlicensed copies only).
void EditorShellBase::setupBanners()
{
    refreshSupportAffordances();
}

EditorShellBase::~EditorShellBase()
{
    // Clear every processor callback before teardown — processor outlives the
    // editor in DAW close-window-keep-plugin scenarios; any deferred invocation
    // into a destroyed editor is a UAF. All lambdas captured raw [this]. (The chrome clears
    // the ones it owns, removes its key listener and releases the look and feel in its own
    // destructor.)
    processorRef.apvts.state.removeListener(this);
}

void EditorShellBase::setMainArea(juce::Component* newSidebar, juce::Component* newMainPanel)
{
    chrome.setMainArea(newSidebar, newMainPanel);

    // Performance mode: the shared sidebar follows the live layer, and its layer indicator stays
    // reachable through the shield; the mode takes the editor's current layer as its starting point.
    if (auto* shared = dynamic_cast<LayerSidebar*>(newSidebar))
    {
        layerSidebar = shared;
        shared->setLiveView(processorRef.supportsPerformanceMode() ? processorRef.getLiveView() : nullptr);
        chrome.addShieldExemption(shared->getLayerIndicator());
        transportBar.getEditorLayer = [this] { return layerSidebar != nullptr ? layerSidebar->getSelectedIndex() : 0; };
    }

    // The layer panel's clip strip talks to the status bar (hover text, Stored / Cleared / Queued / Mute lines).
    if (auto* panel = dynamic_cast<LayerPanel*>(newMainPanel))
        panel->getHeader().getClipStrip().onStatusUpdate = [this](const juce::String& name, const juce::String& value)
        { statusBar.showParam(name, value); };
}

void EditorShellBase::forwardKnobStatus(juce::Component& root)
{
    // The chrome knows the plain knobs; these two panel types are plugin-side.
    chrome.forwardKnobStatus(root, [](juce::Component& c, const mu_ui::ShellChrome::StatusFn& show)
    {
        if (auto* ins = dynamic_cast<InsertSubsection*>(&c))
        {
            if (! ins->onStatusUpdate) ins->onStatusUpdate = show;
            return true;
        }
        if (auto* grid = dynamic_cast<mu_ui::ParamKnobGrid*>(&c))
        {
            if (! grid->onStatusUpdate) grid->onStatusUpdate = show;
            return true;
        }
        return false;
    });
}

void EditorShellBase::setMixerOverlay(juce::Component* overlay)
{
    chrome.setMixerOverlay(overlay);
    // A mu-core MixerOverlay also reports its status messages to the status bar.
    if (auto* mixer = dynamic_cast<MixerOverlay*>(overlay))
        mixer->onStatusUpdate = [this](const juce::String& name, const juce::String& val, juce::Colour col)
        { statusBar.showParam(name, val, col); };
}

void EditorShellBase::setMetalStyle(bool metal, juce::Colour appAccent)
{
    chrome.setMetalStyle(metal, appAccent);
}

void EditorShellBase::setProductIdentity(const juce::String& displayName, const juce::StringArray& extraCredits)
{
    // Third-party code every product ships (Monocypher only with the licence verifier).
    const auto credit = [](const char* utf8) { return juce::String(juce::CharPointer_UTF8(utf8)); };
    juce::StringArray credits { credit("JUCE \xe2\x80\x94 AGPLv3"),
                                credit("Signalsmith Reverb \xe2\x80\x94 MIT") };
    if (processorRef.hasLicensing())
        credits.add(credit("Monocypher \xe2\x80\x94 BSD-2-Clause"));
    credits.add(credit("clap-juce-extensions \xe2\x80\x94 MIT"));
    credits.addArray(extraCredits);

    productDisplayName = displayName;
    aboutOverlay.setProductInfo(displayName, credits);
    transportBar.setLogoText(displayName);
    if (processorRef.hasLicensing())
        activationOverlay.setProductName(displayName);
}

void EditorShellBase::setScrews(bool on)
{
    chrome.setScrews(on);
}

void EditorShellBase::setSettingsOverlay(juce::Component* overlay)
{
    chrome.setSettingsOverlay(overlay);

    // The standard page's program-change buttons swap it for the matching program-change table.
    if (auto* standard = dynamic_cast<mu_ui::StandardSettingsOverlay*>(overlay))
    {
        standard->onMidiPresetsClicked = [this] { showSettings(false); showMidiPresets(true); };
        standard->onFullPresetsClicked = [this] { showSettings(false); showMidiFullPresets(true); };
        standard->onStatusUpdate = [this](const juce::String& name, const juce::String& value)
        { if (transportBar.onStatusUpdate) transportBar.onStatusUpdate(name, value); };
    }
}

bool EditorShellBase::keyPressed(const juce::KeyPress& key, juce::Component* originator)
{
    return chrome.keyPressed(key, originator);
}

bool EditorShellBase::keyStateChanged(bool isKeyDown, juce::Component* originator)
{
    return chrome.keyStateChanged(isKeyDown, originator);
}

void EditorShellBase::parentHierarchyChanged()
{
    chrome.parentHierarchyChanged();

    // Offer the support prompt once the editor is first on screen.
    if (! supportPromptChecked && isShowing())
        maybeShowSupportPromptOnStart();
}

void EditorShellBase::refreshSupportAffordances()
{
    statusBar.setSupportLinkVisible(processorRef.hasLicensing() && ! processorRef.isSupporter());
}

// Policy: an unlicensed, un-hidden copy asks once per process (mu_core::support_prompt). A short
// delay lets the editor settle first, and the prompt waits if another overlay is already open.
void EditorShellBase::maybeShowSupportPromptOnStart()
{
    supportPromptChecked = true;
    namespace sp = mu_core::support_prompt;
    if (! sp::shouldShowNow(processorRef.hasLicensing(), processorRef.isSupporter(),
                            processorRef.isSupportPromptHidden()))
        return;

    juce::Component::SafePointer<EditorShellBase> safe(this);
    juce::Timer::callAfterDelay(1500, [safe]
    {
        if (auto* shell = safe.getComponent())
            if (shell->isShowing())
                shell->showSupportPrompt(false);
    });
}

void EditorShellBase::showSupportPrompt(bool userRequested)
{
    namespace sp = mu_core::support_prompt;
    if (supportDialogOpen || ! processorRef.hasLicensing() || processorRef.isSupporter())
        return;

    // The automatic prompt never interrupts: skip it if an overlay is up or it has already
    // been shown or switched off meanwhile.
    if (! userRequested)
    {
        if (chrome.anyOverlayVisible()
            || ! sp::shouldShowNow(true, false, processorRef.isSupportPromptHidden()))
            return;
        sp::markShown();
    }

    supportDialogOpen = true;

    // There is no way to switch the prompt off for good: Maybe later only dismisses it for this session.
    enum { maybeLater = 0, enterKey = 1, pay = 2 };
    auto* dlg = new mu_ui::ModalDialog();
    dlg->width(400)
        .title("Support " + productDisplayName)
        .message(productDisplayName + " is free to use, with every feature. If it's useful to you, "
                 "you can pay what you like (suggested \xC2\xA3" "20, \xC2\xA3" "0 is fine). You'll get "
                 "a licence key, and it helps me keep building these.")
        .icon(mu_ui::ModalDialog::Icon::Info);
    dlg->button("Maybe later", maybeLater).button("Enter licence key", enterKey)
        .button("Pay what you like", pay, true).cancelResult(maybeLater);

    juce::Component::SafePointer<EditorShellBase> safe(this);
    dlg->onResult = [safe](int result)
    {
        auto* shell = safe.getComponent();
        if (shell == nullptr) return;
        shell->supportDialogOpen = false;
        if (result == enterKey)
            shell->showActivation(true);
        else if (result == pay)
        {
            const auto url = shell->processorRef.getCheckoutUrl();
            if (url.isNotEmpty())
                juce::URL(url).launchInDefaultBrowser();
        }
    };
    dlg->show(this);
}

void EditorShellBase::hideAllOverlays()            { chrome.hideAllOverlays(); }
void EditorShellBase::showMixer(bool show)         { chrome.showMixer(show); }
void EditorShellBase::showAbout(bool show)         { chrome.showAbout(show); }
void EditorShellBase::showActivation(bool show)    { chrome.showExtraScreen(activationOverlay, show); }
void EditorShellBase::showSaveDialog(bool show)    { chrome.showSaveDialog(show); }
void EditorShellBase::showPresetBrowser(bool show) { chrome.showPresetBrowser(show); }
void EditorShellBase::showSettings(bool show)      { chrome.showSettings(show); }
void EditorShellBase::showMidiPresets(bool show)   { chrome.showExtraScreen(midiPresetsOverlay, show); }
void EditorShellBase::showMidiFullPresets(bool show) { chrome.showExtraScreen(midiFullPresetsOverlay, show); }

void EditorShellBase::paint(juce::Graphics& g)
{
    chrome.paintBackground(g);
}

void EditorShellBase::resized()
{
    chrome.resized();
}
