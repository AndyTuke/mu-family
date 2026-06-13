#include "TransportBar.h"

static const juce::String kPlay = juce::String(juce::CharPointer_UTF8("\xe2\x96\xb6"));
static const juce::String kStop = juce::String(juce::CharPointer_UTF8("\xe2\x96\xa0"));
static const juce::String kGear = juce::String(juce::CharPointer_UTF8("\xe2\x9a\x99"));

TransportBar::TransportBar(ProcessorBase& p)
    : proc(p),
      isStandalone(p.wrapperType == juce::AudioProcessor::wrapperType_Standalone)
{
    playBtn.onClick = [this]
    {
        if (!isStandalone) return;
        proc.toggleInternalPlay();
        refreshPlayBtn();
    };
    playBtn.setEnabled(true); // always enabled so colour reflects state; click is no-op in plugin
    addAndMakeVisible(playBtn);

    if (isStandalone)
    {
        bpmInput.setValue((int)proc.getInternalBpm());
        bpmInput.onChange = [this](int v) {
            proc.setInternalBpm((double)v);
            if (onStatusUpdate) onStatusUpdate("BPM", juce::String(v));
        };
        bpmInput.setShowStepButtons(false);
        bpmInput.setLabelInline(true);
        addAndMakeVisible(bpmInput);
    }

    posLabel.setJustificationType(juce::Justification::centred);
    posLabel.setFont(juce::Font(juce::FontOptions{}.withHeight(11.0f)));
    posLabel.setText("1.1.1", juce::dontSendNotification);
    addAndMakeVisible(posLabel);

    presetDropdown.setPlaceholderText("<unnamed preset>");
    presetDropdown.onChange = [this](int id)
    {
        int idx = id - 1;
        if (idx >= 0 && idx < (int)presetFiles.size())
        {
            // don't clear the selection — keep the loaded preset's name
            // visible so the user always sees what's currently loaded. The
            // editor's onPresetSelected does the actual load.
            if (onPresetSelected) onPresetSelected(presetFiles[idx]);
        }
    };
    addAndMakeVisible(presetDropdown);

    // Staging badge for a pending full-preset hot-swap — orange "SWP" pill on the
    // preset name, mirroring the rhythm hot-swap badge on the sidebar items.
    // Added after (on top of) the dropdown; mouse-transparent so it doesn't block it.
    presetStagingBadge.setText("SWP", juce::dontSendNotification);
    presetStagingBadge.setJustificationType(juce::Justification::centred);
    presetStagingBadge.setFont(juce::Font(juce::FontOptions{}.withHeight(8.0f)));
    presetStagingBadge.setColour(juce::Label::backgroundColourId, juce::Colours::orange.withAlpha(0.85f));
    presetStagingBadge.setColour(juce::Label::textColourId, juce::Colours::black);
    presetStagingBadge.setInterceptsMouseClicks(false, false);
    addChildComponent(presetStagingBadge);   // hidden until a swap is staged

    newBtn.onClick  = [this] { if (onNewPreset)  onNewPreset();  };
    addAndMakeVisible(newBtn);

    saveBtn.onClick = [this] { if (onSavePreset) onSavePreset(); };
    addAndMakeVisible(saveBtn);

    mixerBtn.setClickingTogglesState(true);
    mixerBtn.onClick = [this] { if (onMixerToggle) onMixerToggle(); };
    addAndMakeVisible(mixerBtn);

    gearBtn.setButtonText(kGear);
    gearBtn.onClick = [this] { if (onSettingsToggle) onSettingsToggle(); };
    addAndMakeVisible(gearBtn);

    populatePresetDropdown();
    refreshPlayBtn();
    startTimerHz(30);
}

TransportBar::~TransportBar()
{
    stopTimer();
}

void TransportBar::setLogoText(const juce::String& text)
{
    logoText = text;
    repaint();
}

void TransportBar::setShowPresetControls(bool show)
{
    showPresetControls = show;
    presetDropdown.setVisible(show);
    newBtn        .setVisible(show);
    saveBtn       .setVisible(show);
    if (!show) presetStagingBadge.setVisible(false);
    resized();
}

