#include "PluginEditor.h"

//==============================================================================
PluginEditor::PluginEditor(PluginProcessor& p)
    : AudioProcessorEditor(&p), processorRef(p)
{
    setSize(780, 580);
}

PluginEditor::~PluginEditor() {}

void PluginEditor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff1c1c1b));
    g.setColour(juce::Colours::white);
    g.setFont(20.0f);
    g.drawText("mu-Clid", getLocalBounds(), juce::Justification::centred, true);
}

void PluginEditor::resized()
{
    // Layout goes here in later stages
}