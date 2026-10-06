#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

// Shared preset-file handling for every mu product. A full preset is the product's whole
// state tree wrapped in a root element carrying name / description / category; a layer
// preset holds one layer's parameters as <p id v> rows (id without the layer's prefix,
// v normalised 0..1) plus whatever product children the caller adds (modulators, patterns).
namespace mu_pp
{

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

// Write a full preset (`state` wrapped in <rootTag name description category>) to
// dir/<safe name>.<ext>. Returns the file written.
inline juce::File writeFullPreset(const juce::File& dir, const juce::String& ext, const juce::String& rootTag,
                                  const juce::String& name, const juce::String& desc,
                                  const juce::String& category, const juce::ValueTree& state)
{
    dir.createDirectory();
    juce::XmlElement root(rootTag);
    root.setAttribute("name", name);
    root.setAttribute("description", desc);
    root.setAttribute("category", category);
    if (auto xml = state.createXml())
        root.addChildElement(xml.release());
    const auto file = dir.getChildFile(safePresetFileName(name, "Preset") + "." + ext);
    root.writeTo(file);
    return file;
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

// The categories used by the full presets in `dir` (first-seen order, no duplicates).
inline juce::StringArray readPresetCategories(const juce::File& dir, const juce::String& ext,
                                              const juce::String& rootTag)
{
    juce::StringArray cats;
    for (const auto& f : listPresetFiles(dir, ext))
        if (auto xml = juce::XmlDocument::parse(f))
            if (xml->hasTagName(rootTag))
            {
                const auto c = xml->getStringAttribute("category");
                if (c.isNotEmpty()) cats.addIfNotAlreadyThere(c);
            }
    return cats;
}

// Add a <p id v> row to `root` for every parameter whose id starts with `prefix`.
inline void writeLayerParams(juce::XmlElement& root, juce::AudioProcessor& proc, const juce::String& prefix)
{
    for (auto* p : proc.getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*>(p))
        {
            const auto id = rp->getParameterID();
            if (id.startsWith(prefix))
            {
                auto* e = root.createNewChildElement("p");
                e->setAttribute("id", id.substring(prefix.length()));
                e->setAttribute("v", (double) rp->getValue());
            }
        }
}

// Apply a layer preset's <p id v> rows to the parameters `prefix` + id. Parameters the
// file doesn't mention go back to their defaults, so an older preset loads cleanly.
inline void applyLayerParams(const juce::ValueTree& tree, juce::AudioProcessorValueTreeState& apvts,
                             const juce::String& prefix)
{
    for (auto* p : apvts.processor.getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*>(p))
            if (rp->getParameterID().startsWith(prefix))
                rp->setValueNotifyingHost(rp->getDefaultValue());

    for (int i = 0; i < tree.getNumChildren(); ++i)
    {
        const auto row = tree.getChild(i);
        if (! row.hasType("p")) continue;
        if (auto* rp = apvts.getParameter(prefix + row.getProperty("id").toString()))
            rp->setValueNotifyingHost((float) (double) row.getProperty("v"));
    }
}

} // namespace mu_pp
