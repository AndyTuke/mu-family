#include "PluginEditor.h"

PluginEditor::PluginEditor(PluginProcessor& p)
    : AudioProcessorEditor(&p), processorRef(p),
      transportBar(p), sidebar(p), rhythmPanel(p),
      mixerOverlay(p, p.mixerEngine),
      settingsOverlay(p),
      midiPresetsPanel(p)
{
    setLookAndFeel(&lookAndFeel);

    addAndMakeVisible(transportBar);
    addAndMakeVisible(sidebar);
    addAndMakeVisible(rhythmPanel);
    addChildComponent(mixerOverlay);
    addChildComponent(aboutPanel);
    addChildComponent(saveDialog);
    addChildComponent(presetBrowser);
    addChildComponent(settingsOverlay);
    addChildComponent(midiPresetsPanel);
    addAndMakeVisible(statusBar);

    // ── TransportBar callbacks ────────────────────────────────────────────────
    transportBar.onMixerToggle = [this] { showMixer(!mixerVisible); };

    transportBar.onLogoClicked = [this] { showAbout(true); };

    transportBar.onPresetSelected = [this](const juce::File& f)
    {
        processorRef.loadPreset(f);
        sidebar.refreshItems();
        rhythmPanel.setRhythm(0);
        sidebar.setSelectedIndex(0);
        if (mixerVisible) { mixerOverlay.refresh(); mixerOverlay.loadFromAPVTS(); }
    };

    transportBar.onSavePreset = [this]
    {
        if (!processorRef.isLicensed()) return;
        showSaveDialog(true);
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
        sidebar.setSelectedIndex(newSelected);
        rhythmPanel.setRhythm(newSelected);
        if (mixerVisible)
        {
            mixerOverlay.refresh();
            mixerOverlay.loadFromAPVTS();
        }
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
        r.colourIndex = processorRef.getNumRhythms() % 30;
        processorRef.addRhythm(r);
        const int newIdx = processorRef.getNumRhythms() - 1;
        processorRef.applyDefaultRhythm(newIdx);
        sidebar.refreshItems();
        sidebar.setSelectedIndex(newIdx);
        rhythmPanel.setRhythm(newIdx);
        if (mixerVisible) mixerOverlay.refresh();
    };

    // ── RhythmPanel status ────────────────────────────────────────────────────
    rhythmPanel.onStatusUpdate = [this](const juce::String& name,
                                        const juce::String& val,
                                        juce::Colour col)
    {
        statusBar.showParam(name, val, col);
    };

    rhythmPanel.onRhythmRenamed = [this]
    {
        sidebar.repaintItems();
        if (mixerVisible) mixerOverlay.refresh();
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
        processorRef.savePreset(name, desc, category, embedSamples);
        transportBar.refreshPresets();
        showSaveDialog(false);
        if (pendingQuitCallback)
        {
            auto cb = std::move(pendingQuitCallback);
            pendingQuitCallback = nullptr;
            cb();
        }
    };
    saveDialog.onCancel = [this]
    {
        pendingQuitCallback = nullptr;
        showSaveDialog(false);
    };

    // ── Preset browser ────────────────────────────────────────────────────────
    presetBrowser.onLoadPreset = [this](const juce::File& f)
    {
        processorRef.loadPreset(f);
        sidebar.refreshItems();
        rhythmPanel.setRhythm(0);
        sidebar.setSelectedIndex(0);
        if (mixerVisible) { mixerOverlay.refresh(); mixerOverlay.loadFromAPVTS(); }
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

    setSize(1170, 870);
    setResizeLimits(780, 580, 2400, 1600);

    isStandalone = processorRef.wrapperType == juce::AudioProcessor::wrapperType_Standalone;
    loadKeybindings();
    if (isStandalone)
    {
        setWantsKeyboardFocus(true);
        addKeyListener(this);
        needsFocusGrab = true;
    }
}

PluginEditor::~PluginEditor()
{
    if (isStandalone)
        removeKeyListener(this);
    setLookAndFeel(nullptr);
}

//==============================================================================
bool PluginEditor::keyPressed(const juce::KeyPress& key, juce::Component*)
{
    // Don't intercept when a text editor or button has focus — they handle keys themselves,
    // and the global listener fires before the focused component so we'd double-fire.
    auto* focused = juce::Component::getCurrentlyFocusedComponent();
    if (dynamic_cast<juce::TextEditor*>(focused)) return false;
    if (dynamic_cast<juce::Button*>(focused))     return false;

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
    rhythmPanel     .setAlpha(1.0f);
    mixerOverlay    .setAlpha(1.0f);
    presetBrowser   .setAlpha(1.0f);
    settingsOverlay .setAlpha(1.0f);
    midiPresetsPanel.setAlpha(1.0f);

    mixerVisible       = false;
    transportBar.setMixerActive(false);
    aboutVisible       = false;
    saveVisible        = false;
    browserVisible     = false;
    settingsVisible    = false;
    midiPresetsVisible = false;

    mixerOverlay    .setVisible(false);
    aboutPanel      .setVisible(false);
    saveDialog      .setVisible(false);
    presetBrowser   .setVisible(false);
    settingsOverlay .setVisible(false);
    midiPresetsPanel.setVisible(false);
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
        saveDialog.setKnownCategories(presetBrowser.getCategories());
    }
    saveDialog.setVisible(show);
    saveDialog.toFront(false);
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

//==============================================================================
void PluginEditor::paint(juce::Graphics& g)
{
    g.fillAll(MuClidLookAndFeel::colour(MuClidLookAndFeel::windowBackground));
}

void PluginEditor::resized()
{
    const int w          = getWidth();
    const int h          = getHeight();
    const int statusH    = 20;
    const int transportH = 36;
    const int bannerH    = processorRef.isLicensed() ? 0 : kDemoBannerH;
    const int contentH   = h - transportH - statusH - bannerH;

    const juce::Rectangle<int> mainArea { RhythmSidebar::kWidth, transportH,
                                          w - RhythmSidebar::kWidth, contentH };

    transportBar.setBounds(0, 0, w, transportH);
    sidebar.setBounds(0, transportH, RhythmSidebar::kWidth, contentH);

    rhythmPanel     .setBounds(mainArea);
    mixerOverlay    .setBounds(mainArea);
    presetBrowser   .setBounds(mainArea);
    settingsOverlay .setBounds(mainArea);
    midiPresetsPanel.setBounds(mainArea);

    // Modal overlays span the full editor area
    aboutPanel .setBounds(getLocalBounds());
    saveDialog .setBounds(getLocalBounds());

    if (bannerH > 0)
        demoBanner.setBounds(0, h - statusH - bannerH, w, bannerH);

    statusBar.setBounds(0, h - statusH, w, statusH);
}
