#include "TransportBar.h"

static const juce::String kPlay = juce::String(juce::CharPointer_UTF8("\xe2\x96\xb6"));
static const juce::String kStop = juce::String(juce::CharPointer_UTF8("\xe2\x96\xa0"));
static const juce::String kGear = juce::String(juce::CharPointer_UTF8("\xe2\x9a\x99"));

TransportBar::TransportBar(PluginProcessor& p)
    : proc(p),
      isStandalone(p.wrapperType == juce::AudioProcessor::wrapperType_Standalone)
{
    playBtn.onClick = [this]
    {
        proc.toggleInternalPlay();
        refreshPlayBtn();
    };
    playBtn.setEnabled(isStandalone);
    addAndMakeVisible(playBtn);

    if (isStandalone)
    {
        bpmInput.setValue((int)proc.getInternalBpm());
        bpmInput.onChange = [this](int v) { proc.setInternalBpm((double)v); };
        bpmInput.setShowStepButtons(false);
        bpmInput.setLabelInline(true);
        addAndMakeVisible(bpmInput);
    }

    posLabel.setJustificationType(juce::Justification::centred);
    posLabel.setFont(juce::Font(juce::FontOptions{}.withHeight(11.0f)));
    posLabel.setText("1.1.1", juce::dontSendNotification);
    addAndMakeVisible(posLabel);

    loopLabel.setText("Loop:", juce::dontSendNotification);
    loopLabel.setJustificationType(juce::Justification::centredRight);
    loopLabel.setFont(juce::Font(juce::FontOptions{}.withHeight(11.0f)));
    addAndMakeVisible(loopLabel);

    loopDropdown.addItem(juce::String::charToString(0x221E), 1); // ∞ = pattern reset length disabled
    for (int i = 1; i <= 16; ++i)
        loopDropdown.addItem(juce::String(i * 16) + " steps", i + 1);
    loopDropdown.setSelectedId(1, false); // default: off
    loopDropdown.onChange = [this](int id)
    {
        const int paramVal = id - 1; // id=1→0 (off), id=2→1 (16 steps), ...
        if (auto* p = proc.apvts.getParameter("mstrLoop"))
            p->setValueNotifyingHost(p->convertTo0to1((float)paramVal));
        loopStepLabel.setVisible(id != 1);
    };
    addAndMakeVisible(loopDropdown);

    loopStepLabel.setJustificationType(juce::Justification::centredLeft);
    loopStepLabel.setFont(juce::Font(juce::FontOptions{}.withHeight(11.0f)));
    // Sync initial visibility with the current loop param.
    {
        const int paramVal = (int)proc.apvts.getRawParameterValue("mstrLoop")->load();
        loopStepLabel.setVisible(paramVal > 0);
        if (paramVal > 0)
            loopDropdown.setSelectedId(paramVal + 1, false);
    }
    addAndMakeVisible(loopStepLabel);

#if !MUCLID_LITE_BUILD
    presetDropdown.setPlaceholderText("<unnamed preset>");
    presetDropdown.onChange = [this](int id)
    {
        int idx = id - 1;
        if (idx >= 0 && idx < (int)presetFiles.size())
        {
            // #241: don't clear the selection — keep the loaded preset's name
            // visible so the user always sees what's currently loaded. The
            // editor's onPresetSelected does the actual load.
            if (onPresetSelected) onPresetSelected(presetFiles[idx]);
        }
    };
    addAndMakeVisible(presetDropdown);

    saveBtn.onClick = [this] { if (onSavePreset) onSavePreset(); };
    addAndMakeVisible(saveBtn);

    mixerBtn.setClickingTogglesState(true);
    mixerBtn.onClick = [this] { if (onMixerToggle) onMixerToggle(); };
    addAndMakeVisible(mixerBtn);
#endif

    gearBtn.setButtonText(kGear);
    gearBtn.onClick = [this] { if (onSettingsToggle) onSettingsToggle(); };
    addAndMakeVisible(gearBtn);

#if !MUCLID_LITE_BUILD
    populatePresetDropdown();
#endif
    refreshPlayBtn();
    startTimerHz(30);
}

TransportBar::~TransportBar()
{
    stopTimer();
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

    if (loopStepLabel.isVisible())
    {
        const int steps   = proc.sequencer.getMasterLoopSteps();
        const int current = proc.sequencer.getMasterLoopCurrentStep() + 1;
        loopStepLabel.setText(juce::String(current) + " / " + juce::String(steps),
                              juce::dontSendNotification);
    }
}

