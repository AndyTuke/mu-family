#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "Control/LiveState.h"
#include <functional>
#include <memory>

// The interim way to choose a Performance (docs/design-ui-family.md 12.6): the standard async file
// chooser on the product's Performances folder, then LiveState::loadPerformance. No list and no editing;
// the chooser overlay replaces it. Header-only so the load step is unit-tested without a display.
namespace mu_ui::perf
{

// The folder performances live in, under the product's content root (beside Presets).
inline juce::File performancesDir(const juce::File& contentDir) { return contentDir.getChildFile("Performances"); }

// Reads `file` and hands it to the live state. Returns an empty string on success, else the reason.
inline juce::String loadPerformanceFile(mu_core::LiveState& live, const juce::File& file)
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
    return live.loadPerformance(std::move(loaded), file) ? juce::String() : juce::String("leave Performance mode first");
}

// Opens the chooser; `holder` keeps it alive until it answers. `status` gets the status-bar line.
inline void launchPerformanceChooser(std::unique_ptr<juce::FileChooser>& holder, const juce::File& contentDir,
                                     mu_core::LiveState& live,
                                     std::function<void(const juce::String& name, const juce::String& value)> status)
{
    const auto dir = performancesDir(contentDir);
    dir.createDirectory();   // so the chooser opens there on a fresh install
    holder = std::make_unique<juce::FileChooser>("Choose a performance", dir, "*.muPerformance");
    holder->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [&live, status = std::move(status)](const juce::FileChooser& chooser)
        {
            const auto file = chooser.getResult();
            if (file == juce::File()) return;   // cancelled
            const auto problem = loadPerformanceFile(live, file);
            if (status)
                status("Performance", problem.isEmpty() ? file.getFileNameWithoutExtension() + " loaded"
                                                        : "could not load " + file.getFileName() + ": " + problem);
        });
}

} // namespace mu_ui::perf
