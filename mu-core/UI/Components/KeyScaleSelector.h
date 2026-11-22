#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <functional>
#include <memory>
#include "MuLookAndFeel.h"
#include "UI/StatusSink.h"

namespace mu_ui
{

// The family's one key + scale control (docs/design-ui-family.md section 6.8). A single LCD window reads the pair as one phrase
// ("C Minor"); clicking it opens a floating raised panel with a one-octave piano keyboard (the key) over the twelve scales of
// mu_audio::kScales in two columns. A pick applies at once and the panel stays open; it closes on a click outside, Esc, or a
// click on the LCD. The mouse wheel over the closed LCD steps the scale.
//
// Callback-driven, so it fits any product: setKey / setScale update the display without a callback; onKeyChanged / onScaleChanged
// fire on a user pick. attachKeyScale() binds it to an APVTS key id and scale id (int or choice parameters).
class KeyScaleSelector : public juce::Component
{
public:
    std::function<void(int key)>   onKeyChanged;     // 0..11 (C..B)
    std::function<void(int scale)> onScaleChanged;   // index into mu_audio::kScales

    KeyScaleSelector();
    ~KeyScaleSelector() override;

    void setKey(int key0to11);
    void setScale(int scaleIndex);
    int  getKey() const noexcept   { return key; }
    int  getScale() const noexcept { return scale; }

    // "C Minor": what the closed LCD shows.
    static juce::String phrase(int key, int scale);
    // Width of the closed LCD in Medium pixels (kTonalRootW + kTonalScaleW); a host sets bounds with this and kDropdownH.
    static int closedWidth() noexcept { return MuLookAndFeel::kTonalRootW + MuLookAndFeel::kTonalScaleW; }

    void closePanel();
    bool isPanelOpen() const noexcept { return panel != nullptr; }

    void paint(juce::Graphics&) override;
    void mouseEnter(const juce::MouseEvent&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void parentHierarchyChanged() override;

private:
    class Panel;
    void openPanel();
    void pickKey(int k);
    void pickScale(int s);

    int key = 0, scale = 0;
    bool hover = false;
    std::unique_ptr<Panel> panel;
};

// The selector set into a transport pane (TransportBar::setLoopSection): centred at the family dropdown height, with its engraved "Key"
// caption. A product that has a master key / scale (mu-Tronic) hands this to the bar.
class KeyScalePane : public juce::Component
{
public:
    KeyScalePane() { addAndMakeVisible(selector); }
    // Width in Medium pixels the pane needs: caption, gap, the selector and the pane's side padding.
    static int width() noexcept
    { return 2 * MuLookAndFeel::kSpaceS + MuLookAndFeel::kDropdownCaptionW + MuLookAndFeel::kSpaceXS + KeyScaleSelector::closedWidth(); }
    void paint(juce::Graphics&) override;
    void resized() override;
    KeyScaleSelector selector;
private:
    juce::Rectangle<int> captionR;
};

// Binds `selector` to the parameters `keyId` and `scaleId` of `apvts` (an int or a choice parameter each: mu-Toni's root is an int
// 0..11, mu-Tant's a choice). Keeps the selector in step with the parameters and writes user picks back. Destroy it before the selector.
class KeyScaleAttachment : private juce::AudioProcessorValueTreeState::Listener, private juce::AsyncUpdater
{
public:
    KeyScaleAttachment(juce::AudioProcessorValueTreeState& apvts, const juce::String& keyId, const juce::String& scaleId,
                       KeyScaleSelector& selector);
    ~KeyScaleAttachment() override;

private:
    void parameterChanged(const juce::String&, float) override { triggerAsyncUpdate(); }
    void handleAsyncUpdate() override;
    static int  readIndex(juce::RangedAudioParameter* p);
    static void writeIndex(juce::RangedAudioParameter* p, int index);

    juce::AudioProcessorValueTreeState& state;
    juce::String keyId, scaleId;
    KeyScaleSelector& sel;
};

} // namespace mu_ui
