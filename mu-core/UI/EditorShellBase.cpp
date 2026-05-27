#include "EditorShellBase.h"

EditorShellBase::EditorShellBase(ProcessorBase& proc)
    // Apply the stored UI scale BEFORE any child component is constructed.
    // Comma expression evaluates as part of the base-class init arg; the C++
    // standard guarantees the base class is fully constructed before member
    // ctors run. So `mu_ui::scale` is correct when `transportBar(proc)` and
    // the other members below construct, which is the only way ctor-time
    // `sf(...)` font assignments pick up the right size (audit #574).
    : AudioProcessorEditor((mu_ui::scale = juce::jlimit(ProcessorBase::kUiScaleMedium,
                                                        ProcessorBase::kUiScaleLarge,
                                                        proc.getUiScale()), &proc)),
      processorRef(proc),
      transportBar(proc),
      midiPresetsPanel(proc),
      midiFullPresetsPanel(proc)
{
    setLookAndFeel(&lookAndFeel);

    addAndMakeVisible(transportBar);
    addChildComponent(aboutPanel);
    addChildComponent(saveDialog);
    addChildComponent(presetBrowser);
    addChildComponent(midiPresetsPanel);
    addChildComponent(midiFullPresetsPanel);
    addAndMakeVisible(statusBar);

    // ── TransportBar callbacks ──────────────────────────────────────────────
    transportBar.onMixerToggle  = [this] { showMixer(!mixerVisible); };
    transportBar.onLogoClicked  = [this] { showAbout(true); };
    transportBar.onSettingsToggle = [this] { showSettings(!settingsVisible); };

    transportBar.onPresetSelected = [this](const juce::File& f)
    {
        processorRef.loadPreset(f);
        transportBar.setLoadedPreset(f);
        presetDirty = false;
        onPresetLoaded(f);
    };

    transportBar.onSavePreset = [this]
    {
        if (!processorRef.isLicensed()) return;
        showSaveDialog(true);
    };

    transportBar.onNewPreset = [this]
    {
        if (presetDirty)
        {
            juce::Component::SafePointer<EditorShellBase> safeThis(this);
            auto* w = new juce::AlertWindow("New Preset",
                                            "Discard unsaved changes?",
                                            juce::MessageBoxIconType::QuestionIcon);
            w->addButton("Discard", 1, juce::KeyPress(juce::KeyPress::returnKey));
            w->addButton("Cancel",  0, juce::KeyPress(juce::KeyPress::escapeKey));
            w->enterModalState(true,
                juce::ModalCallbackFunction::create([safeThis](int result) {
                    if (safeThis && result == 1)
                        safeThis->doNewPreset();
                }),
                true);
            return;
        }
        doNewPreset();
    };

    transportBar.onStatusUpdate = [this](const juce::String& name, const juce::String& val)
    {
        statusBar.showParam(name, val);
    };

    processorRef.onSaveAndQuit = [this](std::function<void()> quitCallback)
    {
        pendingQuitCallback = std::move(quitCallback);
        showSaveDialog(true);
    };

    // ── About panel ─────────────────────────────────────────────────────────
    aboutPanel.onDismiss = [this] { showAbout(false); };

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

        juce::String safeName = name.replaceCharacters("\\/:|*?<>\"", "_________");
        if (safeName.isEmpty()) safeName = "Preset";
        const juce::File destFile = processorRef.getPresetsDir()
                                                .getChildFile(safeName + "." + processorRef.getFullPresetExtension());

        if (destFile.existsAsFile())
        {
            juce::Component::SafePointer<EditorShellBase> safeThis(this);
            auto* w = new juce::AlertWindow("Overwrite Preset",
                                            "Overwrite \"" + name + "\"?",
                                            juce::AlertWindow::QuestionIcon);
            w->addButton("Overwrite", 1, juce::KeyPress(juce::KeyPress::returnKey));
            w->addButton("Cancel",    0, juce::KeyPress(juce::KeyPress::escapeKey));
            w->enterModalState(true,
                juce::ModalCallbackFunction::create(
                    [safeThis, n = name, d = desc, c = category, e = embedSamples](int result)
                    {
                        if (safeThis && result == 1)
                            safeThis->doSavePreset(n, d, c, e);
                    }),
                true);
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
    presetBrowser.setFileExtension(processorRef.getFullPresetExtension());
    presetBrowser.onLoadPreset = [this](const juce::File& f)
    {
        processorRef.loadPreset(f);
        transportBar.setLoadedPreset(f);
        presetDirty = false;
        onPresetLoaded(f);
    };
    presetBrowser.onClose = [this] { showPresetBrowser(false); };

    midiPresetsPanel.onClose     = [this] { showMidiPresets(false);     showSettings(true); };
    midiFullPresetsPanel.onClose = [this] { showMidiFullPresets(false); showSettings(true); };

    // Surface preset / state-load failures to the user.
    processorRef.onLoadError = [this](const juce::String& msg)
    {
        statusBar.showParam("Load failed", msg,
                            MuLookAndFeel::colour(MuLookAndFeel::knobLevel));
    };

    // Deferred full-preset swaps commit at the loop boundary — refresh chrome
    // once they land. Product overrides onPresetLoaded to refresh its panels.
    processorRef.onPresetSwapCommitted = [this]
    {
        onPresetLoaded(transportBar.getLoadedPresetFile());
    };

    // ── Demo banner ─────────────────────────────────────────────────────────
    {
        using Id = MuLookAndFeel::ColourIds;
        const bool licensed = processorRef.isLicensed();
        transportBar.setSaveEnabled(licensed);

        demoBanner.setText("DEMO  \xe2\x80\x94  Save disabled  \xe2\x80\x94  Purchase a license to unlock all features",
                           juce::dontSendNotification);
        demoBanner.setJustificationType(juce::Justification::centred);
        demoBanner.setFont(juce::Font(juce::FontOptions{}.withHeight(11.0f)));
        demoBanner.setColour(juce::Label::backgroundColourId,
                             MuLookAndFeel::colour(Id::segmentWarningBg));
        demoBanner.setColour(juce::Label::textColourId,
                             MuLookAndFeel::colour(Id::segmentWarningBorder));
        addChildComponent(demoBanner);
        demoBanner.setVisible(!licensed);
    }

    // ── Window sizing ───────────────────────────────────────────────────────
    setResizable(false, false);
    setSize(mu_ui::s(MuLookAndFeel::kWindowWidth), mu_ui::s(MuLookAndFeel::kWindowHeight));

    // React to runtime scale changes from the settings overlay.
    juce::Component::SafePointer<EditorShellBase> safeThis(this);
    processorRef.onUiScaleChanged = [safeThis](float scale)
    {
        if (auto* self = safeThis.getComponent())
        {
            mu_ui::scale = scale;
            self->setSize(mu_ui::s(MuLookAndFeel::kWindowWidth),
                          mu_ui::s(MuLookAndFeel::kWindowHeight));
            self->sendLookAndFeelChange();
            self->resized();
            self->repaint();
        }
    };

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
}

EditorShellBase::~EditorShellBase()
{
    // Clear every processor callback before teardown — processor outlives the
    // editor in DAW close-window-keep-plugin scenarios; any deferred invocation
    // into a destroyed editor is a UAF. All lambdas captured raw [this].
    processorRef.apvts.state.removeListener(this);
    processorRef.onPresetSwapCommitted = nullptr;
    processorRef.onSaveAndQuit         = nullptr;
    processorRef.onLoadError           = nullptr;
    processorRef.onUiScaleChanged      = nullptr;

    if (isStandalone)
        removeKeyListener(this);
    setLookAndFeel(nullptr);
}

void EditorShellBase::setMainArea(juce::Component* newSidebar, juce::Component* newMainPanel)
{
    if (sidebar != nullptr)   removeChildComponent(sidebar);
    if (mainPanel != nullptr) removeChildComponent(mainPanel);
    sidebar   = newSidebar;
    mainPanel = newMainPanel;
    if (sidebar != nullptr)   addAndMakeVisible(*sidebar);
    if (mainPanel != nullptr) addAndMakeVisible(*mainPanel);
    resized();
}

void EditorShellBase::setMixerOverlay(juce::Component* overlay)
{
    if (mixerOverlay != nullptr) removeChildComponent(mixerOverlay);
    mixerOverlay = overlay;
    if (mixerOverlay != nullptr)
    {
        addChildComponent(*mixerOverlay);
        transportBar.setShowMixerToggle(true);
    }
    else
    {
        transportBar.setShowMixerToggle(false);
    }
    resized();
}

void EditorShellBase::setSettingsOverlay(juce::Component* overlay)
{
    if (settingsOverlay != nullptr) removeChildComponent(settingsOverlay);
    settingsOverlay = overlay;
    if (settingsOverlay != nullptr)
        addChildComponent(*settingsOverlay);
    // TransportBar's gear button stays visible regardless — if there's no
    // settings overlay, clicking it is a no-op (onSettingsToggle does nothing
    // useful). Products that want to hide the gear entirely can call
    // transportBar.setShowSettingsButton(false) once that setter exists; for
    // now mu-tant simply doesn't register and the click flickers nothing.
    resized();
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
}

void EditorShellBase::hideAllOverlays()
{
    // Cancel any in-progress fade animations before forcing visibility states.
    if (mainPanel)         animator.cancelAnimation(mainPanel, true);
    if (mixerOverlay)      animator.cancelAnimation(mixerOverlay, true);
    if (settingsOverlay)   animator.cancelAnimation(settingsOverlay, true);
    animator.cancelAnimation(&presetBrowser, true);
    animator.cancelAnimation(&midiPresetsPanel, true);
    animator.cancelAnimation(&midiFullPresetsPanel, true);

    auto setOne = [](juce::Component* c) { if (c) { c->setAlpha(1.0f); c->setVisible(false); } };
    setOne(mixerOverlay);
    setOne(settingsOverlay);
    aboutPanel.setVisible(false);
    saveDialog.setVisible(false);
    presetBrowser.setVisible(false);
    midiPresetsPanel.setVisible(false);
    midiFullPresetsPanel.setVisible(false);

    mixerVisible           = false;
    transportBar.setMixerActive(false);
    aboutVisible           = false;
    saveVisible            = false;
    browserVisible         = false;
    settingsVisible        = false;
    midiPresetsVisible     = false;
    midiFullPresetsVisible = false;

    if (mainPanel)
    {
        mainPanel->setAlpha(1.0f);
        mainPanel->setVisible(true);
    }
}

void EditorShellBase::fadeSwitch(juce::Component* outgoing, juce::Component* incoming, int durationMs)
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

void EditorShellBase::showMixer(bool show)
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

void EditorShellBase::showAbout(bool show)
{
    aboutVisible = show;
    aboutPanel.setVisible(show);
    aboutPanel.toFront(false);
}

void EditorShellBase::showSaveDialog(bool show)
{
    saveVisible = show;
    if (show)
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
            if (auto xml = juce::parseXML(loaded))
            {
                auto state = juce::ValueTree::fromXml(*xml);
                saveDialog.setDefaultCategory(state.getProperty("presetCategory",    "").toString());
                saveDialog.setDefaultEmbed   ((int)state.getProperty("presetEmbedSamples", 0) != 0);
            }
        }
    }
    saveDialog.setVisible(show);
    saveDialog.toFront(false);
}

