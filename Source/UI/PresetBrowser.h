#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "Components/MuClidLookAndFeel.h"
#include "Components/SegmentControl.h"
#include <vector>

// Panel overlay listing saved presets.  Replaces main area when visible.
class PresetBrowser : public juce::Component,
                      public juce::ListBoxModel
{
public:
    std::function<void(const juce::File&)> onLoadPreset;
    std::function<void()>                  onClose;

    PresetBrowser();

    // Rescan presets folder.
    void refresh(const juce::File& presetsDir);

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
    SegmentControl    categoryFilter { { "All", "Techno", "Perc", "Ambient", "Xpmt" } };
    juce::ListBox     listBox;
    juce::TextButton  loadBtn   { "Load" };
    juce::TextButton  closeBtn  { "Close" };

    std::vector<PresetInfo> allPresets;
    std::vector<int>        filteredIndices;
    juce::File              presetsDir;
    int                     selectedRow = -1;

    static constexpr int kTopBarH  = 40;
    static constexpr int kBotBarH  = 40;
    static constexpr int kPad      = 8;
};
