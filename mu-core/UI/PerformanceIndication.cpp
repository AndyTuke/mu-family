#include "PerformanceIndication.h"

namespace mu_ui::perf
{

namespace
{
using mu_pp::PerformanceFile;

// The name an item shows: the cached display name, else the preset file's own name.
juce::String itemName(const PerformanceFile::Item& item)
{
    return item.name.isNotEmpty() ? item.name : juce::File(item.path).getFileNameWithoutExtension();
}
}

Indication buildIndication(const mu_core::LiveView& live, const juce::String& loadedName)
{
    constexpr int n = PerformanceFile::kMaxItems;
    const auto& perf = live.getPerformance();
    const int current = live.getCurrentItem();
    const int queued  = live.getQueuedItem();
    const juce::String of = "/" + juce::String(n);

    Indication out;
    out.lamps.assign((size_t) n, StepIndicator::Lamp::Empty);
    out.hints.assign((size_t) n, {});

    if (perf.count() == 0)
    {
        out.ghost = "No performance";
        out.hint  = { "Performance", "none chosen" };
        return out;
    }

    const juce::String title = "Performance \"" + perf.name + "\"";
    out.hint = { title, juce::String(perf.count()) + " presets" };

    // Phase 1: a lamp and a hint per item.
    for (int i = 0; i < n; ++i)
    {
        const auto& item = perf.items[(size_t) i];
        const juce::String position = "preset " + juce::String(i + 1) + " of " + juce::String(n);
        if (! item.used)
        {
            out.hints[(size_t) i] = { title, position + ": empty" };
            continue;
        }
        const bool playable = live.isItemPlayable(i);
        out.lamps[(size_t) i] = i == current ? StepIndicator::Lamp::Current
                              : i == queued  ? StepIndicator::Lamp::Queued
                              : playable     ? StepIndicator::Lamp::Available
                                             : StepIndicator::Lamp::Fault;
        out.hints[(size_t) i] = playable ? std::make_pair(title, position + ": " + itemName(item))
                                         : std::make_pair("Preset " + juce::String(i + 1) + " missing", itemName(item));
    }

    // Phase 2: the lettering. The position and name of what plays, then the next playable item as a
    // ghost, or the queued one pulsing.
    if (current >= 0)
        out.lit = juce::String(current + 1) + of + " " + itemName(perf.items[(size_t) current]);
    else
        out.lit = "-" + of + (loadedName.isNotEmpty() ? "  " + loadedName : juce::String());

    if (queued >= 0 && queued < n)
    {
        const auto label = juce::String(queued + 1) + " " + itemName(perf.items[(size_t) queued]);
        out.ghost        = "-> " + label;
        out.ghostPulses  = true;
        out.queuedValue  = label + " (at the next swap point)";
    }
    else if (current >= 0)
    {
        for (int i = current + 1; i < n; ++i)
            if (perf.items[(size_t) i].used && live.isItemPlayable(i))
            {
                out.ghost = "> " + itemName(perf.items[(size_t) i]);
                break;
            }
    }
    return out;
}

juce::String layerIndicatorText(int selected, int layerCount)
{
    return "Layer " + juce::String(followedLayer(selected, layerCount) + 1) + "/" + juce::String(juce::jmax(1, layerCount));
}

int followedLayer(int liveSelected, int layerCount) noexcept
{
    return juce::jlimit(0, juce::jmax(0, layerCount - 1), liveSelected);
}

} // namespace mu_ui::perf
