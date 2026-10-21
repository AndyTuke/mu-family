#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "UI/Components/MuLookAndFeel.h"
#include "UI/Components/DropdownSelect.h"
#include <vector>

// Panel overlay listing saved presets.  Replaces main area when visible.
class PresetBrowser : public juce::Component,
                      public juce::ListBoxModel
{
public:
    std::function<void(const juce::File&)> onLoadPreset;
    std::function<void()>                  onClose;
    // Opens the Performance chooser: a "Performances" button in the footer, shown only while this is set.
    std::function<void()>                  onChoosePerformance;

    PresetBrowser();

    // Call after setting or clearing onChoosePerformance.
    void refreshFooter();

    // Pick mode: the next Pick (button or double-click) hands the chosen preset to `picked` instead of
    // loading it, then closes. Close, or the browser being hidden any other way, ends the mode.
    void beginPick(std::function<void(const juce::File&)> picked);
    bool isPicking() const noexcept { return static_cast<bool>(pickCallback); }
    // Selects a listed row and loads (or picks) it, as a double-click does.
    void chooseRow(int row);

    // file extension to scan for (without the dot). Call BEFORE refresh().
    // Consumers should always set this explicitly — the empty default scans
    // no files. Each consuming plugin's preset extension comes from its
    // ProcessorBase virtuals (getLayerPresetExtension / getFullPresetExtension).
    void setFileExtension(juce::StringRef extWithoutDot) { fileExtension = extWithoutDot; }

    // Rescan presets folder.
    void refresh(const juce::File& presetsDir);

    // Returns unique non-"All" categories found in the last refresh().
    juce::StringArray getCategories() const { return knownCategories; }

    void resized() override;
    void paint(juce::Graphics& g) override;
    void visibilityChanged() override;

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
    void endPick();

    juce::TextEditor  searchBox;
    DropdownSelect    categoryFilter;
    juce::ListBox     listBox;
    juce::TextButton  loadBtn   { "Load" };
    juce::TextButton  closeBtn  { "Close" };
    juce::TextButton  performancesBtn { "Performances" };

    std::vector<PresetInfo> allPresets;
    std::vector<int>        filteredIndices;
    juce::File              presetsDir;
    juce::StringArray       knownCategories;
    int                     selectedRow = -1;
    juce::String            fileExtension;   // consumer sets via setFileExtension()
    std::function<void(const juce::File&)> pickCallback;   // set while in pick mode

    static constexpr int kTopBarH  = 40;
    static constexpr int kBotBarH  = 40;
    static constexpr int kPad      = 8;
};
