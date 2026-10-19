#include "ShellChrome.h"
#include "UI/Components/KnobWithLabel.h"
#include "UI/SettingsOverlayBase.h"
#include "UI/ConfirmDialog.h"           // shared themed confirm dialogs (mu_ui::confirmAsync)
#include "UI/ModalDialog.h"
#include "Persistence/PresetFiles.h"    // mu_pp::safePresetFileName, readPresetMeta
#include "UI/PerformanceChooserModel.h" // perf::performancesDir
#include "BuildNumber.h"        // BUILD_NUMBER, MU_VERSION_STRING: local version for the update check

namespace mu_ui
{

ShellChrome::ScaleInit::ScaleInit(mu_core::ShellBackend& b)
{
    mu_ui::scale = juce::jlimit(mu_core::ShellBackend::kUiScaleMedium,
                                mu_core::ShellBackend::kUiScaleLarge,
                                b.getUiScale());
}

ShellChrome::ShellChrome(juce::Component& hostComponent, mu_core::ShellBackend& shellBackend)
    : host(hostComponent),
      backend(shellBackend),
      scaleInit(shellBackend),
      transportBar(shellBackend),
      tooltipWindow(&hostComponent, 700)
{
    wireBuiltIns();
    wirePerformanceChooser();
    wirePresetFlow();
    installKeys();

    // Performance mode: the shield goes above everything; the toggle and the preset indicator stay
    // reachable through it. The live view is watched whenever the backend has one, and the mode is
    // acted on only when the backend supports it.
    host.addChildComponent(frame);
    host.addChildComponent(shield);
    shield.addExempt(transportBar.getPerformToggle());
    shield.addExempt(transportBar.getStepIndicator());
    // Play/Stop stays live in the mode by pointer only; the keyboard is fully locked.
    shield.addExempt(transportBar.getPlayButton());
    liveView = shellBackend.getLiveView();
    if (liveView != nullptr) liveView->addListener(this);
    syncPerformanceMode();
}

ShellChrome::~ShellChrome()
{
    if (liveView != nullptr) liveView->removeListener(this);

    // The backend can outlive the host (a DAW closes the window and keeps the plugin), so no
    // callback into this object may survive it.
    backend.onLoadError           = nullptr;
    backend.onUiScaleChanged      = nullptr;
    backend.onSaveAndQuit         = nullptr;
    backend.onPresetSwapCommitted = nullptr;
    host.removeKeyListener(this);
    host.setLookAndFeel(nullptr);
}

// Parents the built-in pieces to the host in their fixed z-order and wires their callbacks.
void ShellChrome::wireBuiltIns()
{
    host.setLookAndFeel(&lookAndFeel);

    host.addAndMakeVisible(transportBar);
    host.addChildComponent(aboutOverlay);
    host.addChildComponent(saveDialog);
    host.addChildComponent(presetBrowser);
    host.addAndMakeVisible(statusBar);

    // Transport bar: mixer / settings / about toggles and the status line.
    transportBar.onMixerToggle    = [this] { showMixer(! mixerVisible); };
    transportBar.onLogoClicked    = [this] { showAbout(true); };
    transportBar.onSettingsToggle = [this] { showSettings(! settingsVisible); };
    transportBar.onStatusUpdate   = [this](const juce::String& name, const juce::String& val)
    {
        statusBar.showParam(name, val);
    };

    aboutOverlay.onDismiss = [this] { showAbout(false); };

    presetBrowser.setFileExtension(backend.getFullPresetExtension());
    // Closing the browser after a pick (or a cancelled pick) goes back to the chooser that asked.
    presetBrowser.onClose = [this]
    {
        const bool backToChooser = returnToChooser;
        showPresetBrowser(false);
        if (backToChooser) showExtraScreen(performanceChooser, true);
    };

    // Surface preset / state-load failures to the user.
    backend.onLoadError = [this](const juce::String& msg)
    {
        statusBar.showParam("Load failed", msg, MuLookAndFeel::colour(MuLookAndFeel::knobLevel));
    };
}

// The Performance chooser joins the state machine as a page over the main area, and its entry points
// are wired where the mode is supported.
void ShellChrome::wirePerformanceChooser()
{
    addExtraScreen(performanceChooser, ExtraKind::Page, &statusBar);

    performanceChooser.onClose  = [this] { showExtraScreen(performanceChooser, false); };
    performanceChooser.onStatus = [this](const juce::String& name, const juce::String& value)
    {
        statusBar.showParam(name, value);
    };
    performanceChooser.onPickPreset = [this](std::function<void(const juce::File&)> picked)
    {
        pickPresetForChooser(std::move(picked));
    };
    performanceChooser.onUse = [this](mu_pp::PerformanceFile list, const juce::File& where)
    {
        return backend.loadPerformance(std::move(list), where);
    };

    if (! backend.supportsPerformanceMode()) return;
    presetBrowser.onChoosePerformance = [this] { showPerformanceChooser(); };
    presetBrowser.refreshFooter();
    transportBar.onChoosePerformance = [this] { showPerformanceChooser(); };
    transportBar.refreshPresets();   // the dropdown gains its "Performances..." entry
}

void ShellChrome::showPerformanceChooser()
{
    if (! backend.supportsPerformanceMode()) return;
    if (liveView != nullptr && liveView->isPerformanceMode()) return;   // the PC is locked in the mode
    if (isExtraScreenVisible(performanceChooser)) return;               // already editing: keep the session

    showExtraScreen(performanceChooser, true);

    // The session starts from the list in use, or a new one when none is chosen.
    const mu_pp::PerformanceFile none;
    performanceChooser.beginSession(perf::performancesDir(backend.getContentDir()), backend.getFullPresetDir(),
                                    backend.getProductStateId(),
                                    liveView != nullptr ? liveView->getPerformance() : none,
                                    liveView != nullptr ? liveView->getPerformanceFile() : juce::File());
}

// The chooser asked for a preset: show the browser in pick mode. Hiding the chooser to do so keeps
// its edits (they live in the chooser's model); the browser's close returns to it.
void ShellChrome::pickPresetForChooser(std::function<void(const juce::File&)> picked)
{
    showPresetBrowser(true);
    returnToChooser = true;
    presetBrowser.beginPick(std::move(picked));
    statusBar.showParam("Performance", "Pick a preset");
}

// Fixed window size from the UI scale, re-applied when the scale changes in settings.
void ShellChrome::applyWindowSize()
{
    host.setSize(mu_ui::s(MuLookAndFeel::kWindowWidth), mu_ui::s(MuLookAndFeel::kWindowHeight));

    // React to runtime scale changes from the settings overlay.
    juce::Component::SafePointer<juce::Component> safeHost(&host);
    backend.onUiScaleChanged = [safeHost](float newScale)
    {
        if (auto* h = safeHost.getComponent())
        {
            mu_ui::scale = newScale;
            h->setSize(mu_ui::s(MuLookAndFeel::kWindowWidth), mu_ui::s(MuLookAndFeel::kWindowHeight));
            h->sendLookAndFeelChange();
            h->resized();
            h->repaint();
        }
    };
}

void ShellChrome::startVersionCheck() { updateNotice.start(); }

void ShellChrome::setMainArea(juce::Component* newSidebar, juce::Component* newMainPanel)
{
    if (sidebar != nullptr)   host.removeChildComponent(sidebar);
    if (mainPanel != nullptr) host.removeChildComponent(mainPanel);
    sidebar   = newSidebar;
    mainPanel = newMainPanel;
    if (sidebar != nullptr)   host.addAndMakeVisible(*sidebar);
    if (mainPanel != nullptr) host.addAndMakeVisible(*mainPanel);
    // The main page's knobs take the app colour (the mixer and other overlays keep theirs).
    if (mainPanel != nullptr) MuLookAndFeel::useAppColourKnobs(*mainPanel);
    raiseShield();
    host.resized();
}

void ShellChrome::setMixerOverlay(juce::Component* overlay)
{
    if (mixerOverlay != nullptr) host.removeChildComponent(mixerOverlay);
    mixerOverlay = overlay;
    if (mixerOverlay != nullptr)
    {
        host.addChildComponent(*mixerOverlay);
        transportBar.setShowMixerToggle(true);
    }
    else
    {
        transportBar.setShowMixerToggle(false);
    }
    raiseShield();
    host.resized();
}

void ShellChrome::setSettingsOverlay(juce::Component* overlay)
{
    if (settingsOverlay != nullptr) host.removeChildComponent(settingsOverlay);
    settingsOverlay = overlay;
    if (settingsOverlay != nullptr)
        host.addChildComponent(*settingsOverlay);

    // Standard overlay chrome: close returns to the main view.
    if (auto* base = dynamic_cast<SettingsOverlayBase*>(overlay))
        base->onClose = [this] { showSettings(false); };
    raiseShield();
    host.resized();
}

ShellChrome::Extra* ShellChrome::findExtra(const juce::Component& screen)
{
    for (auto& e : extras)
        if (e.screen == &screen) return &e;
    return nullptr;
}

const ShellChrome::Extra* ShellChrome::findExtra(const juce::Component& screen) const
{
    for (auto& e : extras)
        if (e.screen == &screen) return &e;
    return nullptr;
}

void ShellChrome::addExtraScreen(juce::Component& screen, ExtraKind kind, juce::Component* insertBefore)
{
    jassert(findExtra(screen) == nullptr);
    extras.push_back({ &screen, kind, false });
    const int index = insertBefore != nullptr ? host.getIndexOfChildComponent(insertBefore) : -1;
    host.addChildComponent(screen, index);
    raiseShield();
}

bool ShellChrome::isExtraScreenVisible(const juce::Component& screen) const
{
    const auto* e = findExtra(screen);
    return e != nullptr && e->visible;
}

// A Modal toggles in place (and takes focus); a Page replaces the main panel.
void ShellChrome::showExtraScreen(juce::Component& screen, bool show)
{
    auto* e = findExtra(screen);
    if (e == nullptr) return;

    if (e->kind == ExtraKind::Modal)
    {
        e->visible = show;
        screen.setVisible(show);
        screen.toFront(true);
        return;
    }

    if (show)
    {
        hideAllOverlays();
        e->visible = true;
        if (mainPanel) mainPanel->setVisible(false);
        screen.setVisible(true);
    }
    else
    {
        e->visible = false;
        screen.setVisible(false);
        if (mainPanel) mainPanel->setVisible(true);   // opening any overlay cleared the mixer (hideAllOverlays) so always return to main
    }
}

bool ShellChrome::anyOverlayVisible() const noexcept
{
    if (mixerVisible || aboutVisible || saveVisible || browserVisible || settingsVisible)
        return true;
    for (auto& e : extras)
        if (e.visible) return true;
    return false;
}

void ShellChrome::hideAllOverlays()
{
    returnToChooser = false;
    // Cancel any in-progress fade animations before forcing visibility states.
    if (mainPanel)         animator.cancelAnimation(mainPanel, true);
    if (mixerOverlay)      animator.cancelAnimation(mixerOverlay, true);
    if (settingsOverlay)   animator.cancelAnimation(settingsOverlay, true);
    animator.cancelAnimation(&presetBrowser, true);
    for (auto& e : extras)
        if (e.kind == ExtraKind::Page) animator.cancelAnimation(e.screen, true);

    auto setOne = [](juce::Component* c) { if (c) { c->setAlpha(1.0f); c->setVisible(false); } };
    setOne(mixerOverlay);
    setOne(settingsOverlay);
    aboutOverlay.setVisible(false);
    saveDialog.setVisible(false);
    presetBrowser.setVisible(false);
    for (auto& e : extras)
    {
        e.screen->setVisible(false);
        e.visible = false;
    }

    mixerVisible = false;
    transportBar.setMixerActive(false);
    aboutVisible    = false;
    saveVisible     = false;
    browserVisible  = false;
    settingsVisible = false;

    if (mainPanel)
    {
        mainPanel->setAlpha(1.0f);
        mainPanel->setVisible(true);
    }
}

void ShellChrome::fadeSwitch(juce::Component* outgoing, juce::Component* incoming, int durationMs)
{
    if (incoming)
    {
        incoming->setAlpha(0.0f);
        incoming->setVisible(true);
    }
    if (outgoing)
        animator.animateComponent(outgoing, outgoing->getBounds(), 0.0f, durationMs, false, 1.0, 1.0);
    if (incoming)
        animator.animateComponent(incoming, incoming->getBounds(), 1.0f, durationMs, false, 1.0, 1.0);

    if (outgoing)
    {
        juce::Component::SafePointer<juce::Component> safeOut(outgoing);
        juce::Timer::callAfterDelay(durationMs + 10, [safeOut]
        {
            if (auto* out = safeOut.getComponent())
            {
                out->setVisible(false);
                out->setAlpha(1.0f);
            }
        });
    }
}

void ShellChrome::showMixer(bool show)
{
    if (mixerOverlay == nullptr) return;
    if (show) hideAllOverlays();
    mixerVisible = show;
    transportBar.setMixerActive(show);
    if (show)
    {
        if (mainPanel) mixerOverlay->setBounds(mainPanel->getBounds());
        fadeSwitch(mainPanel, mixerOverlay);
    }
    else
    {
        fadeSwitch(mixerOverlay, mainPanel);
    }
}

void ShellChrome::showAbout(bool show)
{
    aboutVisible = show;
    if (show && onAboutOpening)
        onAboutOpening();
    aboutOverlay.setVisible(show);
    aboutOverlay.toFront(false);
}

void ShellChrome::showSaveDialog(bool show)
{
    saveVisible = show;
    if (show)
        fillSaveDialog();
    saveDialog.setVisible(show);
    saveDialog.toFront(false);
}

// ─── Preset flow ─────────────────────────────────────────────────────────────

// Transport selector, Save card, browser and the backend's deferred-swap / save-and-quit calls.
void ShellChrome::wirePresetFlow()
{
    transportBar.onPresetSelected = [this](const juce::File& f) { loadPresetFromUi(f); };
    transportBar.onSavePreset     = [this] { showSaveDialog(true); };

    transportBar.onNewPreset = [this]
    {
        if (isDirty())
        {
            juce::WeakReference<ShellChrome> safeThis(this);
            mu_ui::confirmAsync(&host, "New Preset", "Discard unsaved changes?", "Discard",
                [safeThis] { if (safeThis != nullptr) safeThis->doNewPreset(); });
            return;
        }
        doNewPreset();
    };

    backend.onSaveAndQuit = [this](std::function<void()> quitCallback)
    {
        pendingQuitCallback = std::move(quitCallback);
        showSaveDialog(true);
    };

    saveDialog.onSave = [this](const juce::String& name,
                               const juce::String& desc,
                               const juce::String& category,
                               bool embedSamples)
    {
        // "Save as Default": overwrite _default unconditionally.
        if (saveDialog.isSaveAsDefault())
        {
            doSavePreset("_default", desc, category, embedSamples);
            return;
        }

        if (fullPresetFileFor(name).existsAsFile())
        {
            juce::WeakReference<ShellChrome> safeThis(this);
            mu_ui::confirmAsync(&host, "Overwrite Preset", "Overwrite \"" + name + "\"?", "Overwrite",
                [safeThis, n = name, d = desc, c = category, e = embedSamples]
                { if (safeThis != nullptr) safeThis->doSavePreset(n, d, c, e); });
            return;
        }
        doSavePreset(name, desc, category, embedSamples);
    };
    saveDialog.onCancel = [this]
    {
        pendingQuitCallback = nullptr;
        showSaveDialog(false);
    };

    presetBrowser.onLoadPreset = [this](const juce::File& f) { loadPresetFromUi(f); };

    // Deferred full-preset swaps commit at the loop boundary: tell the host once they land.
    backend.onPresetSwapCommitted = [this]
    {
        if (onPresetLoaded) onPresetLoaded(transportBar.getLoadedPresetFile());
    };
}

// Fills the Save card as it opens: the known categories (browser + shared list + host panels)
// and the name / category / embed of the currently loaded preset.
void ShellChrome::fillSaveDialog()
{
    presetBrowser.refresh(backend.getPresetsDir());
    juce::StringArray merged = presetBrowser.getCategories();
    for (const auto& c : backend.loadCategoryList())
        if (! merged.contains(c, false)) merged.add(c);
    if (getProductCategories)
        for (const auto& c : getProductCategories())
            if (! merged.contains(c, false)) merged.add(c);
    merged.sort(false);
    saveDialog.setKnownCategories(merged);
    if (onCategoriesRefreshed) onCategoriesRefreshed(merged);

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

// A full preset chosen in the transport selector or the preset browser: load it and show it.
void ShellChrome::loadPresetFromUi(const juce::File& f)
{
    backend.loadPreset(f);
    transportBar.setLoadedPreset(f);
    clearDirty();
    if (onPresetLoaded) onPresetLoaded(f);
}

// The full-preset file the backend writes for `name` (shared name-cleaning rule).
juce::File ShellChrome::fullPresetFileFor(const juce::String& name) const
{
    return backend.getPresetsDir().getChildFile(mu_pp::safePresetFileName(name, "Preset")
                                                + "." + backend.getFullPresetExtension());
}

void ShellChrome::doSavePreset(const juce::String& name, const juce::String& desc,
                               const juce::String& category, bool embedSamples)
{
    backend.savePreset(name, desc, category, embedSamples);
    backend.ensureCategoryInList(category);
    transportBar.refreshPresets();
    const juce::File saved = fullPresetFileFor(name);
    transportBar.setLoadedPreset(saved);
    clearDirty();
    if (onCategoriesRefreshed) onCategoriesRefreshed(backend.loadCategoryList());
    if (onPresetSaved) onPresetSaved(saved);
    showSaveDialog(false);
    if (pendingQuitCallback)
    {
        auto cb = std::move(pendingQuitCallback);
        pendingQuitCallback = nullptr;
        cb();
    }
}

void ShellChrome::doNewPreset()
{
    const juce::File defaultFile = backend.getDefaultPresetFile();
    if (! defaultFile.existsAsFile())
    {
        statusBar.showParam("New", "No default preset saved \xe2\x80\x94 use Save \xe2\x86\x92 Save as Default first",
                            MuLookAndFeel::colour(MuLookAndFeel::knobLevel));
        if (onPresetNew) onPresetNew();
        return;
    }
    backend.loadPreset(defaultFile);
    transportBar.setLoadedPreset({});
    clearDirty();
    if (onPresetLoaded) onPresetLoaded(defaultFile);
    if (onPresetNew) onPresetNew();
}

// ─── Keys ────────────────────────────────────────────────────────────────────

// Standalone: the play / stop key works from launch (the host grabs focus when first shown).
void ShellChrome::installKeys()
{
    loadKeybindings();
    if (backend.isStandaloneApp())
    {
        host.setWantsKeyboardFocus(true);
        needsFocusGrab = true;
    }
    // The listener also serves Ctrl+P and the lock's key swallowing where the mode is supported.
    if (backend.isStandaloneApp() || backend.supportsPerformanceMode())
        host.addKeyListener(this);
}

void ShellChrome::loadKeybindings()
{
    const juce::File bindingsFile = backend.getContentDir().getChildFile("keybindings.json");

    if (! bindingsFile.existsAsFile())
    {
        // Try to seed defaults, only possible if the content dir exists.
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
            if (s.length() == 1)               return juce::KeyPress((int) s[0]);
            return {};
        };

        if (obj->hasProperty("play_stop"))
            keybindPlayStop = parseKey(obj->getProperty("play_stop").toString());
    }
}

bool ShellChrome::isPerformanceKey(const juce::KeyPress& key) noexcept
{
    const auto m = key.getModifiers();
    const int code = key.getKeyCode();   // letters arrive in either case depending on the platform
    return (m.isCtrlDown() || m.isCommandDown()) && ! m.isShiftDown() && ! m.isAltDown()
        && (code == 'p' || code == 'P');
}

bool ShellChrome::keyPressed(const juce::KeyPress& key, juce::Component*)
{
    // Performance mode locks the whole keyboard (no performance keys, the play key included).
    if (shieldUp) return true;

    // Don't intercept when a text editor has focus: it needs the key for input.
    auto* focused = juce::Component::getCurrentlyFocusedComponent();
    if (dynamic_cast<juce::TextEditor*>(focused)) return false;

    // Ctrl+P enters Performance mode (or says to choose a performance first); it never leaves it.
    if (isPerformanceKey(key) && transportBar.isPerformanceAvailable())
    {
        transportBar.enterPerformanceMode();
        return true;
    }

    if (backend.isStandaloneApp() && key == keybindPlayStop)
    {
        backend.toggleInternalPlay();
        return true;
    }
    return false;
}

bool ShellChrome::keyStateChanged(bool, juce::Component*) { return false; }

void ShellChrome::parentHierarchyChanged()
{
    // Grab focus once on first show so the play key works from launch without a mouse click.
    if (needsFocusGrab && host.isShowing())
    {
        needsFocusGrab = false;
        host.grabKeyboardFocus();
    }
}

// ─── Performance mode: the input shield ──────────────────────────────────────

ShellChrome::InputShield::InputShield()
{
    setInterceptsMouseClicks(true, false);   // it takes the events itself; nothing is drawn
    setWantsKeyboardFocus(true);
    setMouseClickGrabsKeyboardFocus(false);
}

void ShellChrome::InputShield::addExempt(juce::Component& c)
{
    exempt.emplace_back(&c);
}

// False over an exempt component, so the pointer reaches what is underneath there.
bool ShellChrome::InputShield::hitTest(int x, int y)
{
    // Bounds are read per call, so an exemption follows resizes; it only counts while it and its parents are visible.
    for (auto& p : exempt)
        if (auto* c = p.getComponent())
        {
            bool visible = true;
            for (auto* a = c; a != nullptr && a != getParentComponent(); a = a->getParentComponent())
                visible = visible && a->isVisible();
            if (visible && getLocalArea(c, c->getLocalBounds()).contains(x, y))
                return false;
        }
    return true;
}

void ShellChrome::PerformanceFrame::paint(juce::Graphics& g)
{
    g.setColour(MuLookAndFeel::appAccent(*this).withAlpha(MuLookAndFeel::lighting().performanceRim));
    g.drawRect(getLocalBounds(), mu_ui::s(MuLookAndFeel::kPerfFrameW));
}

void ShellChrome::raiseShield()
{
    if (! shieldUp) return;
    frame.toFront(false);    // the frame sits just under the (invisible) shield, over every panel
    shield.toFront(false);
}

// Reads the live state and raises or lowers the shield to match; the bar and sidebar follow the same state.
void ShellChrome::syncPerformanceMode()
{
    const bool on = backend.supportsPerformanceMode() && liveView != nullptr
                    && backend.getControlSink() != nullptr && liveView->isPerformanceMode();
    transportBar.syncPerformance();
    if (on == shieldUp) return;

    shieldUp = on;
    updateNotice.setSuppressed(on);
    if (on) enterLock(); else leaveLock();
}

// Entering: everything open is closed as a cancel first (an overlay left under an invisible shield
// would be a frozen screen), then text focus is released, then the shield goes up and takes the keyboard.
void ShellChrome::enterLock()
{
    // Phase 1: dismiss themed dialogs (confirm, support prompt) as a cancel.
    std::vector<juce::Component::SafePointer<mu_ui::ModalDialog>> dialogs;
    for (auto* c : host.getChildren())
        if (auto* d = dynamic_cast<mu_ui::ModalDialog*>(c)) dialogs.emplace_back(d);
    for (auto& d : dialogs)
        if (d != nullptr) d->cancel();

    // Phase 2: close the overlays; a pending save-and-quit is cancelled with the Save card.
    if (saveVisible) pendingQuitCallback = nullptr;
    hideAllOverlays();

    // Phase 3: release focus, then raise the shield.
    juce::Component::unfocusAllComponents();
    shield.setBounds(host.getLocalBounds());
    frame.setBounds(host.getLocalBounds());
    shield.setVisible(true);
    frame.setVisible(true);
    raiseShield();
    shield.grabKeyboardFocus();
}

void ShellChrome::leaveLock()
{
    shield.setVisible(false);
    frame.setVisible(false);
    host.grabKeyboardFocus();   // the play key works again
}

void ShellChrome::showPresetBrowser(bool show)
{
    if (show)
    {
        hideAllOverlays();
        presetBrowser.refresh(backend.getPresetsDir());
        browserVisible = true;
        if (mainPanel) mainPanel->setVisible(false);
        presetBrowser.setVisible(true);
    }
    else
    {
        browserVisible = false;
        presetBrowser.setVisible(false);
        if (mainPanel) mainPanel->setVisible(true);   // opening any overlay cleared the mixer (hideAllOverlays) so always return to main
    }
}

void ShellChrome::showSettings(bool show)
{
    if (settingsOverlay == nullptr) return;
    if (show)
    {
        hideAllOverlays();
        settingsVisible = true;
        if (mainPanel) mainPanel->setVisible(false);
        settingsOverlay->setVisible(true);
    }
    else
    {
        settingsVisible = false;
        settingsOverlay->setVisible(false);
        if (mainPanel) mainPanel->setVisible(true);   // opening any overlay cleared the mixer (hideAllOverlays) so always return to main
    }
}

void ShellChrome::setMetalStyle(bool metal, juce::Colour appAccent)
{
    lookAndFeel.setMetalStyle(metal);
    if (! appAccent.isTransparent())
    {
        lookAndFeel.setAppAccent(appAccent);
    }
    host.sendLookAndFeelChange();   // shared components re-read the style in lookAndFeelChanged()
    host.repaint();
}

void ShellChrome::setScrews(bool on)
{
    lookAndFeel.setScrews(on);
    host.sendLookAndFeelChange();
    host.repaint();
}

void ShellChrome::forwardKnobStatus(juce::Component& root, const StatusVisitor& visitor)
{
    const StatusFn show = [this](const juce::String& name, const juce::String& val) { statusBar.showParam(name, val); };
    // Walk the panel's tree; a component that reports its own status needn't be searched inside.
    for (auto* c : root.getChildren())
    {
        if (auto* k = dynamic_cast<KnobWithLabel*>(c))
        {
            if (! k->onStatusUpdate) k->onStatusUpdate = show;
        }
        else if (visitor && visitor(*c, show))
        {
            // handled by the host
        }
        else
            forwardKnobStatus(*c, visitor);
    }
}

void ShellChrome::paintBackground(juce::Graphics& g) const
{
    g.fillAll(MuLookAndFeel::colour(MuLookAndFeel::windowBackground));
}

void ShellChrome::resized()
{
    using mu_ui::s;
    const int w          = host.getWidth();
    const int h          = host.getHeight();
    const int statusH    = s(MuLookAndFeel::kStatusBarH);
    const int transportH = s(MuLookAndFeel::kTransportBarH);
    const int sidebarW   = (sidebar != nullptr) ? s(MuLookAndFeel::kSidebarW) : 0;
    const int contentH   = h - transportH - statusH;

    const juce::Rectangle<int> mainArea { sidebarW, transportH,
                                          w - sidebarW, contentH };

    transportBar.setBounds(0, 0, w, transportH);
    if (sidebar)   sidebar  ->setBounds(0, transportH, sidebarW, contentH);
    if (mainPanel) mainPanel->setBounds(mainArea);
    if (mixerOverlay)    mixerOverlay   ->setBounds(mainArea);
    if (settingsOverlay) settingsOverlay->setBounds(mainArea);
    presetBrowser.setBounds(mainArea);
    for (auto& e : extras)
        if (e.kind == ExtraKind::Page) e.screen->setBounds(mainArea);

    // Modal overlays span the full host area.
    aboutOverlay.setBounds(host.getLocalBounds());
    for (auto& e : extras)
        if (e.kind == ExtraKind::Modal) e.screen->setBounds(host.getLocalBounds());
    saveDialog.setBounds(host.getLocalBounds());

    statusBar.setBounds(0, h - statusH, w, statusH);
    shield.setBounds(host.getLocalBounds());
    frame.setBounds(host.getLocalBounds());
}

} // namespace mu_ui
