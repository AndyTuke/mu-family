#include "TransportBar.h"

static const juce::String kPlay  = juce::String(juce::CharPointer_UTF8("\xe2\x96\xb6"));
static const juce::String kStop  = juce::String(juce::CharPointer_UTF8("\xe2\x96\xa0"));

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

    mixerBtn.setClickingTogglesState(true);
    mixerBtn.onClick = [this] { if (onMixerToggle) onMixerToggle(); };
    addAndMakeVisible(mixerBtn);

    if (isStandalone)
    {
        bpmInput.setValue((int)proc.getInternalBpm());
        bpmInput.onChange = [this](int v) { proc.setInternalBpm((double)v); };
        addAndMakeVisible(bpmInput);
    }

    refreshPlayBtn();
    startTimerHz(10);
}

TransportBar::~TransportBar()
{
    stopTimer();
}

void TransportBar::timerCallback()
{
    refreshPlayBtn();
}

void TransportBar::refreshPlayBtn()
{
    if (isStandalone)
        playBtn.setButtonText(proc.isInternalPlaying() ? kStop : kPlay);
    else
        playBtn.setButtonText(kPlay);
}

void TransportBar::paint(juce::Graphics& g)
{
    using Id = MuClidLookAndFeel::ColourIds;

    g.setColour(MuClidLookAndFeel::colour(Id::panelBackground));
    g.fillAll();

    g.setColour(MuClidLookAndFeel::colour(Id::headingText));
    g.setFont(juce::Font(14.0f));
    g.drawText("mu-Clid", 8, 0, 80, getHeight(), juce::Justification::centredLeft, false);

    if (!isStandalone)
    {
        // Centred "Syncing to DAW" indicator for plugin mode
        g.setColour(MuClidLookAndFeel::colour(Id::mutedText));
        g.setFont(juce::Font(11.0f));
        g.drawText("Syncing to DAW", 0, 0, getWidth(), getHeight(),
                   juce::Justification::centred, false);
    }

    g.setColour(MuClidLookAndFeel::colour(Id::segmentInactiveBorder));
    g.drawLine(0.0f, (float)(getHeight() - 1), (float)getWidth(), (float)(getHeight() - 1), 0.5f);
}

void TransportBar::resized()
{
    const int h    = getHeight();
    const int btnW = 28;
    const int btnH = 28;
    const int bpmW = 120;
    const int gap  = 8;

    // Centre the play button (and BPM input if standalone) in the bar.
    // Mixer button is right-aligned.
    const int groupW  = isStandalone ? (btnW + gap + bpmW) : btnW;
    const int startX  = (getWidth() - groupW) / 2;
    const int btnY    = (h - btnH) / 2;

    playBtn.setBounds(startX, btnY, btnW, btnH);

    if (isStandalone)
        bpmInput.setBounds(startX + btnW + gap, (h - 28) / 2, bpmW, 28);

    const int mixerW = 52;
    mixerBtn.setBounds(getWidth() - mixerW - 6, btnY, mixerW, btnH);
}