void EditorShellBase::doSavePreset(const juce::String& name, const juce::String& desc,
                                   const juce::String& category, bool embedSamples)
{
    processorRef.savePreset(name, desc, category, embedSamples);
    processorRef.ensureCategoryInList(category);
    transportBar.refreshPresets();
    juce::String safeName = name.replaceCharacters("\\/:|*?<>\"", "_________");
    if (safeName.isEmpty()) safeName = "Preset";
    const juce::File saved = processorRef.getPresetsDir()
                                          .getChildFile(safeName + "." + processorRef.getFullPresetExtension());
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
    const juce::File defaultFile = processorRef.getPresetsDir()
                                                .getChildFile(juce::String("_default.") + processorRef.getFullPresetExtension());
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

void EditorShellBase::showPresetBrowser(bool show)
{
    if (show)
    {
        hideAllOverlays();
        presetBrowser.refresh(processorRef.getPresetsDir());
        browserVisible = true;
        if (mainPanel) mainPanel->setVisible(false);
        presetBrowser.setVisible(true);
    }
    else
    {
        browserVisible = false;
        presetBrowser.setVisible(false);
        if (mainPanel) mainPanel->setVisible(!mixerVisible);
    }
}

void EditorShellBase::showSettings(bool show)
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
        if (mainPanel) mainPanel->setVisible(!mixerVisible);
    }
}

