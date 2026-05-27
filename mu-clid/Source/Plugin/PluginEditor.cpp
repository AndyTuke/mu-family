#include "PluginEditor.h"
#include "Audio/FX/Slots/FXAlgorithmDef.h"
#include <BinaryData.h>

PluginEditor::PluginEditor(PluginProcessor& p)
    // Apply the stored UI scale BEFORE any child component is constructed.
    // Comma expression evaluates as part of the base-class init arg; the C++
    // standard guarantees the base class is fully constructed before member
    // ctors run. So `mu_ui::scale` is correct when `transportBar(p)` and the
    // other members below construct, which is the only way ctor-time
    // `sf(...)` font assignments pick up the right size (audit #574).
    : AudioProcessorEditor((mu_ui::scale = juce::jlimit(PluginProcessor::kUiScaleMedium,
                                                        PluginProcessor::kUiScaleLarge,
                                                        p.getUiScale()), &p)),
      processorRef(p),
      transportBar(p), sidebar(p), rhythmPanel(p),
      mixerOverlay(p, p.mixerEngine),
      settingsOverlay(p),
      midiPresetsPanel(p),
      midiFullPresetsPanel(p)
{
    setLookAndFeel(&lookAndFeel);

    // Supply product-specific chrome to the shared mu-core overlays.
    aboutPanel.setProductInfo(
        juce::String(juce::CharPointer_UTF8("\xce\xbc")) + "-Clid",
        juce::StringArray {
            juce::String(juce::CharPointer_UTF8("JUCE \xe2\x80\x94 Proprietary (JUCE 7 license)")),
            juce::String(juce::CharPointer_UTF8("Signalsmith Reverb \xe2\x80\x94 MIT")),
            juce::String(juce::CharPointer_UTF8("Monocypher \xe2\x80\x94 BSD-2-Clause")),
            juce::String(juce::CharPointer_UTF8("clap-juce-extensions \xe2\x80\x94 MIT")),
            juce::String(juce::CharPointer_UTF8("Bj\xc3\xb6rklund algorithm \xe2\x80\x94 public domain")),
        });
    saveDialog.setLogoImage(juce::ImageCache::getFromMemory(BinaryData::muclid_png,
                                                            BinaryData::muclid_pngSize));

    addAndMakeVisible(transportBar);
    addAndMakeVisible(sidebar);
    addAndMakeVisible(rhythmPanel);
    addChildComponent(mixerOverlay);
    addChildComponent(aboutPanel);
    addChildComponent(saveDialog);
    addChildComponent(presetBrowser);
    addChildComponent(settingsOverlay);
    addChildComponent(midiPresetsPanel);
    addChildComponent(midiFullPresetsPanel);
    addAndMakeVisible(statusBar);

    // ── TransportBar callbacks ────────────────────────────────────────────────
    transportBar.onMixerToggle = [this] { showMixer(!mixerVisible); };

    transportBar.onLogoClicked = [this] { showAbout(true); };

    transportBar.onPresetSelected = [this](const juce::File& f)
    {
        processorRef.loadPreset(f);
        selectRhythmAndRefresh(0, /*fullSidebarRefresh=*/true, MixerRefresh::FullReload);
        transportBar.setLoadedPreset(f);
        presetDirty = false;
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
            juce::Component::SafePointer<PluginEditor> safeThis(this);
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

    processorRef.onSaveAndQuit = [this](std::function<void()> quitCallback)
    {
        pendingQuitCallback = std::move(quitCallback);
        showSaveDialog(true);
    };

    transportBar.onSettingsToggle = [this] { showSettings(!settingsVisible); };

    // ── Sidebar callbacks ─────────────────────────────────────────────────────
    sidebar.onRhythmSelected = [this](int idx)
    {
        rhythmPanel.setRhythm(idx);
        if (!mixerVisible)
        {
            hideAllOverlays();
            rhythmPanel.setVisible(true);
        }
    };

    sidebar.onRhythmsReordered = [this](int newSelected)
    {
        selectRhythmAndRefresh(newSelected, /*fullSidebarRefresh=*/false, MixerRefresh::FullReload);
    };

    sidebar.onAddRhythm = [this]
    {
        if (!processorRef.isLicensed() && processorRef.getNumRhythms() >= 2)
        {
            statusBar.showParam("Demo", juce::String::fromUTF8(u8"2-rhythm limit — purchase a license to unlock all 8"),
                                MuClidLookAndFeel::colour(MuClidLookAndFeel::knobLevel));
            return;
        }
        if (processorRef.getNumRhythms() >= SequencerEngine::MaxRhythms) return;
        Rhythm r;
        r.name        = "<unnamed>";
        // Pick the first palette index that no existing rhythm is using, so the
        // new slot is visually distinct from every other rhythm. Falls back to
        // the bare cyclic position only if all palette colours are in use
        // (which can't happen since max rhythms = palette size = 8).
        {
            constexpr int N = MuClidLookAndFeel::kChannelPaletteSize;
            std::array<bool, N> used{};
            const int nExisting = processorRef.getNumRhythms();
            for (int i = 0; i < nExisting; ++i)
                used[(size_t) (processorRef.getRhythm(i).colourIndex % N)] = true;
            const int startProbe = nExisting % N;
            int chosen = startProbe;
            for (int i = 0; i < N; ++i)
            {
                const int candidate = (startProbe + i) % N;
                if (! used[(size_t) candidate]) { chosen = candidate; break; }
            }
            r.colourIndex = chosen;
        }
        processorRef.addRhythm(r);
        const int newIdx = processorRef.getNumRhythms() - 1;
        processorRef.applyDefaultRhythm(newIdx);
        selectRhythmAndRefresh(newIdx, /*fullSidebarRefresh=*/true, MixerRefresh::RefreshOnly);
    };

    // ── RhythmPanel status ────────────────────────────────────────────────────
    rhythmPanel.onStatusUpdate = [this](const juce::String& name,
                                        const juce::String& val,
                                        juce::Colour col)
    {
        statusBar.showParam(name, val, col);
    };

    // chrome controls (TransportBar bpm/loop, MixerChannel outBus/scSource)
    // now also report to the global StatusBar.
    transportBar.onStatusUpdate = [this](const juce::String& name, const juce::String& val)
    {
        statusBar.showParam(name, val);
    };
    mixerOverlay.onStatusUpdate = [this](const juce::String& name,
                                          const juce::String& val,
                                          juce::Colour col)
    {
        statusBar.showParam(name, val, col);
    };

    // Mirror the mixer's effect-slot algorithm name on the voice section's
    // Amp "Effect" send knob so the user can read it without opening the
    // mixer overlay. Two paths feed this: (a) the mixer's own dropdown
    // (synchronous from updateEffectSendLabels), (b) host automation of
    // eff_algo (asynchronous via the APVTS listener registered below — fires
    // regardless of mixer visibility).
    mixerOverlay.onEffectAlgorithmNameChanged = [this](const juce::String& name)
    {
        rhythmPanel.setVoiceEffectSendLabel(name);
    };
    processorRef.apvts.addParameterListener("eff_algo", this);

    rhythmPanel.onRhythmRenamed = [this]
    {
        sidebar.repaintItems();
        if (mixerVisible) mixerOverlay.refresh();
    };

    // surface preset/state-load failures to the user. Without this they got
    // silence when clicking a corrupted preset — looked like the click didn't
    // register. Same colour as the other "operation result" status messages.
    processorRef.onLoadError = [this](const juce::String& msg)
    {
        statusBar.showParam("Load failed", msg,
                            MuClidLookAndFeel::colour(MuClidLookAndFeel::knobLevel));
    };

    // hot-swap commit fired non-APVTS state changes that needed UI refresh —
    // the rhythm name, sample bar, and colour tint live outside APVTS so the
    // pushRhythmToAPVTS listener path doesn't propagate them. If the currently-
    // displayed rhythm was the one swapped, re-call setRhythm so it re-binds
    // every panel against the new Rhythm struct.
    processorRef.onRhythmHotSwapCommitted = [this](int r)
    {
        if (r == rhythmPanel.getCurrentRhythmIndex())
            rhythmPanel.setRhythm(r);
        sidebar.repaintItems();
        if (mixerVisible) mixerOverlay.refresh();
    };

    // Deferred full-preset swaps commit at the loop boundary (see HotSwapStager).
    // The browser / transport-bar load paths refresh the UI immediately, but when
    // playing the swap is deferred, so the immediate refresh runs against the old
    // state — this callback re-runs the same full refresh once the swap lands.
    processorRef.onPresetSwapCommitted = [this]
    {
        selectRhythmAndRefresh(0, /*fullSidebarRefresh=*/true, MixerRefresh::FullReload);
    };

    rhythmPanel.onRhythmDeleted = [this](int idx)
    {
        processorRef.removeRhythm(idx);
        const int newIdx = juce::jmax(0, juce::jmin(idx, processorRef.getNumRhythms() - 1));
        sidebar.refreshItems();
        sidebar.setSelectedIndex(newIdx);
        rhythmPanel.setRhythm(newIdx);
        if (mixerVisible) mixerOverlay.refresh();
    };

    // ── About panel ───────────────────────────────────────────────────────────
    aboutPanel.onDismiss = [this] { showAbout(false); };

    // ── Save dialog ───────────────────────────────────────────────────────────
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
        const juce::File destFile = processorRef.getPresetsDir().getChildFile(safeName + ".muClid");

        if (destFile.existsAsFile())
        {
            juce::Component::SafePointer<PluginEditor> safeThis(this);
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

    // ── Preset browser ────────────────────────────────────────────────────────
    presetBrowser.setFileExtension(processorRef.getFullPresetExtension());
    presetBrowser.onLoadPreset = [this](const juce::File& f)
    {
        processorRef.loadPreset(f);
        sidebar.refreshItems();
        rhythmPanel.setRhythm(0);
        sidebar.setSelectedIndex(0);
        if (mixerVisible) { mixerOverlay.refresh(); mixerOverlay.loadFromAPVTS(); }
        transportBar.setLoadedPreset(f);
        presetDirty = false;
    };
    presetBrowser.onClose = [this] { showPresetBrowser(false); };

    // ── Settings overlay ──────────────────────────────────────────────────────
    settingsOverlay.onClose = [this] { showSettings(false); };
    settingsOverlay.onContentDirChanged = [this] { transportBar.refreshPresets(); };
    settingsOverlay.onMidiPresetsClicked = [this]
    {
        showSettings(false);
        showMidiPresets(true);
    };

    midiPresetsPanel.onClose = [this]
    {
        showMidiPresets(false);
        showSettings(true);
    };

    settingsOverlay.onFullPresetsClicked = [this]
    {
        showSettings(false);
        showMidiFullPresets(true);
    };

    midiFullPresetsPanel.onClose = [this]
    {
        showMidiFullPresets(false);
        showSettings(true);
    };

    // ── Demo mode ─────────────────────────────────────────────────────────────
    {
        using Id = MuClidLookAndFeel::ColourIds;
        const bool licensed = processorRef.isLicensed();

        transportBar.setSaveEnabled(licensed);

        demoBanner.setText("DEMO  \xe2\x80\x94  Max 2 rhythms  \xc2\xb7  Save disabled  \xe2\x80\x94  Purchase a license to unlock all features",
                           juce::dontSendNotification);
        demoBanner.setJustificationType(juce::Justification::centred);
        demoBanner.setFont(juce::Font(juce::FontOptions{}.withHeight(11.0f)));
        demoBanner.setColour(juce::Label::backgroundColourId,
                             MuClidLookAndFeel::colour(Id::segmentWarningBg));
        demoBanner.setColour(juce::Label::textColourId,
                             MuClidLookAndFeel::colour(Id::segmentWarningBorder));
        addChildComponent(demoBanner);
        demoBanner.setVisible(!licensed);
    }

    // ── Startup ───────────────────────────────────────────────────────────────
    if (processorRef.getNumRhythms() == 0)
    {
        Rhythm r;
        r.name        = "<unnamed>";
        r.colourIndex = 0;
        processorRef.addRhythm(r);
        sidebar.refreshItems();
    }

    rhythmPanel.setRhythm(0);
    sidebar.setSelectedIndex(0);

    // Sync mixer UI from APVTS — in standalone, state is restored before the editor
    // is created, so without this call the scSourceBox and other controls would show
    // defaults rather than the restored values, causing visible GR with no source shown.
    mixerOverlay.loadFromAPVTS();

    // Plugin window is now fixed-size at the Medium-baseline dimensions in
    // MuLookAndFeel — see kWindowWidth / kWindowHeight there. Layout pass
    // across the UI is being rearchitected to use fixed PX values measured
    // from this baseline (Large / Small to arrive later as % scalings).
    // Removing the resize affordance closes off the axis we're moving away
    // from so layout bugs found after this can't be confused with window
    // resizing edge cases.
    setResizable(false, false);
    setSize(mu_ui::s(MuLookAndFeel::kWindowWidth), mu_ui::s(MuLookAndFeel::kWindowHeight));

    // React to runtime scale changes from SettingsOverlay. The picker writes
    // through proc.setUiScale → callback → here. Layout reflows immediately
    // (every panel's resized() / paint() uses mu_ui::s() / sf() so the
    // geometry catches up). Ctor-time font assignments stay at the previous
    // scale until the editor is reopened — the picker shows a hint about
    // that to set expectations.
    juce::Component::SafePointer<PluginEditor> safeThis(this);
    processorRef.onUiScaleChanged = [safeThis](float scale)
    {
        if (auto* self = safeThis.getComponent())
        {
            mu_ui::scale = scale;
            self->setSize(mu_ui::s(MuLookAndFeel::kWindowWidth),
                          mu_ui::s(MuLookAndFeel::kWindowHeight));
            self->sendLookAndFeelChange();    // re-fonts on components that hook it
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

void PluginEditor::selectRhythmAndRefresh(int idx,
                                          bool fullSidebarRefresh,
                                          MixerRefresh mixerRefresh)
{
    if (fullSidebarRefresh)
        sidebar.refreshItems();
    sidebar.setSelectedIndex(idx);
    rhythmPanel.setRhythm(idx);
    if (mixerVisible)
    {
        if (mixerRefresh != MixerRefresh::Skip)
            mixerOverlay.refresh();
        if (mixerRefresh == MixerRefresh::FullReload)
            mixerOverlay.loadFromAPVTS();
    }
}

PluginEditor::~PluginEditor()
{
    // clear every callback the processor holds before teardown — processor
    // outlives the editor in DAW close-window-keep-plugin scenarios, and any deferred
    // invocation (swap commit, save-and-quit prompt) firing into a destroyed editor is
    // a UAF. The lambdas all capture raw `[this]`. Mirror this any time we add a new
    // processorRef.on* callback below.
    processorRef.apvts.state.removeListener(this);
    processorRef.apvts.removeParameterListener("eff_algo", this);
    processorRef.onRhythmHotSwapCommitted = nullptr;
    processorRef.onPresetSwapCommitted    = nullptr;
    processorRef.onSaveAndQuit            = nullptr;
    processorRef.onLoadError              = nullptr;
    processorRef.onUiScaleChanged         = nullptr;

    if (isStandalone)
        removeKeyListener(this);
    setLookAndFeel(nullptr);
}

//==============================================================================
bool PluginEditor::keyPressed(const juce::KeyPress& key, juce::Component*)
{
    // Don't intercept when a text editor has focus — it needs the key for input.
    // Buttons are NOT excluded: returning true here prevents the focused button from
    // also firing on space, which is the desired behaviour (space = Play/Stop only).
    auto* focused = juce::Component::getCurrentlyFocusedComponent();
    if (dynamic_cast<juce::TextEditor*>(focused)) return false;

    if (key == keybindPlayStop)
    {
        processorRef.toggleInternalPlay();
        return true;
    }
    return false;
}

bool PluginEditor::keyStateChanged(bool, juce::Component*) { return false; }

void PluginEditor::parentHierarchyChanged()
{
    // Grab focus once on first show so Space works from launch without needing a mouse click.
    if (needsFocusGrab && isShowing())
    {
        needsFocusGrab = false;
        grabKeyboardFocus();
    }
}

void PluginEditor::loadKeybindings()
{
    const juce::File bindingsFile = processorRef.getContentDir().getChildFile("keybindings.json");

    if (!bindingsFile.existsAsFile())
    {
        bindingsFile.replaceWithText("{\n  \"play_stop\": \"space\"\n}\n");
        return;  // defaults already set in member initialiser
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

//==============================================================================
void PluginEditor::hideAllOverlays()
{
    // Cancel any in-progress fade animations before forcing visibility states
    animator.cancelAnimation(&rhythmPanel,    true);
    animator.cancelAnimation(&mixerOverlay,   true);
    animator.cancelAnimation(&presetBrowser,  true);
    animator.cancelAnimation(&settingsOverlay, true);
    animator.cancelAnimation(&midiPresetsPanel, true);
    animator.cancelAnimation(&midiFullPresetsPanel, true);
    rhythmPanel     .setAlpha(1.0f);
    mixerOverlay    .setAlpha(1.0f);
    presetBrowser   .setAlpha(1.0f);
    settingsOverlay .setAlpha(1.0f);
    midiPresetsPanel.setAlpha(1.0f);
    midiFullPresetsPanel.setAlpha(1.0f);

    mixerVisible       = false;
    transportBar.setMixerActive(false);
    aboutVisible       = false;
    saveVisible        = false;
    browserVisible     = false;
    settingsVisible    = false;
    midiPresetsVisible = false;
    midiFullPresetsVisible = false;

    mixerOverlay    .setVisible(false);
    aboutPanel      .setVisible(false);
    saveDialog      .setVisible(false);
    presetBrowser   .setVisible(false);
    settingsOverlay .setVisible(false);
    midiPresetsPanel.setVisible(false);
    midiFullPresetsPanel.setVisible(false);
    rhythmPanel     .setVisible(true);
}

void PluginEditor::fadeSwitch(juce::Component* outgoing, juce::Component* incoming, int durationMs)
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

    // Hide outgoing after animation completes. SafePointer guards against the
    // editor (and therefore outgoing) being destroyed mid-animation.
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

void PluginEditor::showMixer(bool show)
{
    if (show) hideAllOverlays();
    mixerVisible = show;
    transportBar.setMixerActive(show);
    if (show)
    {
        mixerOverlay.refresh();
        mixerOverlay.setBounds(rhythmPanel.getBounds());
        fadeSwitch(&rhythmPanel, &mixerOverlay);
    }
    else
    {
        fadeSwitch(&mixerOverlay, &rhythmPanel);
    }
}

void PluginEditor::showAbout(bool show)
{
    aboutVisible = show;
    aboutPanel.setVisible(show);
    aboutPanel.toFront(false);
}

void PluginEditor::showSaveDialog(bool show)
{
    saveVisible = show;
    if (show)
    {
        presetBrowser.refresh(processorRef.getPresetsDir());
        // Merge categories from both browsers + the shared categories.txt
        juce::StringArray merged = presetBrowser.getCategories();
        for (const auto& c : processorRef.loadCategoryList())
            if (!merged.contains(c, false)) merged.add(c);
        for (const auto& c : rhythmPanel.getKnownCategories())
            if (!merged.contains(c, false)) merged.add(c);
        merged.sort(false);
        saveDialog.setKnownCategories(merged);
        rhythmPanel.setKnownCategories(merged);

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

void PluginEditor::doSavePreset(const juce::String& name, const juce::String& desc,
                                const juce::String& category, bool embedSamples)
{
    processorRef.savePreset(name, desc, category, embedSamples);
    processorRef.ensureCategoryInList(category);
    rhythmPanel.setKnownCategories(processorRef.loadCategoryList());
    transportBar.refreshPresets();
    juce::String safeName = name.replaceCharacters("\\/:|*?<>\"", "_________");
    if (safeName.isEmpty()) safeName = "Preset";
    transportBar.setLoadedPreset(processorRef.getPresetsDir().getChildFile(safeName + ".muClid"));
    presetDirty = false;
    showSaveDialog(false);
    if (pendingQuitCallback)
    {
        auto cb = std::move(pendingQuitCallback);
        pendingQuitCallback = nullptr;
        cb();
    }
}

void PluginEditor::doNewPreset()
{
    const juce::File defaultFile = processorRef.getPresetsDir().getChildFile("_default.muClid");
    if (!defaultFile.existsAsFile())
    {
        statusBar.showParam("New", "No default preset saved \xe2\x80\x94 use Save \xe2\x86\x92 Save as Default first",
                            MuClidLookAndFeel::colour(MuClidLookAndFeel::knobLevel));
        return;
    }
    processorRef.loadPreset(defaultFile);
    selectRhythmAndRefresh(0, /*fullSidebarRefresh=*/true, MixerRefresh::FullReload);
    transportBar.setLoadedPreset({});
    presetDirty = false;
}

void PluginEditor::showPresetBrowser(bool show)
{
    if (show)
    {
        hideAllOverlays();
        presetBrowser.refresh(processorRef.getPresetsDir());
        browserVisible = true;
        rhythmPanel.setVisible(false);
        presetBrowser.setVisible(true);
    }
    else
    {
        browserVisible = false;
        presetBrowser.setVisible(false);
        rhythmPanel.setVisible(!mixerVisible);
    }
}

void PluginEditor::showSettings(bool show)
{
    if (show)
    {
        hideAllOverlays();
        settingsVisible = true;
        rhythmPanel.setVisible(false);
        settingsOverlay.setVisible(true);
    }
    else
    {
        settingsVisible = false;
        settingsOverlay.setVisible(false);
        rhythmPanel.setVisible(!mixerVisible);
    }
}

void PluginEditor::showMidiPresets(bool show)
{
    if (show)
    {
        hideAllOverlays();
        midiPresetsVisible = true;
        rhythmPanel.setVisible(false);
        midiPresetsPanel.setVisible(true);
    }
    else
    {
        midiPresetsVisible = false;
        midiPresetsPanel.setVisible(false);
        rhythmPanel.setVisible(!mixerVisible);
    }
}

void PluginEditor::showMidiFullPresets(bool show)
{
    if (show)
    {
        hideAllOverlays();
        midiFullPresetsVisible = true;
        rhythmPanel.setVisible(false);
        midiFullPresetsPanel.setVisible(true);
    }
    else
    {
        midiFullPresetsVisible = false;
        midiFullPresetsPanel.setVisible(false);
        rhythmPanel.setVisible(!mixerVisible);
    }
}

//==============================================================================
void PluginEditor::paint(juce::Graphics& g)
{
    g.fillAll(MuClidLookAndFeel::colour(MuClidLookAndFeel::windowBackground));
}

void PluginEditor::resized()
{
    using mu_ui::s;
    const int w          = getWidth();
    const int h          = getHeight();
    const int statusH    = s(MuLookAndFeel::kStatusBarH);
    const int transportH = s(MuLookAndFeel::kTransportBarH);
    const int sidebarW   = s(MuLookAndFeel::kSidebarW);
    const int bannerH    = processorRef.isLicensed() ? 0 : s(kDemoBannerH);
    const int contentH   = h - transportH - statusH - bannerH;

    const juce::Rectangle<int> mainArea { sidebarW, transportH,
                                          w - sidebarW, contentH };

    transportBar.setBounds(0, 0, w, transportH);
    sidebar.setBounds(0, transportH, sidebarW, contentH);

    // Align the rhythm preset dropdown's left edge with the main preset dropdown
    // above it (+ 10 px indent to show visual hierarchy). Computed after the
    // TransportBar has laid out so presetDropdown.getX() is valid.
    rhythmPanel.setPresetDropLeft(transportBar.getPresetDropdownLeft() - sidebarW + s(10));
    rhythmPanel     .setBounds(mainArea);
    mixerOverlay    .setBounds(mainArea);
    presetBrowser   .setBounds(mainArea);
    settingsOverlay .setBounds(mainArea);
    midiPresetsPanel.setBounds(mainArea);
    midiFullPresetsPanel.setBounds(mainArea);

    // Modal overlays span the full editor area
    aboutPanel .setBounds(getLocalBounds());
    saveDialog .setBounds(getLocalBounds());

    if (bannerH > 0)
        demoBanner.setBounds(0, h - statusH - bannerH, w, bannerH);

    statusBar.setBounds(0, h - statusH, w, statusH);
}

//==============================================================================
// APVTS listener — fires from any thread (host automation hits the audio
// thread). Marshal to the message thread before touching the UI.
void PluginEditor::parameterChanged(const juce::String& parameterID, float)
{
    if (parameterID != "eff_algo") return;

    juce::Component::SafePointer<PluginEditor> safeThis(this);
    juce::MessageManager::callAsync([safeThis]
    {
        if (safeThis) safeThis->syncVoiceEffectSendLabel();
    });
}

void PluginEditor::syncVoiceEffectSendLabel()
{
    const auto& algos = FXAlgorithmRegistry::effectAlgorithms();
    const int ai = processorRef.fxChain.effectSlot().getAlgorithmIndex();
    const juce::String name = (ai >= 0 && ai < (int) algos.size())
                              ? algos[(size_t) ai].name
                              : juce::String("Effect");
    rhythmPanel.setVoiceEffectSendLabel(name);
}
