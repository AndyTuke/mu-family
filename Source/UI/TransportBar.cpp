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
    posLabel.setFont(juce::Font(11.0f));
    posLabel.setText("1.1.1", juce::dontSendNotification);
    addAndMakeVisible(posLabel);

    rhythmCountLabel.setJustificationType(juce::Justification::centred);
    rhythmCountLabel.setFont(juce::Font(11.0f));
    addAndMakeVisible(rhythmCountLabel);

    loopDropdown.addItem("Loop Off", 1);
    for (int i = 1; i <= 16; ++i)
        loopDropdown.addItem(juce::String(i * 16) + " steps", i + 1);
    loopDropdown.setSelectedId(1, false); // default: off
    loopDropdown.onChange = [this](int id)
    {
        const int paramVal = id - 1; // id=1→0 (off), id=2→1 (16 steps), ...
        if (auto* p = proc.apvts.getParameter("mstrLoop"))
            p->setValueNotifyingHost(p->convertTo0to1((float)paramVal));
    };
    addAndMakeVisible(loopDropdown);

    addRhythmBtn.onClick = [this] { if (onAddRhythm) onAddRhythm(); };
    addAndMakeVisible(addRhythmBtn);

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
    addRhythmBtn.setEnabled(n < SequencerEngine::MaxRhythms);
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

    g.setColour(MuClidLookAndFeel::colour(Id::headingText));
    g.setFont(juce::Font(14.0f));
    g.drawText("mu-Clid", 8, 0, kLogoW - 8, getHeight(),
               juce::Justification::centredLeft, false);

    g.setColour(MuClidLookAndFeel::colour(Id::segmentInactiveBorder));
    g.drawLine(0.0f, (float)(getHeight() - 1), (float)getWidth(), (float)(getHeight() - 1), 0.5f);
}

void TransportBar::resized()
{
    const int h    = getHeight();
    const int btnH = 28;
    const int btnY = (h - btnH) / 2;

    // Left: play + BPM (standalone only)
    int leftGroupW = kPlayW + (isStandalone ? kGap + kBpmW : 0);
    int leftX = kLogoW + kGap;

    playBtn.setBounds(leftX, btnY, kPlayW, btnH);
    if (isStandalone)
        bpmInput.setBounds(leftX + kPlayW + kGap, (h - 28) / 2, kBpmW, 28);

    // Right: mixer button, anchored to right edge
    int rightEdge = getWidth() - kGap;
    mixerBtn.setBounds(rightEdge - kMixerW, btnY, kMixerW, btnH);
    rightEdge -= kMixerW + kGap;

    // Gear button
    gearBtn.setBounds(rightEdge - kGearW, btnY, kGearW, btnH);
    rightEdge -= kGearW + kGap;

    // Save button
    saveBtn.setBounds(rightEdge - kSaveW, btnY, kSaveW, btnH);
    rightEdge -= kSaveW + kGap;

    // Preset dropdown
    presetDropdown.setBounds(rightEdge - kPresetW, btnY, kPresetW, btnH);
    rightEdge -= kPresetW + kGap;

    // +Rhythm button
    addRhythmBtn.setBounds(rightEdge - kAddW, btnY, kAddW, btnH);
    rightEdge -= kAddW + kGap;

    // Rhythm count label
    rhythmCountLabel.setBounds(rightEdge - kRhCountW, btnY, kRhCountW, btnH);
    rightEdge -= kRhCountW + kGap;

    // Master loop length
    loopDropdown.setBounds(rightEdge - kLoopW, btnY, kLoopW, btnH);
    rightEdge -= kLoopW + kGap;

    // Position label — to the right of the play/BPM group
    int posX = leftX + leftGroupW + kGap;
    posLabel.setBounds(posX, btnY, kPosW, btnH);
}
