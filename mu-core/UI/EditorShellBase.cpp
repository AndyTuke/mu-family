#include "EditorShellBase.h"
#include "UI/Components/KnobWithLabel.h"
#include "UI/Voice/InsertSubsection.h"
#include "UI/ParamKnobGrid.h"
#include "BuildNumber.h"        // BUILD_NUMBER — local version for the upgrade check
#include "UI/ConfirmDialog.h"   // shared themed confirm/prompt dialogs (mu_ui::confirmAsync)
#include "UI/StandardSettingsOverlay.h" // wires its program-change buttons
#include "UI/MixerOverlay.h"            // forwards its status messages
#include "Persistence/PresetFiles.h" // mu_pp::safePresetFileName (the processors' name rule), readPresetMeta

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

    wireTransportBar();
    wireOverlays();
    setupBanners();
    chrome.applyWindowSize();

    isStandalone = processorRef.wrapperType == juce::AudioProcessor::wrapperType_Standalone;
    loadKeybindings();
    if (isStandalone)
    {
        setWantsKeyboardFocus(true);
        addKeyListener(this);
        needsFocusGrab = true;
    }

    processorRef.apvts.state.addListener(this);
    presetDirty = false;

    chrome.startVersionCheck();
}

// A full preset chosen in the transport selector or the preset browser: load it and show it.
void EditorShellBase::loadPresetFromUi(const juce::File& f)
{
    processorRef.loadPreset(f);
    transportBar.setLoadedPreset(f);
    presetDirty = false;
    onPresetLoaded(f);
}

// Transport bar: mixer / settings / about toggles, preset select / save / new, status.
void EditorShellBase::wireTransportBar()
{
    // ── TransportBar callbacks (mixer / settings / about / status are the chrome's) ──
    transportBar.onPresetSelected = [this](const juce::File& f) { loadPresetFromUi(f); };

    transportBar.onSavePreset = [this]
    {
        showSaveDialog(true);
    };

    transportBar.onNewPreset = [this]
    {
        if (presetDirty)
        {
            juce::Component::SafePointer<EditorShellBase> safeThis(this);
            mu_ui::confirmAsync(this, "New Preset", "Discard unsaved changes?", "Discard",
                [safeThis] { if (safeThis) safeThis->doNewPreset(); });
            return;
        }
        doNewPreset();
    };

    processorRef.onSaveAndQuit = [this](std::function<void()> quitCallback)
    {
        pendingQuitCallback = std::move(quitCallback);
        showSaveDialog(true);
    };
}

// The overlays (About, Activation, Save, preset browser, MIDI panels) and the processor's
// load-error / swap-committed notifications.
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

    // ── Save dialog ─────────────────────────────────────────────────────────
    saveDialog.onSave = [this](const juce::String& name,
                               const juce::String& desc,
                               const juce::String& category,
                               bool embedSamples)
    {
        // "Save as Default" — overwrite _default unconditionally.
        if (saveDialog.isSaveAsDefault())
        {
            doSavePreset("_default", desc, category, embedSamples);
            return;
        }

        const juce::File destFile = fullPresetFileFor(name);

        if (destFile.existsAsFile())
        {
            juce::Component::SafePointer<EditorShellBase> safeThis(this);
            mu_ui::confirmAsync(this, "Overwrite Preset", "Overwrite \"" + name + "\"?", "Overwrite",
                [safeThis, n = name, d = desc, c = category, e = embedSamples]
                { if (safeThis) safeThis->doSavePreset(n, d, c, e); });
            return;
        }
        doSavePreset(name, desc, category, embedSamples);
    };
    saveDialog.onCancel = [this]
    {
        pendingQuitCallback = nullptr;
        showSaveDialog(false);
    };

    // ── Preset browser ──────────────────────────────────────────────────────
    chrome.onSaveDialogOpening = [this] { fillSaveDialog(); };
    presetBrowser.onLoadPreset = [this](const juce::File& f) { loadPresetFromUi(f); };

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
    // the ones it owns, and releases the look and feel, in its own destructor.)
    processorRef.apvts.state.removeListener(this);
    processorRef.onPresetSwapCommitted = nullptr;
    processorRef.onSaveAndQuit         = nullptr;

    if (isStandalone)
        removeKeyListener(this);
}

void EditorShellBase::setMainArea(juce::Component* newSidebar, juce::Component* newMainPanel)
{
    chrome.setMainArea(newSidebar, newMainPanel);
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
    }
}

void EditorShellBase::loadKeybindings()
{
    const juce::File bindingsFile = processorRef.getContentDir().getChildFile("keybindings.json");

    if (!bindingsFile.existsAsFile())
    {
        // Try to seed defaults — only possible if the content dir exists.
        if (bindingsFile.getParentDirectory().exists())
            bindingsFile.replaceWithText("{\n  \"play_stop\": \"space\"\n}\n");
        return;
    }

    const auto json = juce::JSON::parse(bindingsFile.loadFileAsString());
    if (auto* obj = json.getDynamicObject())
    {
        auto parseKey = [](const juce::String& s) -> juce::KeyPress
        {
            if (s.equalsIgnoreCase("space"))   return juce::KeyPress(juce::KeyPress::spaceKey);
            if (s.equalsIgnoreCase("return"))  return juce::KeyPress(juce::KeyPress::returnKey);
            if (s.equalsIgnoreCase("escape"))  return juce::KeyPress(juce::KeyPress::escapeKey);
            if (s.length() == 1)               return juce::KeyPress((int)s[0]);
            return {};
        };

        if (obj->hasProperty("play_stop"))
            keybindPlayStop = parseKey(obj->getProperty("play_stop").toString());
    }
}

