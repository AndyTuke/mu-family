#pragma once
#include <juce_core/juce_core.h>
#include "Persistence/PerformanceFile.h"
#include <functional>
#include <vector>

// The editing state behind the Performance chooser screen (docs/design-ui-family.md 12.5): the saved
// performances in the Performances folder, and one working copy being edited (reorder, remove, add,
// replace) before it is saved or put into use. Plain data over mu_pp::PerformanceFile and juce::File,
// no component and no display, so every rule of the screen is unit-tested.
namespace mu_ui::perf
{

// The folder performances live in, under the product's content root (beside Presets).
inline juce::File performancesDir(const juce::File& contentDir) { return contentDir.getChildFile("Performances"); }

class PerformanceChooserModel
{
public:
    static constexpr int kItems = mu_pp::PerformanceFile::kMaxItems;

    // One saved performance in the folder.
    struct Entry { juce::File file; juce::String name; };

    // Where performances are kept, where the presets they point at live, and this product's state
    // identifier (another product's item is shown but not playable).
    void setContext(const juce::File& performancesFolder, const juce::File& presetsFolder, const juce::String& ownProduct);

    // ─── The saved performances ──────────────────────────────────────────────
    // Re-reads the folder: every .muPerformance file, sorted by name. A file that is not a performance
    // is still listed (by its file name) so it can be deleted.
    void rescan();
    const juce::File& performancesFolder() const noexcept { return folder; }
    const std::vector<Entry>& library() const noexcept { return entries; }
    // The library row of the working copy's file, or -1 (a new or unsaved performance).
    int libraryRowOfWorking() const;
    // Deletes a saved performance's file; if it is the working copy's file the working copy becomes a new one.
    bool deleteFile(const juce::File& file);
    // True when `file` lives in the performances folder (or no folder is set).
    bool isInFolder(const juce::File& file) const;

    // ─── The working copy ────────────────────────────────────────────────────
    const mu_pp::PerformanceFile& working() const noexcept     { return current; }
    const juce::File&             workingFile() const noexcept { return currentFile; }
    bool                          isDirty() const noexcept     { return dirty; }
    bool                          hasFile() const noexcept     { return currentFile != juce::File(); }
    // True while the list was loaded from outside the performances folder and not yet saved into it.
    bool                          isImport() const noexcept    { return importFile != juce::File(); }
    const juce::File&             importSource() const noexcept { return importFile; }

    void startNew();                                                  // an empty, unsaved list
    void adopt(const mu_pp::PerformanceFile& list, const juce::File& where);   // the list already in use, as saved
    // Loads a saved one; "" or why not. A file outside the performances folder is an import (isImport()).
    juce::String open(const juce::File& file);

    // ─── Items ───────────────────────────────────────────────────────────────
    bool isUsed(int row) const noexcept;
    mu_pp::PerformanceFile::ItemStatus status(int row) const;         // Empty / Ok / Missing / OtherProduct
    juce::String itemName(int row) const;                             // cached display name
    // "MuTant" for the state identifier "MuTantState", "missing" for a gone file, "" for an empty row.
    juce::String productLabel(int row) const;

    bool canMoveUp(int row) const noexcept;
    bool canMoveDown(int row) const noexcept;
    bool moveUp(int row);                                             // swaps with the row above
    bool moveDown(int row);                                           // swaps with the row below (a used one)
    bool removeItem(int row);                                         // later items move up one
    // Points `row` (empty or used: a replace) at `preset`, as this product's item.
    bool setItem(int row, const juce::File& preset);

    // ─── Saving and using ────────────────────────────────────────────────────
    // The file `name` is saved to, and whether another performance already owns that name.
    juce::File fileFor(const juce::String& name) const;
    bool       nameTaken(const juce::String& name) const;
    // Writes the working copy as `name` (renaming the old file when the name changed), clears dirty
    // and rescans. False when the name is empty or the write failed.
    bool save(const juce::String& name, const juce::String& description);
    // True when there is something to play.
    bool canUse() const noexcept { return current.count() > 0; }
    // Hands the saved working copy to `loader` (the live state's load). False without calling it when
    // the copy is unsaved or changed since saving, or has no items; else what `loader` answers.
    bool use(const std::function<bool(mu_pp::PerformanceFile, const juce::File&)>& loader) const;

private:
    juce::File             folder, presets;
    juce::String           product;
    std::vector<Entry>     entries;
    mu_pp::PerformanceFile current;
    juce::File             currentFile;
    juce::File             importFile;   // the outside file an import came from (never written)
    bool                   dirty = false;
};

} // namespace mu_ui::perf