void TransportBar::setShowMixerToggle(bool show)
{
    showMixerToggle = show;
    mixerBtn.setVisible(show);
    resized();
}

void TransportBar::setShowSettingsButton(bool show)
{
    showSettingsButton = show;
    gearBtn.setVisible(show);
    resized();
}

void TransportBar::setLoopSection(juce::Component* component, int width)
{
    if (loopSection == component && loopSectionWidth == width) return;
    if (loopSection != nullptr)
        removeChildComponent(loopSection);
    loopSection      = component;
    loopSectionWidth = (component != nullptr) ? juce::jmax(0, width) : 0;
    if (loopSection != nullptr)
        addAndMakeVisible(loopSection);
    resized();
}

void TransportBar::timerCallback()
{
    refreshPlayBtn();
    updatePositionLabel();

    if (isStandalone)
    {
        const bool midiClockBpm = proc.getMidiSyncEnabled() && proc.getMidiSyncMessages() != 1;
        if (midiClockBpm)
            bpmInput.setValue((int)std::round(proc.getMidiClockBpm()));
        bpmInput.setEnabled(!midiClockBpm);

        const bool midiTransport = proc.getMidiSyncEnabled() && proc.getMidiSyncMessages() != 0;
        playBtn.setEnabled(!midiTransport);
    }

    // Show the staging badge while a full-preset hot-swap is queued for the loop point.
    if (showPresetControls)
        presetStagingBadge.setVisible(proc.hasPendingFullPreset());
}

void TransportBar::refreshPlayBtn()
{
    using Id = MuLookAndFeel::ColourIds;
    if (isStandalone)
    {
        const bool playing = proc.isInternalPlaying() || proc.isMidiClockPlaying();
        playBtn.setButtonText(playing ? kStop : kPlay);
        if (playing)
        {
            playBtn.setColour(juce::TextButton::buttonColourId,  MuLookAndFeel::colour(Id::transportWhilePlayingBg));
            playBtn.setColour(juce::TextButton::textColourOffId, MuLookAndFeel::colour(Id::textBright));
        }
        else
        {
            playBtn.setColour(juce::TextButton::buttonColourId,  MuLookAndFeel::colour(Id::transportWhileStoppedBg));
            playBtn.setColour(juce::TextButton::textColourOffId, MuLookAndFeel::colour(Id::textBright));
        }
    }
    else
    {
        // Reflect DAW transport state via icon + colour (matches standalone:
        // ▶ play → ■ stop while the host plays); button is non-interactive in
        // plugin mode — the host owns transport, so this is display-only.
        bool dawPlaying = false;
        if (auto* ph = proc.getPlayHead())
            if (auto pos = ph->getPosition())
                dawPlaying = pos->getIsPlaying();

        playBtn.setButtonText(dawPlaying ? kStop : kPlay);
        if (dawPlaying)
        {
            playBtn.setColour(juce::TextButton::buttonColourId,  MuLookAndFeel::colour(Id::transportWhilePlayingBg));
            playBtn.setColour(juce::TextButton::textColourOffId, MuLookAndFeel::colour(Id::textBright));
        }
        else
        {
            playBtn.setColour(juce::TextButton::buttonColourId,  MuLookAndFeel::colour(Id::transportWhileStoppedBg));
            playBtn.setColour(juce::TextButton::textColourOffId, MuLookAndFeel::colour(Id::textBright));
        }
    }
}

void TransportBar::updatePositionLabel()
{
    double beatPos = 0.0;
    bool   gotPos  = false;

    if (!isStandalone)
    {
        if (auto* ph = proc.getPlayHead())
        {
            if (auto pos = ph->getPosition())
            {
                if (auto ppq = pos->getPpqPosition())
                {
                    beatPos = *ppq;
                    gotPos  = true;
                }
            }
        }
    }
    else
    {
        beatPos = proc.getInternalBeatPos();
        gotPos  = true;
    }

    if (!gotPos)
    {
        posLabel.setText("---", juce::dontSendNotification);
        return;
    }

    const int beatsPerBar = 4;
    const int subsPerBeat = 4;

    int totalBeats = (int)beatPos;
    int sub        = (int)((beatPos - totalBeats) * subsPerBeat) + 1;
    int bar        = totalBeats / beatsPerBar + 1;
    int beat       = totalBeats % beatsPerBar + 1;

    posLabel.setText(juce::String(bar) + "." + juce::String(beat) + "." + juce::String(sub),
                     juce::dontSendNotification);
}

