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
        addAndMakeVisible(bpmInput);
    }

    posLabel.setJustificationType(juce::Justification::centred);
    posLabel.setFont(juce::Font(juce::FontOptions{}.withHeight(11.0f)));
    posLabel.setText("1.1.1", juce::dontSendNotification);
    addAndMakeVisible(posLabel);

    rhythmCountLabel.setJustificationType(juce::Justification::centred);
    rhythmCountLabel.setFont(juce::Font(juce::FontOptions{}.withHeight(11.0f)));
    addAndMakeVisible(rhythmCountLabel);

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

    presetDropdown.onChange = [this](int id)
    {
        int idx = id - 1;
        if (idx >= 0 && idx < (int)presetFiles.size())
        {
            if (onPresetSelected) onPresetSelected(presetFiles[idx]);
            // Reset selection so the same preset can be reloaded again.
            presetDropdown.setSelectedId(0, false);
        }
    };
    addAndMakeVisible(presetDropdown);

    saveBtn.onClick = [this] { if (onSavePreset) onSavePreset(); };
    addAndMakeVisible(saveBtn);

    gearBtn.setButtonText(kGear);
    gearBtn.onClick = [this] { if (onSettingsToggle) onSettingsToggle(); };
    addAndMakeVisible(gearBtn);

    mixerBtn.setClickingTogglesState(true);
    mixerBtn.onClick = [this] { if (onMixerToggle) onMixerToggle(); };
    addAndMakeVisible(mixerBtn);

    populatePresetDropdown();
    refreshPlayBtn();
    updateRhythmCount();
    startTimerHz(10);
}

TransportBar::~TransportBar()
{
    stopTimer();
}

void TransportBar::timerCallback()
{
    refreshPlayBtn();
    updatePositionLabel();
    updateRhythmCount();

    if (loopStepLabel.isVisible())
    {
        const int steps   = proc.sequencer.getMasterLoopSteps();
        const int current = proc.sequencer.getMasterLoopCurrentStep() + 1; // 1-based display
        loopStepLabel.setText(juce::String(current) + " / " + juce::String(steps),
                              juce::dontSendNotification);
    }
}

void TransportBar::refreshPlayBtn()
{
    if (isStandalone)
        playBtn.setButtonText(proc.isInternalPlaying() ? kStop : kPlay);
    else
        playBtn.setButtonText(kPlay);
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

void TransportBar::updateRhythmCount()
{
    int n = proc.getNumRhythms();
    rhythmCountLabel.setText(juce::String(n) + "/" + juce::String(SequencerEngine::MaxRhythms),
                             juce::dontSendNotification);
}

void TransportBar::populatePresetDropdown()
{
    presetFiles.clear();
    presetDropdown.clear();

    auto dir = proc.getPresetsDir();
    if (dir.isDirectory())
    {
        for (const auto& f : dir.findChildFiles(juce::File::findFiles, false, "*.muclid"))
        {
            presetFiles.push_back(f);
            presetDropdown.addItem(f.getFileNameWithoutExtension(), (int)presetFiles.size());
        }
    }

    if (presetFiles.empty())
        presetDropdown.addItem("No presets", 9999);
}

void TransportBar::refreshPresets()
{
    populatePresetDropdown();
}

void TransportBar::setMixerActive(bool active)
{
    mixerBtn.setButtonText(active ? "Sequencer" : "Mixer");
}

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

    // Bordered panel around transport controls (excluding the logo area).
    const int panelX = kLogoW;
    const int panelY = 4;
    const int panelH = getHeight() - 8;
    g.setColour(MuClidLookAndFeel::colour(Id::segmentInactiveBorder));
    g.drawRect(panelX, panelY, getWidth() - kLogoW - 4, panelH, 1);

    g.setColour(MuClidLookAndFeel::colour(Id::headingText));
    g.setFont(juce::Font(juce::FontOptions{}.withHeight(14.0f)));
    g.drawText("mu-Clid", 8, 0, kLogoW - 8, getHeight(),
               juce::Justification::centredLeft, false);
}

void TransportBar::resized()
{
    const int h    = getHeight();
    const int btnH = 28;
    const int btnY = (h - btnH) / 2;

    // Left group: Logo | Play | BPM | Loop label | Loop dropdown | Position
    int leftX = kLogoW + kGap;
    playBtn.setBounds(leftX, btnY, kPlayW, btnH);
    leftX += kPlayW + kGap;

    if (isStandalone)
    {
        bpmInput.setBounds(leftX, (h - 28) / 2, kBpmW, 28);
        leftX += kBpmW + kGap;
    }

    loopLabel.setBounds(leftX, btnY, kLoopLabelW, btnH);
    leftX += kLoopLabelW + 2;
    loopDropdown.setBounds(leftX, btnY, kLoopW, btnH);
    leftX += kLoopW + 2;
    loopStepLabel.setBounds(leftX, btnY, kLoopStepW, btnH);
    leftX += kLoopStepW + kGap;

    posLabel.setBounds(leftX, btnY, kPosW, btnH);

    // Right group (right to left): Mixer | Gear | Save | Preset | +Rhythm | RhythmCount
    int rightEdge = getWidth() - kGap;
    mixerBtn.setBounds(rightEdge - kMixerW, btnY, kMixerW, btnH);
    rightEdge -= kMixerW + kGap;

    gearBtn.setBounds(rightEdge - kGearW, btnY, kGearW, btnH);
    rightEdge -= kGearW + kGap;

    saveBtn.setBounds(rightEdge - kSaveW, btnY, kSaveW, btnH);
    rightEdge -= kSaveW + kGap;

    presetDropdown.setBounds(rightEdge - kPresetW, btnY, kPresetW, btnH);
    rightEdge -= kPresetW + kGap;

    rhythmCountLabel.setBounds(rightEdge - kRhCountW, btnY, kRhCountW, btnH);
}
