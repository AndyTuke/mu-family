#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "UI/Components/MuLookAndFeel.h"
#include "UI/PerformanceChooserModel.h"
#include "UI/SaveDialog.h"
#include <array>
#include <functional>
#include <memory>

// The Performance chooser screen (docs/design-ui-family.md 12.5): the saved performances on the left,
// the working copy's eight item rows on the right (number lamp, name, product, Replace / Up / Down /
// Remove, or "+ Preset" on an empty row), New / Save / Rename / Delete in the header and Close / Use in
// the footer. It edits through mu_ui::perf::PerformanceChooserModel; the owner (ShellChrome) shows it as a page,
// opens the Preset Browser in pick mode when asked for a preset, and puts the list into use. Reachable
// only with Performance mode off. Processor-free.
namespace mu_ui
{

class PerformanceChooser : public juce::Component,
                           private juce::ListBoxModel
{
public:
    PerformanceChooser();
    ~PerformanceChooser() override;

    // ─── The owner's wiring ──────────────────────────────────────────────────
    // Asks for a preset: the owner shows the Preset Browser in pick mode and calls `picked` with the choice.
    std::function<void(std::function<void(const juce::File&)> picked)> onPickPreset;
    // Puts a saved list into use (the live state's load); false when it is refused.
    std::function<bool(mu_pp::PerformanceFile list, const juce::File& where)> onUse;
    std::function<void()>                                                      onClose;
    std::function<void(const juce::String& name, const juce::String& value)>   onStatus;

    // Starts an editing session: the folders and product, and the list now in use (empty = start a new one).
    void beginSession(const juce::File& performancesFolder, const juce::File& presetsFolder,
                      const juce::String& ownProduct, const mu_pp::PerformanceFile& inUse, const juce::File& inUseFile);
    // Re-reads the model into the rows, the list and the buttons (after any edit, or on coming back from a pick).
    void refresh();

    SaveDialog&               getSaveDialog() noexcept  { return saveDialog; }
    perf::PerformanceChooserModel&       getModel() noexcept       { return model; }
    const perf::PerformanceChooserModel& getModel() const noexcept { return model; }

    // The screen's actions, as the buttons call them (public so they are driven without a mouse).
    void actionNew();
    void actionSave();
    void actionRename();
    void actionDelete();
    void actionUse();
    void actionClose();
    void actionOpenSaved(int libraryRow);
    void actionPick(int row);   // "+ Preset" / "Replace" on a row

    // Layout, tested for fit.
    struct Layout
    {
        juce::Rectangle<int> list, header, footer;
        std::array<juce::Rectangle<int>, perf::PerformanceChooserModel::kItems> rows;
    };
    static Layout computeLayout(juce::Rectangle<int> bounds);

    void paint(juce::Graphics&) override;
    void resized() override;
    void visibilityChanged() override;

private:
    // One item row: number lamp and number, name LCD (or "+ Preset"), product label and the row buttons.
    class Row : public juce::Component
    {
    public:
        Row(PerformanceChooser& owner, int index);
        void update();
        void paint(juce::Graphics&) override;
        void resized() override;
        juce::TextButton addBtn { "+ Preset" }, replaceBtn { "Replace" }, upBtn { "Up" }, downBtn { "Down" }, removeBtn { "Remove" };
        int  index;
    private:
        PerformanceChooser& chooser;
        juce::Rectangle<int> lcdRect, productRect;
    };

    // ListBoxModel: the saved performances.
    int  getNumRows() override;
    void paintListBoxItem(int row, juce::Graphics&, int w, int h, bool selected) override;
    void listBoxItemClicked(int row, const juce::MouseEvent&) override;

    void guardDirty(std::function<void()> proceed);
    void openSaveDialog();
    void commitSave(const juce::String& name, const juce::String& description);
    juce::String currentName() const;
    void putIntoUse(bool justSaved = false);
    void say(const juce::String& name, const juce::String& value) const;
    juce::String titleText() const;

    perf::PerformanceChooserModel model;
    juce::ListBox      listBox;
    std::array<std::unique_ptr<Row>, perf::PerformanceChooserModel::kItems> rows;
    juce::TextButton   newBtn { "New" }, saveBtn { "Save" }, renameBtn { "Rename" }, deleteBtn { "Delete" };
    juce::TextButton   closeBtn { "Close" }, useBtn { "Use" };
    SaveDialog         saveDialog;
    bool               useAfterSave = false;   // the Save card was opened by Use on an unsaved list
};

} // namespace mu_ui
