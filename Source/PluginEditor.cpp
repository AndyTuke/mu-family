#include "PluginEditor.h"

PluginEditor::PluginEditor(PluginProcessor& p)
    : AudioProcessorEditor(&p), processorRef(p),
      transportBar(p), sidebar(p), rhythmPanel(p)
{
    setLookAndFeel(&lookAndFeel);

    addAndMakeVisible(transportBar);
    addAndMakeVisible(sidebar);
    addAndMakeVisible(rhythmPanel);
    addAndMakeVisible(statusBar);

    sidebar.onRhythmSelected = [this](int idx)
    {
        rhythmPanel.setRhythm(idx);
    };

    sidebar.onAddRhythm = [this]
    {
        if (processorRef.getNumRhythms() >= SequencerEngine::MaxRhythms) return;
        Rhythm r;
        r.name        = "Rhythm " + std::to_string(processorRef.getNumRhythms() + 1);
        r.colourIndex = processorRef.getNumRhythms() % 30;
        processorRef.addRhythm(r);
        sidebar.refreshItems();
        const int newIdx = processorRef.getNumRhythms() - 1;
        sidebar.setSelectedIndex(newIdx);
        rhythmPanel.setRhythm(newIdx);
    };

    rhythmPanel.onStatusUpdate = [this](const juce::String& name,
                                        const juce::String& val,
                                        juce::Colour col)
    {
        statusBar.showParam(name, val, col);
    };

    // Ensure at least one rhythm exists on startup
    if (processorRef.getNumRhythms() == 0)
    {
        Rhythm r;
        r.name        = "Rhythm 1";
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
    const int contentH   = h - transportH - statusH;

    transportBar.setBounds(0, 0, w, transportH);
    sidebar.setBounds(0, transportH, RhythmSidebar::kWidth, contentH);
    rhythmPanel.setBounds(RhythmSidebar::kWidth, transportH,
                          w - RhythmSidebar::kWidth, contentH);
    statusBar.setBounds(0, h - statusH, w, statusH);
}
