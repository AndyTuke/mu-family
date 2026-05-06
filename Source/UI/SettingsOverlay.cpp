#include "SettingsOverlay.h"
#include "../PluginProcessor.h"

SettingsOverlay::SettingsOverlay(PluginProcessor& p)
    : proc(p)
{
    closeBtn.onClick = [this] { if (onClose) onClose(); };
    addAndMakeVisible(closeBtn);

    masterVolKnob.setRange(0.0, 1.0, 0.001);
    if (auto* raw = proc.apvts.getRawParameterValue("mstr_lvl"))
        masterVolKnob.setValue(*raw, juce::dontSendNotification);
    masterVolKnob.onValueChanged = [this](double v) {
        if (auto* p = proc.apvts.getParameter("mstr_lvl"))
            p->setValueNotifyingHost(p->convertTo0to1((float)v));
    };
    addAndMakeVisible(masterVolKnob);

    contentFolderLabel.setFont(juce::Font(juce::FontOptions{}.withHeight(11.0f)));
    contentFolderLabel.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(contentFolderLabel);
    updateFolderLabel();

    browseContentFolderBtn.onClick = [this]
    {
        fileChooser = std::make_unique<juce::FileChooser>(
            "Choose content folder...", proc.getContentDir());
        fileChooser->launchAsync(
            juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
            [this](const juce::FileChooser& fc)
            {
                auto result = fc.getResult();
                if (result.isDirectory())
                {
                    proc.setContentDir(result);
                    updateFolderLabel();
                    if (onContentDirChanged) onContentDirChanged();
                }
            });
    };
    addAndMakeVisible(browseContentFolderBtn);

    resetContentFolderBtn.onClick = [this]
    {
        proc.setContentDir(juce::File());  // empty = revert to default
        updateFolderLabel();
        if (onContentDirChanged) onContentDirChanged();
    };
    addAndMakeVisible(resetContentFolderBtn);
}

void SettingsOverlay::updateFolderLabel()
{
    contentFolderLabel.setText(proc.getContentDir().getFullPathName(),
                               juce::dontSendNotification);
}

void SettingsOverlay::resized()
{
    const int w = getWidth();

    closeBtn.setBounds(w - kPad - 60, kPad, 60, 28);

    int y = kHeaderH + kPad;
    const int ctrlW = 120;

    masterVolKnob.setBounds(kPad, y, ctrlW, kRowH);
    y += kRowH + kPad * 2;

    // Content folder row
    const int btnW  = 70;
    const int labelW = w - kPad * 2 - btnW * 2 - kPad * 2;
    contentFolderLabel      .setBounds(kPad, y, labelW, 20);
    browseContentFolderBtn  .setBounds(kPad + labelW + kPad, y - 2, btnW, 24);
    resetContentFolderBtn   .setBounds(kPad + labelW + kPad + btnW + kPad, y - 2, btnW, 24);
}

void SettingsOverlay::paint(juce::Graphics& g)
{
    using Id = MuClidLookAndFeel::ColourIds;

    g.setColour(MuClidLookAndFeel::colour(Id::panelBackground));
    g.fillAll();

    g.setColour(MuClidLookAndFeel::colour(Id::headingText));
    g.setFont(juce::Font(juce::FontOptions{}.withHeight(14.0f)));
    g.drawText("Settings", kPad, 0, 200, kHeaderH, juce::Justification::centredLeft, false);

    g.setColour(MuClidLookAndFeel::colour(Id::segmentInactiveBorder));
    g.drawLine(0.0f, (float)kHeaderH, (float)getWidth(), (float)kHeaderH, 0.5f);

    // Content folder section heading
    const int folderY = kHeaderH + kPad + kRowH + kPad * 2;
    g.setColour(MuClidLookAndFeel::colour(Id::labelText));
    g.setFont(juce::Font(juce::FontOptions{}.withHeight(10.0f)));
    g.drawText("Content Folder", kPad, folderY - 14, 200, 12,
               juce::Justification::centredLeft, false);
}