void TransportBar::populatePresetDropdown()
{
    presetFiles.clear();
    presetDropdown.clear();

    auto dir = proc.getPresetsDir();
    if (!dir.isDirectory()) return;

    const juce::String wildcard = "*." + proc.getFullPresetExtension();

    struct Entry { juce::File file; juce::String name, category; };
    std::vector<Entry> entries;

    for (const auto& f : dir.findChildFiles(juce::File::findFiles, false, wildcard))
    {
        if (f.getFileNameWithoutExtension().equalsIgnoreCase("_default")) continue;
        Entry e { f, f.getFileNameWithoutExtension(), "Uncategorised" };
        if (auto xml = juce::parseXML(f))
        {
            auto state = juce::ValueTree::fromXml(*xml);
            juce::String cat = state.getProperty("presetCategory", "").toString();
            if (cat.isNotEmpty() && cat != "All" && cat != "Uncategorised")
                e.category = cat;
        }
        entries.push_back(std::move(e));
    }

    // Sort: named categories alphabetically, "Uncategorised" last, name within category.
    std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) {
        const bool aU = (a.category == "Uncategorised");
        const bool bU = (b.category == "Uncategorised");
        if (aU != bU) return bU;  // uncategorised goes last
        const int cc = a.category.compareIgnoreCase(b.category);
        return cc != 0 ? cc < 0 : a.name.compareIgnoreCase(b.name) < 0;
    });

    // Determine whether there are multiple distinct categories.
    bool multiCat = false;
    if (!entries.empty())
    {
        const auto& first = entries.front().category;
        for (const auto& e : entries)
            if (e.category.compareIgnoreCase(first) != 0) { multiCat = true; break; }
    }

    juce::String currentCat;
    for (const auto& e : entries)
    {
        if (multiCat && e.category != currentCat)
        {
            currentCat = e.category;
            presetDropdown.addSectionHeading(currentCat);
        }
        presetFiles.push_back(e.file);
        presetDropdown.addItem(e.name, (int)presetFiles.size());
    }
}

void TransportBar::refreshPresets()
{
    populatePresetDropdown();
}

void TransportBar::setLoadedPreset(const juce::File& file)
{
    // select the dropdown item whose stored File matches the loaded one
    // (by full path) so the dropdown text reflects the active preset. An
    // invalid file or one not in the dropdown list reverts to the placeholder.
    if (!file.existsAsFile())
    {
        presetDropdown.setSelectedId(0, juce::dontSendNotification);
        return;
    }
    const auto target = file.getFullPathName();
    for (int i = 0; i < (int)presetFiles.size(); ++i)
    {
        if (presetFiles[i].getFullPathName() == target)
        {
            presetDropdown.setSelectedId(i + 1, juce::dontSendNotification);
            return;
        }
    }
    presetDropdown.setSelectedId(0, juce::dontSendNotification);
}

juce::File TransportBar::getLoadedPresetFile() const
{
    const int id = presetDropdown.getSelectedId();
    const int idx = id - 1;
    if (idx >= 0 && idx < (int)presetFiles.size())
        return presetFiles[idx];
    return {};
}

void TransportBar::setMixerActive(bool active)
{
    mixerBtn.setButtonText(active ? "Sequencer" : "Mixer");
}

int TransportBar::getPresetDropdownLeft() const noexcept { return presetDropdown.getX(); }

void TransportBar::setSaveEnabled(bool enabled)
{
    saveBtn.setEnabled(enabled);
    saveBtn.setAlpha(enabled ? 1.0f : 0.35f);
}

void TransportBar::mouseDown(const juce::MouseEvent& e)
{
    if (e.x < mu_ui::s(kLogoW) && onLogoClicked)
        onLogoClicked();
}

