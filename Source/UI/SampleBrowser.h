#pragma once

#include "../Plugin/PluginProcessor.h"
#include "Components/SegmentControl.h"
#include "Components/MuClidLookAndFeel.h"

// Custom file browser used for sample loading so the user can audition files
// before committing to a slot. Launched inside a DialogWindow (modal) from
// RhythmPanel::loadSample(). Self-contained — no access to RhythmPanel internals.
class SampleBrowserContent : public juce::Component,
                              public juce::FileBrowserListener
{
public:
    SampleBrowserContent(PluginProcessor& proc,
                         const juce::File& startDir,
                         std::function<void(const juce::File&)> onChosen);
    ~SampleBrowserContent() override;

    void resized() override;

    void selectionChanged() override;
    void fileClicked(const juce::File&, const juce::MouseEvent&) override {}
    void fileDoubleClicked(const juce::File& f) override;
    void browserRootChanged(const juce::File&) override {}

private:
    PluginProcessor& proc;
    std::function<void(const juce::File&)> onChosen;
    juce::WildcardFileFilter fileFilter;
    juce::FileBrowserComponent browser;
    juce::TextButton loadBtn { "Load" }, cancelBtn { "Cancel" };
    SegmentControl sourceToggle {
        { juce::String("Main Library"),
          juce::String(juce::CharPointer_UTF8("\xce\xbc-Clid Content")) },
        SegmentControl::ActiveStyle::General,
        SegmentControl::DrawStyle::Pills };

    void commit(const juce::File& f);
};
