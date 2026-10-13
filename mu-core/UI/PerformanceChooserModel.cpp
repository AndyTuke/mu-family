#include "PerformanceChooserModel.h"
#include "Persistence/PresetFiles.h"   // mu_pp::safePresetFileName
#include <algorithm>

namespace mu_ui::perf
{

static const char* const kExtension = "muPerformance";

void PerformanceChooserModel::setContext(const juce::File& performancesFolder, const juce::File& presetsFolder,
                              const juce::String& ownProduct)
{
    folder  = performancesFolder;
    presets = presetsFolder;
    product = ownProduct;
}

// ─── The saved performances ──────────────────────────────────────────────────

void PerformanceChooserModel::rescan()
{
    entries.clear();
    if (folder.isDirectory())
        for (const auto& f : folder.findChildFiles(juce::File::findFiles, false, juce::String("*.") + kExtension))
            entries.push_back({ f, f.getFileNameWithoutExtension() });

    std::sort(entries.begin(), entries.end(),
              [](const Entry& a, const Entry& b) { return a.name.compareIgnoreCase(b.name) < 0; });
}

int PerformanceChooserModel::libraryRowOfWorking() const
{
    if (! hasFile()) return -1;
    for (size_t i = 0; i < entries.size(); ++i)
        if (entries[i].file == currentFile) return (int) i;
    return -1;
}

bool PerformanceChooserModel::deleteFile(const juce::File& file)
{
    const bool wasWorking = (file == currentFile);
    if (! file.deleteFile()) return false;
    if (wasWorking) startNew();
    rescan();
    return true;
}

// ─── The working copy ────────────────────────────────────────────────────────

void PerformanceChooserModel::startNew()
{
    current     = {};
    currentFile = juce::File();   // (`= {}` is ambiguous between File's copy and move assignment on GCC)
    dirty       = false;
}

void PerformanceChooserModel::adopt(const mu_pp::PerformanceFile& list, const juce::File& where)
{
    current     = list;
    currentFile = where;
    dirty       = false;
}

juce::String PerformanceChooserModel::open(const juce::File& file)
{
    using Result = mu_pp::PerformanceFile::LoadResult;
    mu_pp::PerformanceFile loaded;
    switch (mu_pp::PerformanceFile::load(file, loaded))
    {
        case Result::Ok:            break;
        case Result::NotFound:      return "file not found";
        case Result::UnknownFormat: return "made by a newer version";
        case Result::Invalid:       return "not a performance file";
    }
    adopt(loaded, file);
    return {};
}

// ─── Items ───────────────────────────────────────────────────────────────────

bool PerformanceChooserModel::isUsed(int row) const noexcept
{
    return row >= 0 && row < kItems && current.items[(size_t) row].used;
}

mu_pp::PerformanceFile::ItemStatus PerformanceChooserModel::status(int row) const
{
    return current.status(row, presets, product);
}

juce::String PerformanceChooserModel::itemName(int row) const
{
    return isUsed(row) ? current.items[(size_t) row].name : juce::String();
}

juce::String PerformanceChooserModel::productLabel(int row) const
{
    using S = mu_pp::PerformanceFile::ItemStatus;
    switch (status(row))
    {
        case S::Empty:        return {};
        case S::Missing:      return "missing";
        case S::Ok:
        case S::OtherProduct: break;
    }
    const auto& id = current.items[(size_t) row].product;
    return id.endsWith("State") ? id.dropLastCharacters(5) : id;
}

bool PerformanceChooserModel::canMoveUp(int row) const noexcept   { return isUsed(row) && row > 0; }
bool PerformanceChooserModel::canMoveDown(int row) const noexcept { return isUsed(row) && isUsed(row + 1); }

bool PerformanceChooserModel::moveUp(int row)
{
    if (! canMoveUp(row)) return false;
    std::swap(current.items[(size_t) row], current.items[(size_t) row - 1]);
    dirty = true;
    return true;
}

bool PerformanceChooserModel::moveDown(int row)
{
    if (! canMoveDown(row)) return false;
    std::swap(current.items[(size_t) row], current.items[(size_t) row + 1]);
    dirty = true;
    return true;
}

bool PerformanceChooserModel::removeItem(int row)
{
    if (! isUsed(row)) return false;
    // Close the gap: every later item moves up one, so the used rows stay together at the top.
    for (int i = row; i < kItems - 1; ++i)
        current.items[(size_t) i] = current.items[(size_t) i + 1];
    current.items[(size_t) kItems - 1] = {};
    dirty = true;
    return true;
}

bool PerformanceChooserModel::setItem(int row, const juce::File& preset)
{
    if (! current.setItem(row, product, preset, presets, preset.getFileNameWithoutExtension()))
        return false;
    dirty = true;
    return true;
}

// ─── Saving and using ────────────────────────────────────────────────────────

juce::File PerformanceChooserModel::fileFor(const juce::String& name) const
{
    return folder.getChildFile(mu_pp::safePresetFileName(name.trim(), "Performance") + "." + kExtension);
}

bool PerformanceChooserModel::nameTaken(const juce::String& name) const
{
    const auto f = fileFor(name);
    return f.existsAsFile() && f != currentFile;
}

bool PerformanceChooserModel::save(const juce::String& name, const juce::String& description)
{
    const auto trimmed = name.trim();
    if (trimmed.isEmpty()) return false;

    const auto target = fileFor(trimmed);
    folder.createDirectory();   // first save on a fresh install

    auto toWrite        = current;
    toWrite.name        = trimmed;
    toWrite.description = description;
    if (! toWrite.save(target)) return false;

    // A rename leaves the old file behind unless it is removed.
    const auto old = currentFile;
    current     = toWrite;
    currentFile = target;
    dirty       = false;
    if (old != juce::File() && old != target) old.deleteFile();
    rescan();
    return true;
}

bool PerformanceChooserModel::use(const std::function<bool(mu_pp::PerformanceFile, const juce::File&)>& loader) const
{
    if (! loader || dirty || ! hasFile() || ! canUse()) return false;
    return loader(current, currentFile);
}

} // namespace mu_ui::perf
