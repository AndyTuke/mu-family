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
        if (onStatusUpdate) onStatusUpdate("Master Loop", loopDropdown.getText());
    };
    addAndMakeVisible(loopDropdown);

    loopStepLabel.setJustificationType(juce::Justification::centredLeft);
    loopStepLabel.setFont(juce::Font(juce::FontOptions{}.withHeight(11.0f)));
    syncLoopDropdownFromAPVTS();
    addAndMakeVisible(loopStepLabel);

    // Catch host automation of mstrLoop so the dropdown / step label stay in
    // sync with the DAW state. parameterChanged marshals to the message thread.
    proc.apvts.addParameterListener("mstrLoop", this);

#if !MUCLID_LITE_BUILD
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

    newBtn.onClick  = [this] { if (onNewPreset)  onNewPreset();  };
    addAndMakeVisible(newBtn);

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
    proc.apvts.removeParameterListener("mstrLoop", this);
    stopTimer();
}

void TransportBar::syncLoopDropdownFromAPVTS()
{
    const int paramVal = (int) proc.apvts.getRawParameterValue("mstrLoop")->load();
    loopStepLabel.setVisible(paramVal > 0);
    loopDropdown.setSelectedId(paramVal + 1, false);
}

void TransportBar::parameterChanged(const juce::String& parameterID, float /*newValue*/)
{
    if (parameterID != "mstrLoop") return;

    // host automation can fire on the audio thread; juce::Slider / DropdownSelect
    // state isn't safe to mutate off the message thread.
    juce::Component::SafePointer<TransportBar> safe(this);
    auto refresh = [safe] { if (auto* self = safe.getComponent()) self->syncLoopDropdownFromAPVTS(); };
    if (juce::MessageManager::getInstance()->isThisTheMessageThread())
        refresh();
    else
        juce::MessageManager::callAsync(std::move(refresh));
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
            playBtn.setColour(juce::TextButton::buttonColourId,  MuClidLookAndFeel::colour(Id::transportWhilePlayingBg));
            playBtn.setColour(juce::TextButton::textColourOffId, MuClidLookAndFeel::colour(Id::textBright));
        }
        else
        {
            playBtn.setColour(juce::TextButton::buttonColourId,  MuClidLookAndFeel::colour(Id::transportWhileStoppedBg));
            playBtn.setColour(juce::TextButton::textColourOffId, MuClidLookAndFeel::colour(Id::textBright));
        }
    }
    else
    {
        playBtn.setButtonText(kPlay);
        playBtn.setColour(juce::TextButton::buttonColourId,
                          MuClidLookAndFeel::colour(Id::segmentInactiveBg));
        playBtn.setColour(juce::TextButton::textColourOffId, MuClidLookAndFeel::colour(Id::textDisabledButton));
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

    struct Entry { juce::File file; juce::String name, category; };
    std::vector<Entry> entries;

    for (const auto& f : dir.findChildFiles(juce::File::findFiles, false, "*.muclid"))
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
    using Id = MuClidLookAndFeel::ColourIds;

    g.setColour(MuClidLookAndFeel::colour(Id::panelBackground));
    g.fillAll();

    // Two sub-pane borders: transport (play+bpm+pos) and loop (loop dropdown+counter).
    const juce::Colour borderCol = MuClidLookAndFeel::colour(Id::segmentInactiveBorder);
    g.setColour(borderCol);
    if (!transportPaneBounds.isEmpty())
        g.drawRoundedRectangle(transportPaneBounds.toFloat(), mu_ui::sf(3.0f), 1.0f);
    if (!loopPaneBounds.isEmpty())
        g.drawRoundedRectangle(loopPaneBounds.toFloat(), mu_ui::sf(3.0f), 1.0f);

    g.setColour(MuClidLookAndFeel::colour(Id::headingText));
    g.setFont(juce::Font(juce::FontOptions{}.withHeight(mu_ui::sf(14.0f))));
    g.drawText(juce::String(juce::CharPointer_UTF8("\xce\xbc-Clid")),
               mu_ui::s(8), 0, mu_ui::s(kLogoW - 8), getHeight(),
               juce::Justification::centredLeft, false);
}

void TransportBar::resized()
{
    using mu_ui::s;
    const int h      = getHeight();
    const int inset  = s(2);   // pane border vertical inset
    const int pad    = s(3);   // item vertical padding within pane
    const int itemY  = pad;
    const int itemH  = h - 2 * pad;
    const int posH   = s(14);  // position label height (text only)
    const int posY   = (h - posH) / 2;
    const int btnH   = itemH;
    const int btnY   = itemY;
    const int gap    = s(kGap);
    const int padIn  = s(5);   // inner pane padding

    // ── Transport sub-pane: [play] [bpm] [pos] ────────────────────────────
    const int tpOuterX = s(kLogoW) + gap;
    int x = tpOuterX + padIn;

    playBtn.setBounds(x, btnY, s(kPlayW), btnH);
    x += s(kPlayW) + gap;

    if (isStandalone)
    {
        bpmInput.setBounds(x, itemY, s(kBpmW), itemH);
        x += s(kBpmW) + gap;
    }

    posLabel.setBounds(x, posY, s(kPosW), posH);
    x += s(kPosW) + padIn;

    transportPaneBounds = { tpOuterX, inset, x - tpOuterX, h - 2 * inset };

    // ── Loop sub-pane: [Loop:] [dropdown] [step counter] ──────────────────
    const int lpOuterX = transportPaneBounds.getRight() + gap;
    x = lpOuterX + padIn;

    loopLabel.setBounds(x, btnY, s(kLoopLabelW), btnH);
    x += s(kLoopLabelW) + s(2);
    loopDropdown.setBounds(x, btnY, s(kLoopW), btnH);
    x += s(kLoopW) + s(2);
    loopStepLabel.setBounds(x, btnY, s(kLoopStepW), btnH);
    x += s(kLoopStepW) + padIn;

    loopPaneBounds = { lpOuterX, inset, x - lpOuterX, h - 2 * inset };

    // ── Right group (right to left): Mixer | Gear | Save | Preset ─────────
#if MUCLID_LITE_BUILD
    gearBtn.setBounds(getWidth() - gap - s(kGearW), btnY, s(kGearW), btnH);
#else
    int rightEdge = getWidth() - gap;
    mixerBtn.setBounds(rightEdge - s(kMixerW), btnY, s(kMixerW), btnH);
    rightEdge -= s(kMixerW) + gap;

    gearBtn.setBounds(rightEdge - s(kGearW), btnY, s(kGearW), btnH);
    rightEdge -= s(kGearW) + gap;

    saveBtn.setBounds(rightEdge - s(kSaveW), btnY, s(kSaveW), btnH);
    rightEdge -= s(kSaveW) + gap;

    newBtn.setBounds(rightEdge - s(kNewW), btnY, s(kNewW), btnH);
    rightEdge -= s(kNewW) + gap;

    const int presetLeft = loopPaneBounds.getRight() + gap;
    presetDropdown.setBounds(presetLeft, btnY, rightEdge - presetLeft, btnH);
#endif
}
