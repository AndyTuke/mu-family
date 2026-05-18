#include "SettingsOverlay.h"
#include "../PluginProcessor.h"

SettingsOverlay::SettingsOverlay(PluginProcessor& p)
    : proc(p),
      isStandalone(p.wrapperType == juce::AudioProcessor::wrapperType_Standalone)
{
    closeBtn.onClick = [this] { if (onClose) onClose(); };
    addAndMakeVisible(closeBtn);

    masterVolKnob.setRange(0.0, 1.0, 0.001);
    // #400: skew + dB readout to match the MixerChannel master fader's behaviour.
    // The underlying APVTS scale is the same linear 0..1 in both places (see
    // PluginProcessor_APVTS.cpp:240). MixerChannel's tall vertical fader reads OK
    // linearly because the strip is long and labels show dB live (updateDbLabel,
    // [Source/UI/MixerChannel.cpp:585](Source/UI/MixerChannel.cpp#L585)). A small
    // rotary knob with raw 0..1 display compresses the useful -6..0 dB region
    // into the top quarter of rotation, which feels unusable. Mid-point skew at
    // 0.5 (= -6 dB) gives the same per-degree dB resolution as a fader, and the
    // textFromValueFunction puts dB in the readout for parity with the mixer.
    masterVolKnob.getSlider().setSkewFactorFromMidPoint(0.5);
    masterVolKnob.getSlider().textFromValueFunction = [](double v) -> juce::String {
        if (v <= 0.0) return "-inf dB";
        return juce::String(20.0 * std::log10(v), 1) + " dB";
    };
    if (auto* raw = proc.apvts.getRawParameterValue("mstr_lvl"))
        masterVolKnob.setValue(*raw, juce::dontSendNotification);
    masterVolKnob.onValueChanged = [this](double v) {
        // #390: inner `param` (was `p`) shadowed the outer captured-by-reference
        // `proc` member name `p` from the ctor's param list — confusing on read.
        if (auto* param = proc.apvts.getParameter("mstr_lvl"))
            param->setValueNotifyingHost(param->convertTo0to1((float)v));
    };
    addAndMakeVisible(masterVolKnob);

    // #397: identical 5-line label setup was repeated four times — extract a helper.
    auto makeFieldLabel = [this](juce::Label& lbl, const juce::String& text,
                                  juce::Colour textColour,
                                  juce::Justification just)
    {
        lbl.setText(text, juce::dontSendNotification);
        lbl.setFont(juce::Font(juce::FontOptions{}.withHeight(12.0f)));
        lbl.setColour(juce::Label::textColourId, textColour);
        lbl.setJustificationType(just);
        addAndMakeVisible(lbl);
    };

    const juce::Colour labelCol   = MuClidLookAndFeel::colour(MuClidLookAndFeel::ColourIds::labelText);
    const juce::Colour headingCol = MuClidLookAndFeel::colour(MuClidLookAndFeel::ColourIds::headingText);

    makeFieldLabel(swapModeLabel, "Timing", labelCol, juce::Justification::centredRight);

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
        makeFieldLabel(clockSourceLabel, "Source", labelCol, juce::Justification::centredRight);

        clockSourceDropdown.addItem("Internal", 1);
        clockSourceDropdown.addItem("MIDI In",  2);
        clockSourceDropdown.setSelectedId(proc.getMidiSyncEnabled() ? 2 : 1, false);
        clockSourceDropdown.onChange = [this](int id)
        {
            proc.setMidiSyncEnabled(id == 2);
            updateMidiSyncVisibility();
        };
        addAndMakeVisible(clockSourceDropdown);

        makeFieldLabel(midiMessagesLabel, "Messages", labelCol, juce::Justification::centredRight);

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

    // Content folder shows the path itself (set by updateFolderLabel) — heading colour
    // because it's primary content not a secondary "Source:" / "Timing:" annotation.
    makeFieldLabel(contentFolderLabel, juce::String(), headingCol, juce::Justification::centredLeft);
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

    // #418: Primary sample library — mirrors the content folder row pattern.
    makeFieldLabel(sampleLibLabel, juce::String(), headingCol, juce::Justification::centredLeft);
    updateSampleLibLabel();

    browseSampleLibBtn.onClick = [this]
    {
        fileChooser = std::make_unique<juce::FileChooser>(
            "Choose primary sample library...", proc.getPrimarySampleDir());
        fileChooser->launchAsync(
            juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
            [this](const juce::FileChooser& fc)
            {
                auto result = fc.getResult();
                if (result.isDirectory())
                {
                    proc.setPrimarySampleDir(result);
                    updateSampleLibLabel();
                }
            });
    };
    addAndMakeVisible(browseSampleLibBtn);

    resetSampleLibBtn.onClick = [this]
    {
        proc.setPrimarySampleDir(juce::File());   // clears override → OS Music dir
        updateSampleLibLabel();
    };
    addAndMakeVisible(resetSampleLibBtn);

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

void SettingsOverlay::updateSampleLibLabel()   // #418
{
    sampleLibLabel.setText(proc.getPrimarySampleDir().getFullPathName(),
                           juce::dontSendNotification);
}

void SettingsOverlay::computeLayout()
{
    const int w = getWidth();
    layout.contentW = juce::jmin(kContentMaxW, w - kPad * 2);
    layout.contentX = (w - layout.contentW) / 2;

    int y = kHeaderH + kPad;

    // Section: Audio
    layout.audioHeader  = y;
    y += kSectionHeadH;
    layout.masterVolY   = y;
    y += 64;                              // tall enough for the rotary knob + label
    y += kSectionGap;

    // Section: Hot-swap
    layout.swapHeader   = y;
    y += kSectionHeadH;
    layout.swapRowY     = y;
    y += kRowH;
    y += kSectionGap;

    // Section: MIDI Clock (standalone only — collapsed entirely in DAW)
    if (isStandalone)
    {
        layout.midiClockHeader   = y;
        y += kSectionHeadH;
        layout.clockSourceRowY   = y;
        y += kRowH + kRowGap;
        layout.midiMessagesRowY  = y;
        y += kRowH;
        y += kSectionGap;
    }

    // Section: MIDI Program Change
    layout.midiPCHeader     = y;
    y += kSectionHeadH;
    layout.midiPresetsRowY  = y;
    y += kRowH;
    y += kSectionGap;

    // Section: Output
    layout.outputHeader     = y;
    y += kSectionHeadH;
    layout.multiBusRowY     = y;
    y += kRowH;
    y += kSectionGap;

    // Section: Sample Library (#418) — user's personal sample folder, used as
    // the default location for the sample-load dialog.
    layout.sampleLibHeader     = y;
    y += kSectionHeadH;
    layout.sampleLibPathRowY   = y;
    y += kRowH + kRowGap;
    layout.sampleLibBtnsRowY   = y;
    y += kRowH;
    y += kSectionGap;

    // Section: Content Folder
    layout.contentHeader        = y;
    y += kSectionHeadH;
    layout.contentPathRowY      = y;
    y += kRowH + kRowGap;
    layout.contentBtnsRowY      = y;
}

void SettingsOverlay::resized()
{
    computeLayout();

    const int w = getWidth();
    closeBtn.setBounds(w - kPad - kCloseBtnW, (kHeaderH - kCloseBtnH) / 2, kCloseBtnW, kCloseBtnH);

    const int x      = layout.contentX;
    const int cw     = layout.contentW;
    const int labelX = x;
    const int ctrlX  = x + kLabelW + kLabelCtrlGap;

    // #399: Audio — Master Vol knob centred horizontally within the column. Previously
    // anchored at column-left under a centred section header divider, which looked
    // off-balance.
    masterVolKnob.setBounds(x + (cw - kMasterVolW) / 2, layout.masterVolY,
                            kMasterVolW, kMasterVolH);

    swapModeLabel   .setBounds(labelX, layout.swapRowY, kLabelW, kRowH);
    swapModeDropdown.setBounds(ctrlX,  layout.swapRowY, kControlW, kRowH);

    if (isStandalone)
    {
        clockSourceLabel    .setBounds(labelX, layout.clockSourceRowY, kLabelW, kRowH);
        clockSourceDropdown .setBounds(ctrlX,  layout.clockSourceRowY, kControlW, kRowH);

        midiMessagesLabel   .setBounds(labelX, layout.midiMessagesRowY, kLabelW, kRowH);
        midiMessagesDropdown.setBounds(ctrlX,  layout.midiMessagesRowY, kControlW, kRowH);
    }

    midiPresetsBtn.setBounds(ctrlX, layout.midiPresetsRowY, kControlW, kRowH);

    multiBusToggle.setBounds(ctrlX, layout.multiBusRowY, kControlW, kRowH);

    // #418: sample library row — same layout as Content Folder below.
    sampleLibLabel.setBounds(x, layout.sampleLibPathRowY, cw, kRowH);
    resetSampleLibBtn .setBounds(x + cw - kFolderBtnW,
                                 layout.sampleLibBtnsRowY, kFolderBtnW, kRowH);
    browseSampleLibBtn.setBounds(x + cw - kFolderBtnW * 2 - kFolderBtnGap,
                                 layout.sampleLibBtnsRowY, kFolderBtnW, kRowH);

    contentFolderLabel.setBounds(x, layout.contentPathRowY, cw, kRowH);
    resetContentFolderBtn .setBounds(x + cw - kFolderBtnW,
                                     layout.contentBtnsRowY, kFolderBtnW, kRowH);
    browseContentFolderBtn.setBounds(x + cw - kFolderBtnW * 2 - kFolderBtnGap,
                                     layout.contentBtnsRowY, kFolderBtnW, kRowH);
}

void SettingsOverlay::paint(juce::Graphics& g)
{
    using Id = MuClidLookAndFeel::ColourIds;

    g.setColour(MuClidLookAndFeel::colour(Id::panelBackground));
    g.fillAll();

    // Top "Settings" bar
    g.setColour(MuClidLookAndFeel::colour(Id::headingText));
    g.setFont(juce::Font(juce::FontOptions{}.withHeight(15.0f)));
    g.drawText("Settings", kPad, 0, 200, kHeaderH, juce::Justification::centredLeft, false);

    g.setColour(MuClidLookAndFeel::colour(Id::segmentInactiveBorder));
    g.drawLine(0.0f, (float)kHeaderH, (float)getWidth(), (float)kHeaderH, 1.0f);

    const int x = layout.contentX;
    const int w = layout.contentW;

    auto drawSectionHeader = [&](int headerY, const juce::String& title)
    {
        g.setColour(MuClidLookAndFeel::colour(Id::headingText));
        g.setFont(juce::Font(juce::FontOptions{}.withHeight(13.0f)));
        g.drawText(title, x, headerY, w, 18, juce::Justification::centredLeft, false);

        g.setColour(MuClidLookAndFeel::colour(Id::segmentInactiveBorder));
        const float lineY = (float)(headerY + 20);
        g.drawLine((float)x, lineY, (float)(x + w), lineY, 0.5f);
    };

    auto drawHint = [&](int yCentre, const juce::String& text, int hintX, int hintW)
    {
        g.setColour(MuClidLookAndFeel::colour(Id::labelText));
        g.setFont(juce::Font(juce::FontOptions{}.withHeight(11.0f)));
        g.drawText(text, hintX, yCentre - 7, hintW, 14,
                   juce::Justification::centredLeft, false);
    };

    drawSectionHeader(layout.audioHeader,    "Audio");
    drawSectionHeader(layout.swapHeader,     "Hot-swap");
    if (isStandalone)
        drawSectionHeader(layout.midiClockHeader, "MIDI Clock");
    drawSectionHeader(layout.midiPCHeader,   "MIDI Program Change");
    drawSectionHeader(layout.outputHeader,   "Output");
    drawSectionHeader(layout.sampleLibHeader, "Sample Library");   // #418
    drawSectionHeader(layout.contentHeader,  "Content Folder");

    // Rescan-required hint next to the multi-bus toggle.
    const int hintX = layout.contentX + kLabelW + kLabelCtrlGap + kControlW + kLabelCtrlGap;
    const int hintW = layout.contentX + layout.contentW - hintX;
    drawHint(layout.multiBusRowY + kRowH / 2, "(host rescan required after toggling)",
             hintX, hintW);
}
