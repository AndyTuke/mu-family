#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "UI/Components/MuClidLookAndFeel.h"
#include "UI/Components/DropdownSelect.h"
#include <vector>

// Panel overlay listing saved presets.  Replaces main area when visible.
class PresetBrowser : public juce::Component,
                      public juce::ListBoxModel
{
public:
    std::function<void(const juce::File&)> onLoadPreset;
    std::function<void()>                  onClose;

    PresetBrowser();

    // file extension to scan for. Defaults to "muclid" (full plugin presets);
    // set to "muRhyth" for rhythm-only presets when reusing this browser in the
    // MIDI program-change panel. Call BEFORE refresh().
    void setFileExtension(juce::StringRef extWithoutDot) { fileExtension = extWithoutDot; }

    // Rescan presets folder.
    void refresh(const juce::File& presetsDir);

    // Returns unique non-"All" categories found in the last refresh().
    juce::StringArray getCategories() const { return knownCategories; }

    void resized() override;
    void paint(juce::Graphics& g) override;

    // ListBoxModel
    int  getNumRows() override;
    void paintListBoxItem(int row, juce::Graphics& g, int w, int h, bool selected) override;
    void listBoxItemDoubleClicked(int row, const juce::MouseEvent&) override;
    void listBoxItemClicked(int row, const juce::MouseEvent& e) override;

private:
    struct PresetInfo
    {
        juce::File   file;
        juce::String name;
        juce::String category;
        juce::String description;
    };

    void applyFilter();
    void loadSelectedPreset();

    juce::TextEditor  searchBox;
    DropdownSelect    categoryFilter;
    juce::ListBox     listBox;
    juce::TextButton  loadBtn   { "Load" };
    juce::TextButton  closeBtn  { "Close" };

    std::vector<PresetInfo> allPresets;
    std::vector<int>        filteredIndices;
    juce::File              presetsDir;
    juce::StringArray       knownCategories;
    int                     selectedRow = -1;
    juce::String            fileExtension { "muclid" };

    static constexpr int kTopBarH  = 40;
    static constexpr int kBotBarH  = 40;
    static constexpr int kPad      = 8;
};
