#include "UI/SampleBrowser.h"

namespace mu_core {

SampleBrowserContent::SampleBrowserContent(SampleLibrary& lib,
                                            const juce::File& content,
                                            const juce::String& contentLabel,
                                            const juce::File& startDir,
                                            std::function<void(const juce::File&)> onChosen)
    : library(lib), contentDir(content), onChosen(std::move(onChosen)),
      sourceToggle({ juce::String("Main Library"), contentLabel },
                   SegmentControl::ActiveStyle::General, SegmentControl::DrawStyle::Pills),
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
    // one-click access to the product's factory/preset-bundled samples folder. Selected
    // index reflects the current root so the toggle stays honest when the user
    // navigates away via the browser, then jumps back via the toggle.
    sourceToggle.setSelectedIndex(
        startDir == contentDir ? 1 : 0,
        juce::dontSendNotification);
    sourceToggle.onChange = [this](int idx)
    {
        const juce::File target = (idx == 1) ? contentDir
                                             : library.getPrimarySampleDir();
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
        library.stopPreview();
        if (onDismiss) onDismiss();
    };

    setSize(mu_ui::s(560), mu_ui::s(470));
}

SampleBrowserContent::~SampleBrowserContent() { library.stopPreview(); }

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
    pendingPreview = browser.getSelectedFile(0);
    startTimer(150);
}

void SampleBrowserContent::timerCallback()
{
    stopTimer();
    if (pendingPreview.existsAsFile())
        library.startPreview(pendingPreview);
}

void SampleBrowserContent::fileDoubleClicked(const juce::File& f) { commit(f); }

void SampleBrowserContent::commit(const juce::File& f)
{
    stopTimer();
    library.stopPreview();
    onChosen(f);
    if (onDismiss) onDismiss();
}

} // namespace mu_core