void EditorShellBase::showMidiPresets(bool show)
{
    if (show)
    {
        hideAllOverlays();
        midiPresetsVisible = true;
        if (mainPanel) mainPanel->setVisible(false);
        midiPresetsPanel.setVisible(true);
    }
    else
    {
        midiPresetsVisible = false;
        midiPresetsPanel.setVisible(false);
        if (mainPanel) mainPanel->setVisible(!mixerVisible);
    }
}

void EditorShellBase::showMidiFullPresets(bool show)
{
    if (show)
    {
        hideAllOverlays();
        midiFullPresetsVisible = true;
        if (mainPanel) mainPanel->setVisible(false);
        midiFullPresetsPanel.setVisible(true);
    }
    else
    {
        midiFullPresetsVisible = false;
        midiFullPresetsPanel.setVisible(false);
        if (mainPanel) mainPanel->setVisible(!mixerVisible);
    }
}

void EditorShellBase::paint(juce::Graphics& g)
{
    g.fillAll(MuLookAndFeel::colour(MuLookAndFeel::windowBackground));
}

void EditorShellBase::resized()
{
    using mu_ui::s;
    const int w          = getWidth();
    const int h          = getHeight();
    const int statusH    = s(MuLookAndFeel::kStatusBarH);
    const int transportH = s(MuLookAndFeel::kTransportBarH);
    const int sidebarW   = (sidebar != nullptr) ? s(MuLookAndFeel::kSidebarW) : 0;
    const int bannerH    = processorRef.isLicensed() ? 0 : s(kDemoBannerH);
    const int contentH   = h - transportH - statusH - bannerH;

    const juce::Rectangle<int> mainArea { sidebarW, transportH,
                                          w - sidebarW, contentH };

    transportBar.setBounds(0, 0, w, transportH);
    if (sidebar)   sidebar  ->setBounds(0, transportH, sidebarW, contentH);
    if (mainPanel) mainPanel->setBounds(mainArea);
    if (mixerOverlay)    mixerOverlay   ->setBounds(mainArea);
    if (settingsOverlay) settingsOverlay->setBounds(mainArea);
    presetBrowser   .setBounds(mainArea);
    midiPresetsPanel.setBounds(mainArea);
    midiFullPresetsPanel.setBounds(mainArea);

    // Modal overlays span the full editor area
    aboutPanel.setBounds(getLocalBounds());
    saveDialog.setBounds(getLocalBounds());

    if (bannerH > 0)
        demoBanner.setBounds(0, h - statusH - bannerH, w, bannerH);

    statusBar.setBounds(0, h - statusH, w, statusH);
}