void TransportBar::paint(juce::Graphics& g)
{
    using Id = MuLookAndFeel::ColourIds;

    g.setColour(MuLookAndFeel::colour(Id::panelBackground));
    g.fillAll();

    // Two sub-pane borders: transport (play+bpm+pos) and optional loop section
    // (when the product has supplied a loop component via setLoopSection).
    const juce::Colour borderCol = MuLookAndFeel::colour(Id::segmentInactiveBorder);
    g.setColour(borderCol);
    if (!transportPaneBounds.isEmpty())
        g.drawRoundedRectangle(transportPaneBounds.toFloat(), mu_ui::sf(3.0f), 1.0f);
    if (!loopPaneBounds.isEmpty())
        g.drawRoundedRectangle(loopPaneBounds.toFloat(), mu_ui::sf(3.0f), 1.0f);

    g.setColour(MuLookAndFeel::colour(Id::headingText));
    g.setFont(juce::Font(juce::FontOptions{}.withHeight(mu_ui::sf(14.0f))));
    g.drawText(logoText, mu_ui::s(8), 0, mu_ui::s(kLogoW - 8), getHeight(),
               juce::Justification::centredLeft, false);
}

void TransportBar::resized()
{
    using mu_ui::s;
    const int h      = getHeight();
    const int inset  = s(2);   // pane border vertical inset
    const int pad    = s(5);   // item vertical padding — gives visible gap top/bottom
    const int btnY   = pad;
    const int btnH   = h - 2 * pad;
    const int gap    = s(kGap);
    const int padIn  = s(6);   // inner pane padding — matches inter-item gap for even ends

    // ── Transport sub-pane: [play] [bpm] [pos] ────────────────────────────
    const int tpOuterX = s(kLogoW) + gap;
    int x = tpOuterX + padIn;

    playBtn.setBounds(x, btnY, s(kPlayW), btnH);
    x += s(kPlayW) + gap;

    if (isStandalone)
    {
        bpmInput.setBounds(x, btnY, s(kBpmW), btnH);
        x += s(kBpmW) + gap;
    }

    posLabel.setBounds(x, btnY, s(kPosW), btnH);
    x += s(kPosW) + padIn;

    transportPaneBounds = { tpOuterX, inset, x - tpOuterX, h - 2 * inset };

    // ── Optional loop section: positioned right of the transport pane ─────
    if (loopSection != nullptr && loopSectionWidth > 0)
    {
        const int lpOuterX = transportPaneBounds.getRight() + gap;
        loopPaneBounds = { lpOuterX, inset, loopSectionWidth, h - 2 * inset };
        loopSection->setBounds(loopPaneBounds);
    }
    else
    {
        loopPaneBounds = {};
    }

    // ── Right group (right to left): Mixer | Gear | Save | Preset ─────────
    int rightEdge = getWidth() - gap;

    if (showMixerToggle)
    {
        mixerBtn.setBounds(rightEdge - s(kMixerW), btnY, s(kMixerW), btnH);
        rightEdge -= s(kMixerW) + gap;
    }

    if (showSettingsButton)
    {
        gearBtn.setBounds(rightEdge - s(kGearW), btnY, s(kGearW), btnH);
        rightEdge -= s(kGearW) + gap;
    }

    if (showPresetControls)
    {
        saveBtn.setBounds(rightEdge - s(kSaveW), btnY, s(kSaveW), btnH);
        rightEdge -= s(kSaveW) + gap;

        newBtn.setBounds(rightEdge - s(kNewW), btnY, s(kNewW), btnH);
        rightEdge -= s(kNewW) + gap;

        const int presetLeft = (loopPaneBounds.isEmpty() ? transportPaneBounds.getRight()
                                                          : loopPaneBounds.getRight()) + gap;
        presetDropdown.setBounds(presetLeft, btnY, rightEdge - presetLeft, btnH);

        // Staging badge: small pill at the top-right of the preset dropdown.
        const int badgeW = s(28);
        const int badgeH = s(11);
        presetStagingBadge.setBounds(presetDropdown.getRight() - badgeW - s(4),
                                     presetDropdown.getY() + s(2), badgeW, badgeH);
    }
}
