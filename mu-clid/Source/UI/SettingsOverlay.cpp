#include "SettingsOverlay.h"
#include "Plugin/PluginProcessor.h"

SettingsOverlay::SettingsOverlay(PluginProcessor& p)
    : proc(p),
      isStandalone(p.wrapperType == juce::AudioProcessor::wrapperType_Standalone)
{
    // Header bar + Close button are owned by mu_ui::SettingsOverlayBase.
    masterVolKnob.setRange(0.0, 1.0, 0.001);
    // skew + dB readout to match the MixerChannel master fader's behaviour.
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
        // inner `param` (was `p`) shadowed the outer captured-by-reference
        // `proc` member name `p` from the ctor's param list — confusing on read.
        if (auto* param = proc.apvts.getParameter("mstr_lvl"))
            param->setValueNotifyingHost(param->convertTo0to1((float)v));
    };
    addAndMakeVisible(masterVolKnob);

    // identical 5-line label setup was repeated four times — extract a helper.
    auto makeFieldLabel = [this](juce::Label& lbl, const juce::String& text,
                                  juce::Colour textColour,
                                  juce::Justification just)
    {
        lbl.setText(text, juce::dontSendNotification);
        lbl.setFont(juce::Font(juce::FontOptions{}.withHeight(mu_ui::sf(12.0f))));
        lbl.setColour(juce::Label::textColourId, textColour);
        lbl.setJustificationType(just);
        addAndMakeVisible(lbl);
    };

    const juce::Colour labelCol   = MuLookAndFeel::colour(MuLookAndFeel::ColourIds::labelText);
    const juce::Colour headingCol = MuLookAndFeel::colour(MuLookAndFeel::ColourIds::headingText);

    // ── Display — UI size picker (Medium = 1.0, Large = 1.25 = 1463×1088 window)
    makeFieldLabel(uiSizeLabel, "Size", labelCol, juce::Justification::centredLeft);
    {
        const float current = proc.getUiScale();
        // Medium=index 0, Large=index 1. Stored value > midpoint snaps to Large.
        const int idx = (current >= (PluginProcessor::kUiScaleMedium
                                     + PluginProcessor::kUiScaleLarge) * 0.5f) ? 1 : 0;
        uiSizeCtrl.setSelectedIndex(idx, /*notify*/ false);
    }
    uiSizeCtrl.onChange = [this](int idx)
    {
        proc.setUiScale(idx == 1 ? PluginProcessor::kUiScaleLarge
                                 : PluginProcessor::kUiScaleMedium);
    };
    addAndMakeVisible(uiSizeCtrl);

    makeFieldLabel(swapModeLabel, "Timing", labelCol, juce::Justification::centredLeft);

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
        makeFieldLabel(clockSourceLabel, "Source", labelCol, juce::Justification::centredLeft);

        clockSourceDropdown.addItem("Internal", 1);
        clockSourceDropdown.addItem("MIDI In",  2);
        clockSourceDropdown.setSelectedId(proc.getMidiSyncEnabled() ? 2 : 1, false);
        clockSourceDropdown.onChange = [this](int id)
        {
            proc.setMidiSyncEnabled(id == 2);
            updateMidiSyncVisibility();
        };
        addAndMakeVisible(clockSourceDropdown);

        makeFieldLabel(midiMessagesLabel, "Messages", labelCol, juce::Justification::centredLeft);

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

    if (!isStandalone)
    {
        makeFieldLabel(midiModeLabel, "Mode", labelCol, juce::Justification::centredLeft);

        midiModeDropdown.addItem("Free", 1);
        midiModeDropdown.addItem("Note", 2);
        midiModeDropdown.setSelectedId(proc.getMidiNoteMode() + 1, false);
        midiModeDropdown.onChange = [this](int id)
        {
            proc.setMidiNoteMode(id - 1);
        };
        addAndMakeVisible(midiModeDropdown);
    }

    // Content folder shows the path itself (set by updateFolderLabel) — heading colour
    // because it's primary content not a secondary "Source:" / "Timing:" annotation.
    makeFieldLabel(contentFolderLabel, juce::String(), headingCol, juce::Justification::centredLeft);
    updateFolderLabel();

    browseContentFolderBtn.onClick = [this]
    {
        juce::Component::SafePointer<SettingsOverlay> safe(this);
        fileChooser = std::make_unique<juce::FileChooser>(
            "Choose content folder...", proc.getContentDir());
        fileChooser->launchAsync(
            juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
            [safe](const juce::FileChooser& fc)
            {
                if (!safe) return;
                auto result = fc.getResult();
                if (result.isDirectory())
                {
                    safe->proc.setContentDir(result);
                    safe->updateFolderLabel();
                    if (safe->onContentDirChanged) safe->onContentDirChanged();
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

    // Primary sample library — mirrors the content folder row pattern.
    makeFieldLabel(sampleLibLabel, juce::String(), headingCol, juce::Justification::centredLeft);
    updateSampleLibLabel();

    browseSampleLibBtn.onClick = [this]
    {
        juce::Component::SafePointer<SettingsOverlay> safe(this);
        fileChooser = std::make_unique<juce::FileChooser>(
            "Choose primary sample library...", proc.getPrimarySampleDir());
        fileChooser->launchAsync(
            juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
            [safe](const juce::FileChooser& fc)
            {
                if (!safe) return;
                auto result = fc.getResult();
                if (result.isDirectory())
                {
                    safe->proc.setPrimarySampleDir(result);
                    safe->updateSampleLibLabel();
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

    fullPresetsBtn.onClick = [this] { if (onFullPresetsClicked) onFullPresetsClicked(); };
    addAndMakeVisible(fullPresetsBtn);

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

void SettingsOverlay::updateSampleLibLabel()
{
    sampleLibLabel.setText(proc.getPrimarySampleDir().getFullPathName(),
                           juce::dontSendNotification);
}

void SettingsOverlay::computeLayout()
{
    using mu_ui::s;
    // Content column geometry is owned by the base (centred + width-capped).
    layout.contentX = contentX();
    layout.contentW = contentW();

    const int headH    = s(kSectionHeadH);
    const int groupH   = s(kGroupHeadH);
    const int rowH     = s(kRowH);
    const int rowGap   = s(kRowGap);
    const int sectGap  = s(kSectionGap);
    const int groupGap = s(kGroupGap);

    int y = s(kHeaderH) + s(kPad);

    // ── General sub-panel (Audio, Display, Output)
    layout.generalGroupHeader = y;
    y += groupH;

    layout.audioHeader  = y;
    y += headH;
    layout.masterVolY   = y;
    y += s(64);                           // tall enough for the rotary knob + label
    y += sectGap;

    layout.displayHeader = y;
    y += headH;
    layout.uiSizeRowY    = y;
    y += rowH + rowGap;     // extra row gap so the hint label sits below the picker
    y += rowH;
    y += sectGap;

    layout.outputHeader = y;
    y += headH;
    layout.multiBusRowY = y;
    y += rowH;

    y += groupGap;

    // ── MIDI sub-panel (Hot-swap, MIDI Clock, MIDI Program Change)
    layout.midiGroupHeader = y;
    y += groupH;

    layout.swapHeader = y;
    y += headH;
    layout.swapRowY   = y;
    y += rowH;
    y += sectGap;

    // MIDI Clock standalone-only (collapsed entirely in DAW)
    if (isStandalone)
    {
        layout.midiClockHeader  = y;
        y += headH;
        layout.clockSourceRowY  = y;
        y += rowH + rowGap;
        layout.midiMessagesRowY = y;
        y += rowH;
        y += sectGap;
    }

    // MIDI Note Mode: plugin-only (collapsed entirely in standalone)
    if (!isStandalone)
    {
        layout.midiModeHeader = y;
        y += headH;
        layout.midiModeRowY   = y;
        y += rowH;
        y += sectGap;
    }

    layout.midiPCHeader = y;
    y += headH;
    layout.midiPCRowY   = y;
    y += rowH;

    y += groupGap;

    // ── Locations sub-panel (Sample Library, Content Folder)
    layout.locationsGroupHeader = y;
    y += groupH;

    layout.sampleLibHeader     = y;
    y += headH;
    layout.sampleLibPathRowY   = y;
    y += rowH + rowGap;
    layout.sampleLibBtnsRowY   = y;
    y += rowH;
    y += sectGap;

    layout.contentHeader        = y;
    y += headH;
    layout.contentPathRowY      = y;
    y += rowH + rowGap;
    layout.contentBtnsRowY      = y;
}

void SettingsOverlay::layoutContent()
{
    using mu_ui::s;
    computeLayout();   // Close button + header bar are positioned by the base.

    const int x       = layout.contentX;
    const int cw      = layout.contentW;
    const int labelX  = rowLabelX();      // indented from the section headings
    const int labelW  = s(kLabelW);
    const int ctrlX   = rowControlX();    // left-aligned control column
    const int ctrlW   = s(kControlW);
    const int rowH    = s(kRowH);
    const int mvW     = s(kMasterVolW);
    const int mvH     = s(kMasterVolH);
    const int fbtnW   = s(kFolderBtnW);
    const int fbtnGap = s(kFolderBtnGap);

    // Audio — Master Vol knob left-aligned at the row indent.
    masterVolKnob.setBounds(labelX, layout.masterVolY, mvW, mvH);

    // Display — Size picker (Medium / Large). Hint label drawn in paint() below.
    uiSizeLabel.setBounds(labelX, layout.uiSizeRowY, labelW, rowH);
    uiSizeCtrl .setBounds(ctrlX,  layout.uiSizeRowY, ctrlW,  rowH);

    swapModeLabel   .setBounds(labelX, layout.swapRowY, labelW, rowH);
    swapModeDropdown.setBounds(ctrlX,  layout.swapRowY, ctrlW,  rowH);

    if (isStandalone)
    {
        clockSourceLabel    .setBounds(labelX, layout.clockSourceRowY, labelW, rowH);
        clockSourceDropdown .setBounds(ctrlX,  layout.clockSourceRowY, ctrlW,  rowH);

        midiMessagesLabel   .setBounds(labelX, layout.midiMessagesRowY, labelW, rowH);
        midiMessagesDropdown.setBounds(ctrlX,  layout.midiMessagesRowY, ctrlW,  rowH);
    }

    if (!isStandalone)
    {
        midiModeLabel   .setBounds(labelX, layout.midiModeRowY, labelW, rowH);
        midiModeDropdown.setBounds(ctrlX,  layout.midiModeRowY, ctrlW,  rowH);
    }

    // MIDI Program Change — two buttons side-by-side, left-aligned at the row indent.
    {
        const int btnGap = s(kFolderBtnGap);
        const int btnW   = s(180);
        midiPresetsBtn.setBounds(labelX,                  layout.midiPCRowY, btnW, rowH);
        fullPresetsBtn.setBounds(labelX + btnW + btnGap,  layout.midiPCRowY, btnW, rowH);
    }

    multiBusToggle.setBounds(ctrlX, layout.multiBusRowY, ctrlW, rowH);

    // sample library row — same layout as Content Folder below.
    sampleLibLabel.setBounds(x, layout.sampleLibPathRowY, cw, rowH);
    resetSampleLibBtn .setBounds(x + cw - fbtnW,
                                 layout.sampleLibBtnsRowY, fbtnW, rowH);
    browseSampleLibBtn.setBounds(x + cw - fbtnW * 2 - fbtnGap,
                                 layout.sampleLibBtnsRowY, fbtnW, rowH);

    contentFolderLabel.setBounds(x, layout.contentPathRowY, cw, rowH);
    resetContentFolderBtn .setBounds(x + cw - fbtnW,
                                     layout.contentBtnsRowY, fbtnW, rowH);
    browseContentFolderBtn.setBounds(x + cw - fbtnW * 2 - fbtnGap,
                                     layout.contentBtnsRowY, fbtnW, rowH);
}

void SettingsOverlay::paintContent(juce::Graphics& g)
{
    using mu_ui::s;

    // Background + "Settings" header bar + divider are painted by the base.
    drawGroupHeader(g, layout.generalGroupHeader,   "General");
    drawSectionHeader(g, layout.audioHeader,        "Audio");
    drawSectionHeader(g, layout.displayHeader,      "Display");
    drawSectionHeader(g, layout.outputHeader,       "Output");

    drawGroupHeader(g, layout.midiGroupHeader,      "MIDI");
    drawSectionHeader(g, layout.swapHeader,         "Hot-swap");
    if (isStandalone)
        drawSectionHeader(g, layout.midiClockHeader, "MIDI Clock");
    if (!isStandalone)
        drawSectionHeader(g, layout.midiModeHeader,  "MIDI Mode");
    drawSectionHeader(g, layout.midiPCHeader,       "MIDI Program Change");

    drawGroupHeader(g, layout.locationsGroupHeader, "Locations");
    drawSectionHeader(g, layout.sampleLibHeader,    "Sample Library");
    drawSectionHeader(g, layout.contentHeader,      "Content Folder");

    // Rescan-required hint next to the multi-bus toggle.
    const int hintX = rowControlX() + s(kControlW) + s(kLabelCtrlGap);
    const int hintW = layout.contentX + layout.contentW - hintX;
    drawHint(g, layout.multiBusRowY + s(kRowH) / 2, "(host rescan required after toggling)",
             hintX, hintW);

    // UI Size — secondary hint about font rescaling. Ctor-time setFont calls
    // on text labels capture mu_ui::sf(...) at construction; the live resize
    // doesn't re-walk those, so fonts only fully match the new size after the
    // editor is reopened. Painted hints / knob labels (which fetch fonts each
    // paint) update immediately.
    const int hintY = layout.uiSizeRowY + s(kRowH) + s(kRowGap) / 2;
    drawHint(g, hintY, "(reopen the plugin for label fonts to fully rescale)",
             rowControlX(),
             layout.contentX + layout.contentW - rowControlX());
}
