#include "TransportBar.h"

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
        playBtn.setButtonText(proc.isInternalPlaying() ? "Stop" : "Play");
    else
        playBtn.setButtonText("DAW");
}

void TransportBar::paint(juce::Graphics& g)
{
    using Id = MuClidLookAndFeel::ColourIds;

    g.setColour(MuClidLookAndFeel::colour(Id::panelBackground));
    g.fillAll();

    g.setColour(MuClidLookAndFeel::colour(Id::headingText));
    g.setFont(juce::Font(14.0f));
    g.drawText("mu-Clid", 8, 0, 80, getHeight(), juce::Justification::centredLeft, false);

    if (isStandalone)
    {
        // "BPM" label to the left of the nudge input
        g.setColour(MuClidLookAndFeel::colour(Id::labelText));
        g.setFont(juce::Font(10.0f));
        const int bpmLabelX = 8 + 80 + 8 + 64 + 8;
        g.drawText("BPM", bpmLabelX, 0, 32, getHeight(), juce::Justification::centredLeft, false);
    }
    else
    {
        g.setColour(MuClidLookAndFeel::colour(Id::mutedText));
        g.setFont(juce::Font(11.0f));
        g.drawText("Syncing to DAW", 8 + 80 + 8 + 64 + 8, 0, 160, getHeight(),
                   juce::Justification::centredLeft, false);
    }

    g.setColour(MuClidLookAndFeel::colour(Id::segmentInactiveBorder));
    g.drawLine(0.0f, (float)(getHeight() - 1), (float)getWidth(), (float)(getHeight() - 1), 0.5f);
}

void TransportBar::resized()
{
    const int h   = getHeight();
    const int pad = 4;

    // Play/Stop button sits after the logo area
    const int logoW = 88;
    const int btnW  = 64;
    playBtn.setBounds(logoW + 8, (h - 24) / 2, btnW, 24);

    if (isStandalone)
    {
        // NudgeInput after the BPM label (32px label + 8px gap)
        const int bpmX = logoW + 8 + btnW + 8 + 32 + 4;
        bpmInput.setBounds(bpmX, pad, 120, h - pad * 2);
    }
}
