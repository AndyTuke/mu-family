#include "SettingsOverlay.h"
#include "Plugin/PluginProcessor.h"

SettingsOverlay::SettingsOverlay(PluginProcessor& p)
    : StandardSettingsOverlay(p, { /*showTransport*/ false }), product(p)
{
    // ── Output — multi-bus toggle, with a host-rescan hint beside it ───────────
    multiBusToggle.setToggleState(product.getMultiBusEnabled(), juce::dontSendNotification);
    multiBusToggle.onClick = [this] { product.setMultiBusEnabled(multiBusToggle.getToggleState()); };
    addAndMakeVisible(multiBusToggle);
    addSection(Where::General, { "Output", kRowH,
        [this](const Rows& r) { multiBusToggle.setBounds(r.ctrlX, r.area.getY(), r.ctrlW, r.rowH); },
        [this](juce::Graphics& g, const Rows& r) {
            const int hintX = r.ctrlX + r.ctrlW + mu_ui::s(kLabelCtrlGap);
            drawHint(g, r.area.getY() + r.rowH / 2, "(host rescan required after toggling)",
                     hintX, r.area.getRight() - hintX); } });

    // ── Hot-swap timing ────────────────────────────────────────────────────────
    makeFieldLabel(swapModeLabel, "Timing");
    swapModeDropdown.addItem("On master loop", 1);
    swapModeDropdown.addItem("On rhythm loop", 2);
    swapModeDropdown.setSelectedId((int) product.getSwapMode() + 1, false);
    swapModeDropdown.onChange = [this](int id)
    {
        product.setSwapMode(id == 2 ? PluginProcessor::SwapMode::OnRhythmLoop
                                    : PluginProcessor::SwapMode::OnMasterLoop);
    };
    addAndMakeVisible(swapModeDropdown);
    addSection(Where::MidiBeforeClock, { "Hot-swap", kRowH, [this](const Rows& r) {
        swapModeLabel   .setBounds(r.labelX, r.area.getY(), r.labelW, r.rowH);
        swapModeDropdown.setBounds(r.ctrlX,  r.area.getY(), r.ctrlW,  r.rowH); } });

    // ── MIDI Mode (plugin only) ────────────────────────────────────────────────
    if (! isStandalone)
    {
        makeFieldLabel(midiModeLabel, "Mode");
        midiModeDropdown.addItem("Free", 1);
        midiModeDropdown.addItem("Note", 2);
        midiModeDropdown.setSelectedId(product.getMidiNoteMode() + 1, false);
        midiModeDropdown.onChange = [this](int id) { product.setMidiNoteMode(id - 1); };
        addAndMakeVisible(midiModeDropdown);
        addSection(Where::MidiAfterClock, { "MIDI Mode", kRowH, [this](const Rows& r) {
            midiModeLabel   .setBounds(r.labelX, r.area.getY(), r.labelW, r.rowH);
            midiModeDropdown.setBounds(r.ctrlX,  r.area.getY(), r.ctrlW,  r.rowH); } });
    }

    // ── Program-change tables (the shared section) ─────────────────────────────
    addProgramChangeSection("Rhythm Preset Table", "Main Preset Table");

    // ── Locations — the folder paths are primary content, so heading colour ────
    for (auto* l : { &sampleLibLabel, &contentFolderLabel })
    {
        makeFieldLabel(*l, {});
        l->setColour(juce::Label::textColourId, MuLookAndFeel::colour(MuLookAndFeel::headingText));
    }
    updateSampleLibLabel();
    updateFolderLabel();

    browseSampleLibBtn.onClick = [this]
    {
        juce::Component::SafePointer<SettingsOverlay> safe(this);
        fileChooser = std::make_unique<juce::FileChooser>("Choose primary sample library...", product.getPrimarySampleDir());
        fileChooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
            [safe](const juce::FileChooser& fc)
            {
                if (! safe) return;
                if (auto result = fc.getResult(); result.isDirectory())
                {
                    safe->product.setPrimarySampleDir(result);
                    safe->updateSampleLibLabel();
                }
            });
    };
    resetSampleLibBtn.onClick = [this]
    {
        product.setPrimarySampleDir(juce::File());   // clears the override → OS Music folder
        updateSampleLibLabel();
    };
    browseContentFolderBtn.onClick = [this]
    {
        juce::Component::SafePointer<SettingsOverlay> safe(this);
        fileChooser = std::make_unique<juce::FileChooser>("Choose content folder...", product.getContentDir());
        fileChooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
            [safe](const juce::FileChooser& fc)
            {
                if (! safe) return;
                if (auto result = fc.getResult(); result.isDirectory())
                {
                    safe->product.setContentDir(result);
                    safe->updateFolderLabel();
                    if (safe->onContentDirChanged) safe->onContentDirChanged();
                }
            });
    };
    resetContentFolderBtn.onClick = [this]
    {
        product.setContentDir(juce::File());
        updateFolderLabel();
        if (onContentDirChanged) onContentDirChanged();
    };
    for (auto* b : { &browseSampleLibBtn, &resetSampleLibBtn, &browseContentFolderBtn, &resetContentFolderBtn })
        addAndMakeVisible(b);

    addSection(Where::Group, { "Sample Library", 2 * kRowH + kRowGap, [this](const Rows& r) {
        placeFolderRows(r, sampleLibLabel, browseSampleLibBtn, resetSampleLibBtn); } }, "Locations");
    addSection(Where::Group, { "Content Folder", 2 * kRowH + kRowGap, [this](const Rows& r) {
        placeFolderRows(r, contentFolderLabel, browseContentFolderBtn, resetContentFolderBtn); } }, "Locations");
}

void SettingsOverlay::placeFolderRows(const Rows& r, juce::Label& path, juce::TextButton& browse, juce::TextButton& reset)
{
    using mu_ui::s;
    const int btnW = s(kFolderBtnW), gap = s(kFolderBtnGap);
    const int btnY = r.area.getY() + r.rowH + s(kRowGap);
    path  .setBounds(r.area.getX(), r.area.getY(), r.area.getWidth(), r.rowH);
    reset .setBounds(r.area.getRight() - btnW,           btnY, btnW, r.rowH);
    browse.setBounds(r.area.getRight() - btnW * 2 - gap, btnY, btnW, r.rowH);
}

void SettingsOverlay::updateFolderLabel()
{
    contentFolderLabel.setText(product.getContentDir().getFullPathName(), juce::dontSendNotification);
}

void SettingsOverlay::updateSampleLibLabel()
{
    sampleLibLabel.setText(product.getPrimarySampleDir().getFullPathName(), juce::dontSendNotification);
}
