#include "PluginEditor.h"
#include "BuildNumber.h"

//==============================================================================
PluginEditor::PluginEditor(PluginProcessor& p)
    : AudioProcessorEditor(&p), processorRef(p)
{
    playButton.onClick = [this]
    {
        processorRef.toggleInternalPlay();
        playButton.setButtonText(processorRef.isInternalPlaying() ? "Stop" : "Play");
    };
    addAndMakeVisible(playButton);

    loadButton.onClick = [this]
    {
        fileChooser = std::make_unique<juce::FileChooser>(
            "Load Sample",
            juce::File::getSpecialLocation(juce::File::userMusicDirectory),
            "*.wav;*.aiff;*.aif;*.mp3;*.flac");

        fileChooser->launchAsync(
            juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
            [this](const juce::FileChooser& fc)
            {
                auto result = fc.getResult();
                if (result.existsAsFile())
                    processorRef.loadSampleForRhythm(0, result);
            });
    };
    addAndMakeVisible(loadButton);

    setSize(780, 580);
}

PluginEditor::~PluginEditor() {}

void PluginEditor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff1c1c1b));
    g.setColour(juce::Colours::white);
    g.setFont(20.0f);
    g.drawText("mu-Clid", getLocalBounds(), juce::Justification::centred, true);

    g.setFont(13.0f);
    g.setColour(juce::Colour(0xff888780));
    g.drawText("Beta " + juce::String(BUILD_NUMBER),
               juce::Rectangle<int>(0, getHeight() / 2 + 18, getWidth(), 20),
               juce::Justification::centred, true);
}

void PluginEditor::resized()
{
    playButton.setBounds(getWidth() / 2 - 110, getHeight() / 2 - 20, 100, 40);
    loadButton.setBounds(getWidth() / 2 +  10, getHeight() / 2 - 20, 100, 40);
}