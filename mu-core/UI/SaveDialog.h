#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "UI/Components/MuLookAndFeel.h"
#include "UI/Components/DropdownSelect.h"

// Modal overlay for naming and saving a preset. Plugin-agnostic — each product
// passes its own logo image via setLogoImage(), and products without per-preset
// sample embedding (e.g. wavetable synths) hide the toggle via setShowEmbedSamples(false).
class SaveDialog : public juce::Component
{
public:
    std::function<void(const juce::String& name,
                       const juce::String& desc,
                       const juce::String& category,
                       bool embedSamples)> onSave;
    std::function<void()> onCancel;

    SaveDialog();

    // Product-supplied chrome.
    void setLogoImage(const juce::Image& image);
    void setShowEmbedSamples(bool show);

    // Call before showing to populate the category dropdown with known categories.
    void setKnownCategories(const juce::StringArray& cats);
    // Pre-fill the name editor when the dialog next becomes visible.
    void setDefaultName(const juce::String& name) { pendingDefaultName = name; }
    // Pre-select category and embed state when the dialog next becomes visible.
    void setDefaultCategory(const juce::String& cat) { pendingDefaultCategory = cat; }
    void setDefaultEmbed(bool embed) { pendingDefaultEmbed = embed; }
    // Returns true if "Save as Default" is checked.
    bool isSaveAsDefault() const { return saveAsDefaultToggle.getToggleState(); }

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;
    void visibilityChanged() override;

private:
    juce::TextEditor   nameEditor;
    juce::TextEditor   descEditor;
    DropdownSelect     categoryDropdown;
    juce::TextEditor   newCategoryEditor;
    juce::ToggleButton embedSamplesToggle  { "Embed samples in file" };
    juce::ToggleButton saveAsDefaultToggle { "Save as Default" };
    juce::TextButton   saveBtn   { "Save" };
    juce::TextButton   cancelBtn { "Cancel" };

    juce::StringArray  knownCategories;
    juce::String       pendingDefaultName;
    juce::String       pendingDefaultCategory;
    bool               pendingDefaultEmbed = false;
    bool               showEmbedSamples = true;
    juce::Image        logoImage;

    void updateDefaultModeState();

    juce::String resolveCategory() const;

    static constexpr int kCardW = 360;
    static constexpr int kCardH = 360;
};
