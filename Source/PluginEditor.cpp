#include "PluginEditor.h"

PluginEditor::PluginEditor(PluginProcessor& p)
    : AudioProcessorEditor(&p), processorRef(p),
      transportBar(p), sidebar(p), rhythmPanel(p),
      mixerOverlay(p, p.mixerEngine),
      settingsOverlay(p)
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
            statusBar.showParam("Demo", "2-rhythm limit — purchase a license to unlock all 8",
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
    };
    saveDialog.onCancel = [this] { showSaveDialog(false); };

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

    setSize(1170, 870);
    setResizeLimits(780, 580, 2400, 1600);
}

PluginEditor::~PluginEditor()
{
    setLookAndFeel(nullptr);
}

//==============================================================================
void PluginEditor::hideAllOverlays()
{
    // Cancel any in-progress fade animations before forcing visibility states
    animator.cancelAnimation(&rhythmPanel,    true);
    animator.cancelAnimation(&mixerOverlay,   true);
    animator.cancelAnimation(&presetBrowser,  true);
    animator.cancelAnimation(&settingsOverlay, true);
    rhythmPanel    .setAlpha(1.0f);
    mixerOverlay   .setAlpha(1.0f);
    presetBrowser  .setAlpha(1.0f);
    settingsOverlay.setAlpha(1.0f);

    mixerVisible    = false;
    transportBar.setMixerActive(false);
    aboutVisible    = false;
    saveVisible     = false;
    browserVisible  = false;
    settingsVisible = false;

    mixerOverlay  .setVisible(false);
    aboutPanel    .setVisible(false);
    saveDialog    .setVisible(false);
    presetBrowser .setVisible(false);
    settingsOverlay.setVisible(false);
    rhythmPanel   .setVisible(true);
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

    rhythmPanel    .setBounds(mainArea);
    mixerOverlay   .setBounds(mainArea);
    presetBrowser  .setBounds(mainArea);
    settingsOverlay.setBounds(mainArea);

    // Modal overlays span the full editor area
    aboutPanel .setBounds(getLocalBounds());
    saveDialog .setBounds(getLocalBounds());

    if (bannerH > 0)
        demoBanner.setBounds(0, h - statusH - bannerH, w, bannerH);

    statusBar.setBounds(0, h - statusH, w, statusH);
}
