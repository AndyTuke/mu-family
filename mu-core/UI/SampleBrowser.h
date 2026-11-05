#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "Audio/SampleLibrary.h"
#include "UI/Components/SegmentControl.h"
#include "UI/Components/MuLookAndFeel.h"

namespace mu_core {

// Custom file browser used for sample loading so the user can audition files before committing to a slot (lifted
// from mu-Clid). It previews through the SampleLibrary it is given and has two sources: the user's primary sample
// folder ("Main Library") and the product's content folder, named by `contentLabel`. Shown by the caller inside an
// OverlayHost. Self-contained: no access to the layer's UI internals.
// What the browser needs from its product: the user's primary sample folder and the audition (start / stop a file). A SampleLibrary
// supplies it for the products that have one; a product without one (mu-Tilate) fills it from its own layer.
struct SampleBrowserSource
{
    std::function<juce::File()>                primaryDir;
    std::function<void(const juce::File&)>     startPreview;
    std::function<void()>                      stopPreview;
};

class SampleBrowserContent : public juce::Component,
                              public juce::FileBrowserListener,
                              private juce::Timer
{
public:
    SampleBrowserContent(SampleLibrary& library,
                         const juce::File& contentDir,
                         const juce::String& contentLabel,
                         const juce::File& startDir,
                         std::function<void(const juce::File&)> onChosen);
    SampleBrowserContent(SampleBrowserSource source,
                         const juce::File& contentDir,
                         const juce::String& contentLabel,
                         const juce::File& startDir,
                         std::function<void(const juce::File&)> onChosen);
    ~SampleBrowserContent() override;

    // Called when the browser wants to close (Load committed or Cancel). The host (an
    // in-editor OverlayHost) wires this to remove the overlay.
    std::function<void()> onDismiss;

    void resized() override;

    void selectionChanged() override;
    void fileClicked(const juce::File&, const juce::MouseEvent&) override {}
    void fileDoubleClicked(const juce::File& f) override;
    void browserRootChanged(const juce::File&) override {}

private:
    SampleBrowserSource source;
    juce::File     contentDir;
    std::function<void(const juce::File&)> onChosen;
    juce::WildcardFileFilter fileFilter;
    juce::FileBrowserComponent browser;
    juce::TextButton loadBtn { "Load" }, cancelBtn { "Cancel" };
    SegmentControl sourceToggle;

    void commit(const juce::File& f);
    // A selection change previews after a short pause, so stepping through files does not decode each one in turn.
    void timerCallback() override;
    juce::File pendingPreview;
};

} // namespace mu_core
