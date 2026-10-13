#include "PerformanceFile.h"
#include "Persistence/PresetFiles.h"   // replaceFileAtomically: a gig list must never be left half-written

namespace mu_pp
{

PerformanceFile::LoadResult PerformanceFile::load(const juce::File& file, PerformanceFile& out)
{
    if (! file.existsAsFile())
        return LoadResult::NotFound;
    return parse(file.loadFileAsString(), out);
}

PerformanceFile::LoadResult PerformanceFile::parse(const juce::String& xmlText, PerformanceFile& out)
{
    const auto xml = juce::XmlDocument::parse(xmlText);
    if (xml == nullptr || ! xml->hasTagName("MuPerformance"))
        return LoadResult::Invalid;
    if (xml->getIntAttribute("format", 0) > kFormat)
        return LoadResult::UnknownFormat;   // written by a newer version: do not guess at it

    PerformanceFile result;
    result.name        = xml->getStringAttribute("name");
    result.description = xml->getStringAttribute("description");

    // Read each <Item> into its idx slot; anything that does not fit is counted, not stored.
    for (auto* e : xml->getChildWithTagNameIterator("Item"))
    {
        const int idx = e->getIntAttribute("idx", -1);
        if (idx < 0 || idx >= kMaxItems || result.items[(size_t) idx].used)
        {
            ++result.ignoredItems;
            continue;
        }
        auto& item   = result.items[(size_t) idx];
        item.used    = true;
        item.product = e->getStringAttribute("product");
        item.path    = e->getStringAttribute("path");
        item.name    = e->getStringAttribute("name");
    }

    out = std::move(result);
    return LoadResult::Ok;
}

juce::String PerformanceFile::toXml() const
{
    juce::XmlElement root("MuPerformance");
    root.setAttribute("format", kFormat);
    root.setAttribute("name", name);
    root.setAttribute("description", description);
    for (int i = 0; i < kMaxItems; ++i)
    {
        const auto& item = items[(size_t) i];
        if (! item.used) continue;
        auto* e = root.createNewChildElement("Item");
        e->setAttribute("idx", i);
        e->setAttribute("product", item.product);
        e->setAttribute("path", item.path);
        e->setAttribute("name", item.name);
    }
    return root.toString();
}

bool PerformanceFile::save(const juce::File& file) const
{
    return replaceFileAtomically(file, toXml());
}

juce::String PerformanceFile::storedPath(const juce::File& preset, const juce::File& presetFolder)
{
    if (presetFolder != juce::File() && preset.isAChildOf(presetFolder))
        return preset.getRelativePathFrom(presetFolder).replaceCharacter('\\', '/');
    return preset.getFullPathName();
}

bool PerformanceFile::setItem(int idx, const juce::String& product, const juce::File& preset,
                              const juce::File& presetFolder, const juce::String& displayName)
{
    if (idx < 0 || idx >= kMaxItems)
        return false;
    auto& item   = items[(size_t) idx];
    item.used    = true;
    item.product = product;
    item.path    = storedPath(preset, presetFolder);
    item.name    = displayName;
    return true;
}

void PerformanceFile::clearItem(int idx)
{
    if (idx >= 0 && idx < kMaxItems)
        items[(size_t) idx] = {};
}

juce::File PerformanceFile::resolve(int idx, const juce::File& presetFolder) const
{
    if (idx < 0 || idx >= kMaxItems || ! items[(size_t) idx].used)
        return {};
    const auto& path = items[(size_t) idx].path;
    if (juce::File::isAbsolutePath(path))
        return juce::File(path);
    return presetFolder.getChildFile(path);
}

PerformanceFile::ItemStatus PerformanceFile::status(int idx, const juce::File& presetFolder,
                                                    const juce::String& ownProduct) const
{
    if (idx < 0 || idx >= kMaxItems || ! items[(size_t) idx].used)
        return ItemStatus::Empty;
    if (items[(size_t) idx].product != ownProduct)
        return ItemStatus::OtherProduct;
    return resolve(idx, presetFolder).existsAsFile() ? ItemStatus::Ok : ItemStatus::Missing;
}

int PerformanceFile::count() const noexcept
{
    int n = 0;
    for (const auto& item : items)
        if (item.used) ++n;
    return n;
}

} // namespace mu_pp
