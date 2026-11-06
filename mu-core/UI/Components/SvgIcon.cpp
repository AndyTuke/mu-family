#include "UI/Components/SvgIcon.h"

#include "MuAssetData.h"

#include <map>
#include <memory>

namespace mu_ui
{

namespace
{
    struct Source { const char* data; int size; };

    Source sourceFor(SvgIconId id)
    {
        switch (id)
        {
            case SvgIconId::Play:      return { MuAssetData::play_svg,      MuAssetData::play_svgSize };
            case SvgIconId::Stop:      return { MuAssetData::stop_svg,      MuAssetData::stop_svgSize };
            case SvgIconId::Walk:      return { MuAssetData::walk_svg,      MuAssetData::walk_svgSize };
            case SvgIconId::Loop:      return { MuAssetData::loop_svg,      MuAssetData::loop_svgSize };
            case SvgIconId::Metronome: return { MuAssetData::metronome_svg, MuAssetData::metronome_svgSize };
            case SvgIconId::Wave:      return { MuAssetData::wave_svg,      MuAssetData::wave_svgSize };
        }
        return { nullptr, 0 };
    }

    // The parsed drawables by icon and tint. Heap-allocated and never destroyed, like the other UI caches, and bounded: a product
    // tints an icon with a handful of colours.
    std::map<std::pair<int, juce::uint32>, std::unique_ptr<juce::Drawable>>& cache()
    {
        static auto* const c = new std::map<std::pair<int, juce::uint32>, std::unique_ptr<juce::Drawable>>();
        return *c;
    }
}

void drawSvgIcon(juce::Graphics& g, SvgIconId id, juce::Rectangle<float> bounds, juce::Colour colour)
{
    auto& c = cache();
    const auto key = std::make_pair((int) id, colour.getARGB());
    auto it = c.find(key);
    if (it == c.end())
    {
        if (c.size() >= 128) c.clear();
        const auto src = sourceFor(id);
        std::unique_ptr<juce::Drawable> d(juce::Drawable::createFromImageData(src.data, (size_t) src.size));
        if (d == nullptr) return;
        d->replaceColour(juce::Colours::white, colour);
        it = c.emplace(key, std::move(d)).first;
    }
    it->second->drawWithin(g, bounds, juce::RectanglePlacement::centred, 1.0f);
}

} // namespace mu_ui
