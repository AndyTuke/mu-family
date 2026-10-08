#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Persistence/PresetMeta.h"   // preset metadata read from the root tag only
#include <algorithm>
#include <functional>
#include <vector>

// Shared preset-file handling for every mu product: safe file names, atomic writes, listing, and
// a full preset — the product's state tree wrapped in a root element carrying name / description /
// category. What goes inside (a layer node, the composed state) is Persistence/SlotState.h.
namespace mu_pp
{

// Reports a save / load problem to the user (the processor's onLoadError).
using ErrorFn = std::function<void(const juce::String&)>;

// Writes `text` to `dest` atomically: a sibling temp file, then a rename over the target, so a
// full disk / crash / antivirus lock mid-write never leaves a truncated preset — either the new
// bytes land in full or the old file stays. Reports failure through `onError`.
inline bool replaceFileAtomically(const juce::File& dest, const juce::String& text, const ErrorFn& onError = {})
{
    dest.getParentDirectory().createDirectory();
    juce::TemporaryFile tmp(dest);
    if (! tmp.getFile().replaceWithText(text) || ! tmp.overwriteTargetFileWithTemporary())
    {
        if (onError) onError("Could not save \"" + dest.getFileName() + "\" (disk full or folder read-only?)");
        return false;
    }
    return true;
}

// An XML element written to `dest` through replaceFileAtomically.
inline bool writeXmlAtomically(const juce::XmlElement& xml, const juce::File& dest, const ErrorFn& onError = {})
{
    return replaceFileAtomically(dest, xml.toString(), onError);
}

// A preset name made safe for a file name; empty → `fallback`.
inline juce::String safePresetFileName(const juce::String& name, const juce::String& fallback)
{
    auto safe = name.replaceCharacters("\\/:|*?<>\"", "_________");
    return safe.isEmpty() ? fallback : safe;
}

// The preset files in `dir` with extension `ext` (no dot), sorted by name.
inline juce::Array<juce::File> listPresetFiles(const juce::File& dir, const juce::String& ext)
{
    juce::Array<juce::File> files;
    if (dir.isDirectory())
        files = dir.findChildFiles(juce::File::findFiles, false, "*." + ext);
    files.sort();
    return files;
}

// A preset file as a selector lists it: display name and category ("Uncategorised" when none).
struct PresetEntry { juce::File file; juce::String name, category; };

// True for a real category name (not empty, nor the "All" / "Uncategorised" placeholders).
inline bool isNamedCategory(const juce::String& cat)
{
    return cat.isNotEmpty() && cat != "All" && cat != "Uncategorised";
}

// The presets in `dir` with extension `ext` (not `_default`), each with the category it was saved
// under — sorted by category (named ones alphabetically, "Uncategorised" last), then name.
inline std::vector<PresetEntry> listPresetsByCategory(const juce::File& dir, const juce::String& ext)
{
    std::vector<PresetEntry> entries;
    if (! dir.isDirectory()) return entries;
    for (const auto& f : dir.findChildFiles(juce::File::findFiles, false, "*." + ext))
    {
        if (f.getFileNameWithoutExtension().equalsIgnoreCase("_default")) continue;
        PresetEntry e { f, f.getFileNameWithoutExtension(), "Uncategorised" };
        const auto cat = readPresetMeta(f).category;
        if (isNamedCategory(cat))
            e.category = cat;
        entries.push_back(std::move(e));
    }
    std::sort(entries.begin(), entries.end(), [](const PresetEntry& a, const PresetEntry& b)
    {
        const bool aU = a.category == "Uncategorised", bU = b.category == "Uncategorised";
        if (aU != bU) return bU;   // uncategorised last
        const int cc = a.category.compareIgnoreCase(b.category);
        return cc != 0 ? cc < 0 : a.name.compareIgnoreCase(b.name) < 0;
    });
    return entries;
}

// Write a full preset (`state` wrapped in <rootTag name description category>) to
// dir/<safe name>.<ext>. Returns the file written, or an empty File after reporting a failure.
inline juce::File writeFullPreset(const juce::File& dir, const juce::String& ext, const juce::String& rootTag,
                                  const juce::String& name, const juce::String& desc,
                                  const juce::String& category, const juce::ValueTree& state,
                                  const ErrorFn& onError = {})
{
    juce::XmlElement root(rootTag);
    root.setAttribute("name", name);
    root.setAttribute("description", desc);
    root.setAttribute("category", category);
    if (auto xml = state.createXml())
        root.addChildElement(xml.release());
    const auto file = dir.getChildFile(safePresetFileName(name, "Preset") + "." + ext);
    return writeXmlAtomically(root, file, onError) ? file : juce::File();
}

// Read a full preset's state tree (a wrapped <rootTag> or a bare state element of
// `stateType`). On failure returns an invalid tree and sets `error` for the user.
inline juce::ValueTree readFullPreset(const juce::File& file, const juce::String& rootTag,
                                      const juce::Identifier& stateType, juce::String& error)
{
    auto xml = juce::XmlDocument::parse(file);
    if (xml == nullptr) { error = "Could not read \"" + file.getFileName() + "\""; return {}; }
    const auto* stateXml = xml->hasTagName(rootTag) ? xml->getChildByName(stateType.toString()) : xml.get();
    if (stateXml == nullptr || ! stateXml->hasTagName(stateType.toString()))
    {
        error = "Preset has no saved state";
        return {};
    }
    return juce::ValueTree::fromXml(*stateXml);
}

// The named categories used by the full presets in `dir` (first-seen order, no duplicates).
inline juce::StringArray readPresetCategories(const juce::File& dir, const juce::String& ext,
                                              const juce::String& rootTag)
{
    juce::StringArray cats;
    for (const auto& f : listPresetFiles(dir, ext))
    {
        const auto meta = readPresetMeta(f);
        if (meta.rootTag == rootTag && isNamedCategory(meta.category))
            cats.addIfNotAlreadyThere(meta.category);
    }
    return cats;
}

} // namespace mu_pp
