#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "Components/MuClidLookAndFeel.h"
#include "Components/DropdownSelect.h"

// Modal overlay for naming and saving a preset.
class SaveDialog : public juce::Component
{
public:
    std::function<void(const juce::String& name,
                       const juce::String& desc,
                       const juce::String& category,
                       bool embedSamples)> onSave;
    std::function<void()> onCancel;

    SaveDialog();

    // Call before showing to populate the category dropdown with known categories.
    void setKnownCategories(const juce::StringArray& cats);

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;
    void visibilityChanged() override;

private:
    juce::TextEditor   nameEditor;
    juce::TextEditor   descEditor;
    DropdownSelect     categoryDropdown;
    juce::TextEditor   newCategoryEditor;
    juce::ToggleButton embedSamplesToggle { "Embed samples in file" };
    juce::TextButton   saveBtn   { "Save" };
    juce::TextButton   cancelBtn { "Cancel" };

    juce::StringArray  knownCategories;

    juce::String resolveCategory() const;

    static constexpr int kCardW = 360;
    static constexpr int kCardH = 290;
};
