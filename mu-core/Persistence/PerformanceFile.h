#pragma once

#include <juce_core/juce_core.h>
#include "MuLimits.h"
#include <array>

// A Performance: a list of up to kMaxPerformanceItems pointers to full-preset files, for switching a whole
// preset live from a controller's top row (docs/design-launchpad.md section 3.2c). Plain data: load, save and
// resolve, no processor dependency.
//
// File: <MuPerformance format="1" name="" description=""><Item idx="0..7" product="MuTantState" path="" name=""/>...
// Pointers only: the file never embeds a preset and a preset never references a performance. An absent idx is an
// empty item. `product` is the state identifier of the preset's product, so a mixed list needs no format change.
// `path` is relative to the product's preset folder when the preset lies under it, absolute otherwise; `name` is a
// cached display name. A missing or foreign preset stays in the list, marked, and the file is never rewritten silently.
namespace mu_pp
{

class PerformanceFile
{
public:
    static constexpr int kFormat = 1;
    static constexpr int kMaxItems = mu_limits::kMaxPerformanceItems;

    struct Item
    {
        bool         used = false;   // false = an empty item
        juce::String product;        // the preset's product state identifier ("MuTantState")
        juce::String path;           // as stored: relative to the preset folder, or absolute
        juce::String name;           // cached display name
    };

    enum class LoadResult { Ok, NotFound, Invalid, UnknownFormat };

    // What an item is, for the product reading the list: Empty, ready to play, its file gone, or another product's.
    enum class ItemStatus { Empty, Ok, Missing, OtherProduct };

    juce::String                  name;
    juce::String                  description;
    std::array<Item, kMaxItems>   items;
    int                           ignoredItems = 0;   // items past kMaxItems or with a bad idx in the file read last

    // Parse a file or XML text into `out` (replacing it). A file whose format is newer than kFormat is refused,
    // not guessed at. Items beyond kMaxItems, or with a bad or repeated idx, are not loaded and are counted.
    static LoadResult load(const juce::File& file, PerformanceFile& out);
    static LoadResult parse(const juce::String& xmlText, PerformanceFile& out);

    juce::String toXml() const;
    bool         save(const juce::File& file) const;

    // Point item `idx` at `preset`. The path is stored relative to `presetFolder` when the preset lies under it.
    // Returns false for an idx outside 0 .. kMaxItems - 1.
    bool setItem(int idx, const juce::String& product, const juce::File& preset, const juce::File& presetFolder,
                 const juce::String& displayName);
    void clearItem(int idx);

    // The preset file for an item (relative paths resolved against `presetFolder`); juce::File() for an empty item.
    juce::File resolve(int idx, const juce::File& presetFolder) const;

    // `ownProduct` is the reader's state identifier: another product's item is OtherProduct, never loaded.
    ItemStatus status(int idx, const juce::File& presetFolder, const juce::String& ownProduct) const;

    // How many items are in use (not empty).
    int count() const noexcept;

    // The stored form of a preset path: relative to `presetFolder` when it lies under it, else absolute.
    static juce::String storedPath(const juce::File& preset, const juce::File& presetFolder);
};

} // namespace mu_pp
