#include "SampleBrowser.h"

SampleBrowserContent::SampleBrowserContent(PluginProcessor& proc,
                                            const juce::File& startDir,
                                            std::function<void(const juce::File&)> onChosen)
    : proc(proc), onChosen(std::move(onChosen)),
      fileFilter("*.wav;*.aiff;*.aif;*.mp3;*.flac", {}, "Audio files"),
      browser(juce::FileBrowserComponent::openMode |
              juce::FileBrowserComponent::canSelectFiles,
              startDir, &fileFilter, nullptr)
{
    addAndMakeVisible(browser);
    addAndMakeVisible(loadBtn);
    addAndMakeVisible(cancelBtn);
    addAndMakeVisible(sourceToggle);
    browser.addListener(this);

    // source toggle — Library is the default landing folder; Content gives
    // one-click access to the factory/preset-bundled samples folder. Selected
    // index reflects the current root so the toggle stays honest when the user
    // navigates away via the browser, then jumps back via the toggle.
    sourceToggle.setSelectedIndex(
        startDir == this->proc.getSamplesDir() ? 1 : 0,
        juce::dontSendNotification);
    sourceToggle.onChange = [this](int idx)
    {
        const juce::File target = (idx == 1) ? this->proc.getSamplesDir()
                                             : this->proc.getPrimarySampleDir();
        if (target.isDirectory())
            browser.setRoot(target);
    };

    loadBtn.onClick = [this]
    {
        const auto f = browser.getSelectedFile(0);
        if (f.existsAsFile()) commit(f);
    };
    cancelBtn.onClick = [this]
    {
        this->proc.stopSamplePreview();
        if (auto* dw = findParentComponentOfClass<juce::DialogWindow>())
            dw->exitModalState(0);
    };

    setSize(mu_ui::s(560), mu_ui::s(470));
}

SampleBrowserContent::~SampleBrowserContent() { proc.stopSamplePreview(); }

void SampleBrowserContent::resized()
{
    using mu_ui::s;
    auto area = getLocalBounds().reduced(s(8));
    auto topRow = area.removeFromTop(s(26));
    sourceToggle.setBounds(topRow.removeFromLeft(s(240)));
    area.removeFromTop(s(6));

    auto btnRow = area.removeFromBottom(s(32)).reduced(0, s(4));
    cancelBtn.setBounds(btnRow.removeFromRight(s(80)));
    btnRow.removeFromRight(s(8));
    loadBtn.setBounds(btnRow.removeFromRight(s(80)));
    browser.setBounds(area.reduced(0, s(4)));
}

void SampleBrowserContent::selectionChanged()
{
    const auto f = browser.getSelectedFile(0);
    if (f.existsAsFile())
        proc.startSamplePreview(f);
}

void SampleBrowserContent::fileDoubleClicked(const juce::File& f) { commit(f); }

void SampleBrowserContent::commit(const juce::File& f)
{
    proc.stopSamplePreview();
    onChosen(f);
    if (auto* dw = findParentComponentOfClass<juce::DialogWindow>())
        dw->exitModalState(1);
}
