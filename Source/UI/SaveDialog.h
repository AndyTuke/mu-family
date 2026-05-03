#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "Components/MuClidLookAndFeel.h"
#include "Components/SegmentControl.h"

// Modal overlay for naming and saving a preset.
class SaveDialog : public juce::Component
{
public:
    std::function<void(const juce::String& name,
                       const juce::String& desc,
                       const juce::String& category)> onSave;
    std::function<void()> onCancel;

    SaveDialog();

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;
    void visibilityChanged() override;

private:
    juce::TextEditor   nameEditor;
    juce::TextEditor   descEditor;
    SegmentControl     categoryControl { { "All", "Techno", "Perc", "Ambient", "Xpmt" } };
    juce::TextButton   saveBtn   { "Save" };
    juce::TextButton   cancelBtn { "Cancel" };

    static constexpr int kCardW = 360;
    static constexpr int kCardH = 240;
};