bool EditorShellBase::keyPressed(const juce::KeyPress& key, juce::Component*)
{
    // Don't intercept when a text editor has focus — it needs the key for input.
    auto* focused = juce::Component::getCurrentlyFocusedComponent();
    if (dynamic_cast<juce::TextEditor*>(focused)) return false;

    if (key == keybindPlayStop)
    {
        processorRef.toggleInternalPlay();
        return true;
    }
    return false;
}

bool EditorShellBase::keyStateChanged(bool, juce::Component*) { return false; }

void EditorShellBase::parentHierarchyChanged()
{
    // Grab focus once on first show so Space works from launch without needing a mouse click.
    if (needsFocusGrab && isShowing())
    {
        needsFocusGrab = false;
        grabKeyboardFocus();
    }

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

    // "Don't show this again" row: only on the automatic prompt. A user-requested reopen
    // omits it, since they came here on purpose.
    enum { maybeLater = 0, enterKey = 1, pay = 2 };
    auto* dlg = new mu_ui::ModalDialog();
    juce::ToggleButton* hideTick = nullptr;
    dlg->width(400)
        .title("Support " + productDisplayName)
        .message(productDisplayName + " is free to use, with every feature. If it's useful to you, "
                 "you can pay what you like (suggested \xC2\xA3" "20, \xC2\xA3" "0 is fine). You'll get "
                 "a licence key, and it helps me keep building these.")
        .icon(mu_ui::ModalDialog::Icon::Info);
    if (! userRequested)
    {
        auto tick = std::make_unique<juce::ToggleButton>("Don't show this again");
        hideTick = tick.get();
        dlg->content(std::move(tick), 20);
    }
    dlg->button("Maybe later", maybeLater).button("Enter licence key", enterKey)
        .button("Pay what you like", pay, true).cancelResult(maybeLater);

    juce::Component::SafePointer<EditorShellBase> safe(this);
    juce::Component::SafePointer<juce::ToggleButton> safeTick(hideTick);
    dlg->onResult = [safe, safeTick](int result)
    {
        auto* shell = safe.getComponent();
        if (shell == nullptr) return;
        shell->supportDialogOpen = false;
        if (safeTick != nullptr && safeTick->getToggleState())
            shell->processorRef.setSupportPromptHidden(true);   // on any button, per the ruling
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

// Fills the save card as it opens: the known categories (browser + shared list + product panels)
// and the name / category / embed of the currently loaded preset.
void EditorShellBase::fillSaveDialog()
{
    presetBrowser.refresh(processorRef.getPresetsDir());
    // Merge categories from the browser + shared category list + product panels.
    juce::StringArray merged = presetBrowser.getCategories();
    for (const auto& c : processorRef.loadCategoryList())
        if (!merged.contains(c, false)) merged.add(c);
    for (const auto& c : getProductKnownCategories())
        if (!merged.contains(c, false)) merged.add(c);
    merged.sort(false);
    saveDialog.setKnownCategories(merged);
    onCategoriesRefreshed(merged);

    // Pre-fill name, category, and embed from the currently loaded preset.
    const juce::File loaded = transportBar.getLoadedPresetFile();
    if (loaded.existsAsFile())
    {
        saveDialog.setDefaultName(loaded.getFileNameWithoutExtension());
        const auto meta = mu_pp::readPresetMeta(loaded);
        saveDialog.setDefaultCategory(meta.category);
        saveDialog.setDefaultEmbed   (meta.embedSamples);
    }
}

juce::File EditorShellBase::fullPresetFileFor(const juce::String& name) const
{
    return processorRef.getPresetsDir().getChildFile(mu_pp::safePresetFileName(name, "Preset")
                                                     + "." + processorRef.getFullPresetExtension());
}

void EditorShellBase::doSavePreset(const juce::String& name, const juce::String& desc,
                                   const juce::String& category, bool embedSamples)
{
    processorRef.savePreset(name, desc, category, embedSamples);
    processorRef.ensureCategoryInList(category);
    transportBar.refreshPresets();
    const juce::File saved = fullPresetFileFor(name);
    transportBar.setLoadedPreset(saved);
    presetDirty = false;
    onCategoriesRefreshed(processorRef.loadCategoryList());
    onPresetSaved(saved);
    showSaveDialog(false);
    if (pendingQuitCallback)
    {
        auto cb = std::move(pendingQuitCallback);
        pendingQuitCallback = nullptr;
        cb();
    }
}

void EditorShellBase::doNewPreset()
{
    const juce::File defaultFile = processorRef.getDefaultPresetFile();
    if (!defaultFile.existsAsFile())
    {
        statusBar.showParam("New", "No default preset saved \xe2\x80\x94 use Save \xe2\x86\x92 Save as Default first",
                            MuLookAndFeel::colour(MuLookAndFeel::knobLevel));
        onPresetNew();
        return;
    }
    processorRef.loadPreset(defaultFile);
    transportBar.setLoadedPreset({});
    presetDirty = false;
    onPresetLoaded(defaultFile);
    onPresetNew();
}

void EditorShellBase::paint(juce::Graphics& g)
{
    chrome.paintBackground(g);
}

void EditorShellBase::resized()
{
    chrome.resized();
}
