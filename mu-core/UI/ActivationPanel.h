#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "Plugin/ProcessorBase.h"

// Non-blocking activation overlay (shared, family-wide). Opened from the demo banner / an
// Activate affordance — the plugin keeps running in Demo until the user activates. Online
// path: paste the Lemon Squeezy key → activate against this machine's fingerprint (off the
// message thread). Offline path: shows the machine "challenge" code to send the owner for a
// signed `.lic`. Click outside the card → dismiss.
class ActivationPanel : public juce::Component
{
public:
    explicit ActivationPanel (ProcessorBase& proc);

    void setProductName (const juce::String& name) { productName = name; repaint(); }

    std::function<void()> onDismiss;     // close the overlay
    std::function<void()> onActivated;   // license state changed → shell refreshes chrome

    void paint (juce::Graphics& g) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent& e) override;

private:
    void activatePressed();
    void setStatus (const juce::String& text, bool error);
    juce::Rectangle<int> cardBounds() const;

    ProcessorBase& processorRef;
    juce::String   productName;

    juce::Label      keyLabel;
    juce::TextEditor keyEditor;
    juce::TextButton activateBtn { "Activate" };
    juce::TextButton closeBtn    { "Close" };
    juce::Label      statusLabel;

    static constexpr int kCardW = 460;
    static constexpr int kCardH = 340;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ActivationPanel)
};
