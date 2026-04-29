#include "PluginEditor.h"

PluginEditor::PluginEditor(PluginProcessor& p)
    : AudioProcessorEditor(&p), processorRef(p),
      sidebar(p), rhythmPanel(p)
{
    setLookAndFeel(&lookAndFeel);

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

    setSize(780, 580);
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
    const int w       = getWidth();
    const int h       = getHeight();
    const int statusH = 20;

    sidebar.setBounds(0, 0, RhythmSidebar::kWidth, h - statusH);
    rhythmPanel.setBounds(RhythmSidebar::kWidth, 0,
                          w - RhythmSidebar::kWidth, h - statusH);
    statusBar.setBounds(0, h - statusH, w, statusH);
}
