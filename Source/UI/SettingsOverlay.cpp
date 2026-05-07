#include "SettingsOverlay.h"
#include "../PluginProcessor.h"

SettingsOverlay::SettingsOverlay(PluginProcessor& p)
    : proc(p),
      isStandalone(p.wrapperType == juce::AudioProcessor::wrapperType_Standalone)
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

    swapModeLabel.setText("Hot-swap timing:", juce::dontSendNotification);
    swapModeLabel.setFont(juce::Font(juce::FontOptions{}.withHeight(11.0f)));
    swapModeLabel.setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(swapModeLabel);

    swapModeDropdown.addItem("On master loop", 1);
    swapModeDropdown.addItem("On rhythm loop", 2);
    swapModeDropdown.setSelectedId((int)proc.getSwapMode() + 1, false);
    swapModeDropdown.onChange = [this](int id)
    {
        proc.setSwapMode(id == 2 ? PluginProcessor::SwapMode::OnRhythmLoop
                                 : PluginProcessor::SwapMode::OnMasterLoop);
    };
    addAndMakeVisible(swapModeDropdown);

    if (isStandalone)
    {
        clockSourceLabel.setText("Clock source:", juce::dontSendNotification);
        clockSourceLabel.setFont(juce::Font(juce::FontOptions{}.withHeight(11.0f)));
        clockSourceLabel.setJustificationType(juce::Justification::centredRight);
        addAndMakeVisible(clockSourceLabel);

        clockSourceDropdown.addItem("Internal", 1);
        clockSourceDropdown.addItem("MIDI In",  2);
        clockSourceDropdown.setSelectedId(proc.getMidiSyncEnabled() ? 2 : 1, false);
        clockSourceDropdown.onChange = [this](int id)
        {
            proc.setMidiSyncEnabled(id == 2);
            updateMidiSyncVisibility();
        };
        addAndMakeVisible(clockSourceDropdown);

        midiMessagesLabel.setText("MIDI messages:", juce::dontSendNotification);
        midiMessagesLabel.setFont(juce::Font(juce::FontOptions{}.withHeight(11.0f)));
        midiMessagesLabel.setJustificationType(juce::Justification::centredRight);
        addAndMakeVisible(midiMessagesLabel);

        midiMessagesDropdown.addItem("Clock only",  1);
        midiMessagesDropdown.addItem("Transport",   2);
        midiMessagesDropdown.addItem("Both",        3);
        midiMessagesDropdown.setSelectedId(proc.getMidiSyncMessages() + 1, false);
        midiMessagesDropdown.onChange = [this](int id)
        {
            proc.setMidiSyncMessages(id - 1);
        };
        addAndMakeVisible(midiMessagesDropdown);

        updateMidiSyncVisibility();
    }

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
        proc.setContentDir(juce::File());
        updateFolderLabel();
        if (onContentDirChanged) onContentDirChanged();
    };
    addAndMakeVisible(resetContentFolderBtn);

    midiPresetsBtn.onClick = [this] { if (onMidiPresetsClicked) onMidiPresetsClicked(); };
    addAndMakeVisible(midiPresetsBtn);

    multiBusToggle.setToggleState(proc.getMultiBusEnabled(), juce::dontSendNotification);
    multiBusToggle.onClick = [this]
    {
        proc.setMultiBusEnabled(multiBusToggle.getToggleState());
    };
    addAndMakeVisible(multiBusToggle);
}

void SettingsOverlay::updateMidiSyncVisibility()
{
    const bool on = proc.getMidiSyncEnabled();
    midiMessagesLabel  .setVisible(on);
    midiMessagesDropdown.setVisible(on);
}

void SettingsOverlay::updateFolderLabel()
{
    contentFolderLabel.setText(proc.getContentDir().getFullPathName(),
                               juce::dontSendNotification);
}

void SettingsOverlay::resized()
{
    const int w       = getWidth();
    const int labelW  = 110;
    const int dropW   = 140;
    const int rowH    = 24;

    closeBtn.setBounds(w - kPad * 3 - 60, kPad, 60, 28);

    int y = kHeaderH + kPad;

    masterVolKnob.setBounds(kPad, y, 120, kRowH);
    y += kRowH + kPad * 2;

    // Hot-swap timing row
    swapModeLabel   .setBounds(kPad, y + 2, labelW, 20);
    swapModeDropdown.setBounds(kPad + labelW + 8, y, dropW, rowH);
    y += rowH + kPad;

    if (isStandalone)
    {
        y += kPad;  // extra gap before MIDI Clock section

        clockSourceLabel   .setBounds(kPad, y + 2, labelW, 20);
        clockSourceDropdown.setBounds(kPad + labelW + 8, y, dropW, rowH);
        y += rowH + kPad;

        midiMessagesLabel   .setBounds(kPad, y + 2, labelW, 20);
        midiMessagesDropdown.setBounds(kPad + labelW + 8, y, dropW, rowH);
        y += rowH + kPad;
    }

    // MIDI program-change presets button
    y += kPad;
    midiPresetsBtn.setBounds(kPad, y, 140, rowH);
    y += rowH + kPad;

    // Multi-bus output toggle (next row)
    multiBusToggle.setBounds(kPad, y, 220, rowH);
    y += rowH + kPad + 12;  // extra room for the rescan-required note drawn in paint()

    y += kPad;  // extra gap before Content Folder section

    // Content folder row
    const int btnW   = 70;
    const int fldW   = w - kPad * 2 - btnW * 2 - kPad * 2;
    contentFolderLabel    .setBounds(kPad, y, fldW, 20);
    browseContentFolderBtn.setBounds(kPad + fldW + kPad, y - 2, btnW, rowH);
    resetContentFolderBtn .setBounds(kPad + fldW + kPad + btnW + kPad, y - 2, btnW, rowH);
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

    g.setColour(MuClidLookAndFeel::colour(Id::labelText));
    g.setFont(juce::Font(juce::FontOptions{}.withHeight(10.0f)));

    // Section heading y values must mirror resized() layout.
    int y = kHeaderH + kPad + kRowH + kPad * 2;
    g.drawText("Hot-swap", kPad, y - 14, 200, 12, juce::Justification::centredLeft, false);
    y += 24 + kPad;

    if (isStandalone)
    {
        y += kPad;
        g.drawText("MIDI Clock", kPad, y - 14, 200, 12, juce::Justification::centredLeft, false);
        y += 24 + kPad;   // clock source row
        y += 24 + kPad;   // messages row (may be hidden, still reserve space)
    }

    // MIDI program-change presets section
    y += kPad;
    g.drawText("MIDI Program Change", kPad, y - 14, 200, 12, juce::Justification::centredLeft, false);
    y += 24 + kPad;

    // Multi-bus output toggle row + small "rescan required" note.
    g.drawText("(host rescan required after toggling)",
               kPad + 230, y - 24 + 4, getWidth() - kPad - 230, 14,
               juce::Justification::centredLeft, false);
    y += 24 + kPad + 12;

    y += kPad;
    g.drawText("Content Folder", kPad, y - 14, 200, 12, juce::Justification::centredLeft, false);
}