void TransportBar::refreshPlayBtn()
{
    using Id = MuClidLookAndFeel::ColourIds;
    if (isStandalone)
    {
        const bool playing = proc.isInternalPlaying() || proc.isMidiClockPlaying();
        playBtn.setButtonText(playing ? kStop : kPlay);
        if (playing)
        {
            playBtn.setColour(juce::TextButton::buttonColourId,  juce::Colour(0xff5c1a1a));
            playBtn.setColour(juce::TextButton::textColourOffId, juce::Colour(0xffeeeeee));
        }
        else
        {
            playBtn.setColour(juce::TextButton::buttonColourId,  juce::Colour(0xff1a4a26));
            playBtn.setColour(juce::TextButton::textColourOffId, juce::Colour(0xffeeeeee));
        }
    }
    else
    {
        playBtn.setButtonText(kPlay);
        playBtn.setColour(juce::TextButton::buttonColourId,
                          MuClidLookAndFeel::colour(Id::segmentInactiveBg));
        playBtn.setColour(juce::TextButton::textColourOffId, juce::Colour(0xff666666));
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
    if (dir.isDirectory())
    {
        // #251: sort presets alphabetically by display name (filename without
        // extension), case-insensitive. JUCE's findChildFiles order is
        // filesystem-dependent (alphabetical on NTFS, mtime on others), so an
        // explicit sort makes the dropdown predictable across machines.
        auto files = dir.findChildFiles(juce::File::findFiles, false, "*.muclid");
        std::sort(files.begin(), files.end(), [](const juce::File& a, const juce::File& b) {
            return a.getFileNameWithoutExtension().compareIgnoreCase(b.getFileNameWithoutExtension()) < 0;
        });
        for (const auto& f : files)
        {
            presetFiles.push_back(f);
            presetDropdown.addItem(f.getFileNameWithoutExtension(), (int)presetFiles.size());
        }
    }

    // No items means no presets; placeholder text handles the empty-state display.
}

void TransportBar::refreshPresets()
{
    populatePresetDropdown();
}

void TransportBar::setLoadedPreset(const juce::File& file)
{
    // #241: select the dropdown item whose stored File matches the loaded one
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
    if (e.x < kLogoW && onLogoClicked)
        onLogoClicked();
}

void TransportBar::paint(juce::Graphics& g)
{
    using Id = MuClidLookAndFeel::ColourIds;

    g.setColour(MuClidLookAndFeel::colour(Id::panelBackground));
    g.fillAll();

    // Two sub-pane borders: transport (play+bpm+pos) and loop (loop dropdown+counter).
    const juce::Colour borderCol = MuClidLookAndFeel::colour(Id::segmentInactiveBorder);
    g.setColour(borderCol);
    if (!transportPaneBounds.isEmpty())
        g.drawRoundedRectangle(transportPaneBounds.toFloat(), 3.0f, 1.0f);
    if (!loopPaneBounds.isEmpty())
        g.drawRoundedRectangle(loopPaneBounds.toFloat(), 3.0f, 1.0f);

    g.setColour(MuClidLookAndFeel::colour(Id::headingText));
    g.setFont(juce::Font(juce::FontOptions{}.withHeight(14.0f)));
    g.drawText("mu-Clid", 8, 0, kLogoW - 8, getHeight(),
               juce::Justification::centredLeft, false);
}

void TransportBar::resized()
{
    const int h      = getHeight();
    const int inset  = 2;   // pane border vertical inset
    const int pad    = 3;   // item vertical padding within pane
    const int itemY  = pad;
    const int itemH  = h - 2 * pad;
    const int posH   = 14;  // position label height (text only)
    const int posY   = (h - posH) / 2;
    const int btnH   = itemH;
    const int btnY   = itemY;

    // ── Transport sub-pane: [play] [bpm] [pos] ────────────────────────────
    const int tpOuterX = kLogoW + kGap;
    int x = tpOuterX + 5;   // 5 px inner left padding

    playBtn.setBounds(x, btnY, kPlayW, btnH);
    x += kPlayW + kGap;

    if (isStandalone)
    {
        bpmInput.setBounds(x, itemY, kBpmW, itemH);
        x += kBpmW + kGap;
    }

    posLabel.setBounds(x, posY, kPosW, posH);
    x += kPosW + 5;   // 5 px inner right padding

    transportPaneBounds = { tpOuterX, inset, x - tpOuterX, h - 2 * inset };

    // ── Loop sub-pane: [Loop:] [dropdown] [step counter] ──────────────────
    const int lpOuterX = transportPaneBounds.getRight() + kGap;
    x = lpOuterX + 5;  // 5 px inner left padding

    loopLabel.setBounds(x, btnY, kLoopLabelW, btnH);
    x += kLoopLabelW + 2;
    loopDropdown.setBounds(x, btnY, kLoopW, btnH);
    x += kLoopW + 2;
    loopStepLabel.setBounds(x, btnY, kLoopStepW, btnH);
    x += kLoopStepW + 5;  // 5 px inner right padding

    loopPaneBounds = { lpOuterX, inset, x - lpOuterX, h - 2 * inset };

    // ── Right group (right to left): Mixer | Gear | Save | Preset ─────────
#if MUCLID_LITE_BUILD
    gearBtn.setBounds(getWidth() - kGap - kGearW, btnY, kGearW, btnH);
#else
    int rightEdge = getWidth() - kGap;
    mixerBtn.setBounds(rightEdge - kMixerW, btnY, kMixerW, btnH);
    rightEdge -= kMixerW + kGap;

    gearBtn.setBounds(rightEdge - kGearW, btnY, kGearW, btnH);
    rightEdge -= kGearW + kGap;

    saveBtn.setBounds(rightEdge - kSaveW, btnY, kSaveW, btnH);
    rightEdge -= kSaveW + kGap;

    const int presetLeft = loopPaneBounds.getRight() + kGap;
    presetDropdown.setBounds(presetLeft, btnY, rightEdge - presetLeft, btnH);
#endif
}
