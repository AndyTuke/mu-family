#include "Persistence/TrackLayerTypes.h"
#include "Plugin/ProcessorBase.h"
#include "Sequencer/TrackTypes.h"            // mu-On's layer library: the five Track type descriptions
#include "UI/Components/MuLookAndFeel.h"     // the colour ID
#include <limits>

#if MU_TRONIC_WITH_UI
 #include "UI/TrackContent.h"                // mu-On's layer library: the content, its host interface
 #include "UI/Components/LayerDiscVisual.h"   // mu-core: the disc picture
#endif

namespace mu_tronic
{

namespace
{
    // A slot a Rumble can listen to: a filled slot that is not itself a Rumble (a Rumble renders after every other layer,
    // so another Rumble is not a source).
    bool isRumbleSourceSlot(const ProcessorBase& host, int slot)
    {
        const auto* l = host.getLayer(slot);
        if (l == nullptr) return false;
        const auto type = (mu_core::LayerTypeId) l->typeId();
        return type != mu_core::LayerTypeId::Empty && type != mu_core::LayerTypeId::Rumble;
    }
}

void remapRumbleSources(ProcessorBase& host, const std::function<int(int)>& oldToNew)
{
    mu_on::remapRumbleSources(host.apvts, host.getNumLayers(),
        [&host](int j)
        {
            const auto* l = host.getLayer(j);
            return l != nullptr && (mu_core::LayerTypeId) l->typeId() == mu_core::LayerTypeId::Rumble ? mu_on::EngineType::Rumble
                                                                                                       : mu_on::EngineType::Kick;
        },
        oldToNew,
        [&host](int j) { return isRumbleSourceSlot(host, j); });
}

void chooseRumbleSource(ProcessorBase& host, int slot)
{
    auto* p = host.apvts.getParameter(mu_on::layerParamId(slot, "src"));
    if (p == nullptr) return;
    const int current = juce::roundToInt(p->convertFrom0to1(p->getValue()));
    if (current != slot && isRumbleSourceSlot(host, current)) return;
    for (int j = 0; j < host.getNumLayers(); ++j)
        if (j != slot && isRumbleSourceSlot(host, j))
        {
            p->setValueNotifyingHost(p->convertTo0to1((float) j));
            return;
        }
}

#if MU_TRONIC_WITH_UI
namespace
{
    // What the Track content reads from its host, answered from the processor's slots (message thread). One adapter
    // serves the four types; it is told which engine its content shows, because a slot's type is fixed and the content
    // is kept per type. Nothing here holds a pointer into a layer: every call looks the slot's layer up afresh.
    class TrackSlotHost : public mu_on::TrackHost
    {
    public:
        TrackSlotHost(ProcessorBase& p, mu_on::EngineType e) : proc(p), engine(e)
        {
            standIn.lockEngine(e);
        }

        juce::AudioProcessorValueTreeState& getTrackParameters() noexcept override { return proc.apvts; }

        mu_on::Track& getTrack(int layer) noexcept override
        {
            // The content's constructor binds slot 0 before the editor rebinds it to the shown slot, and a content kept
            // for its type can be asked about a slot that has since become another type. Such a slot answers with an
            // unbound stand-in Track (never rendered), so the content never reads another layer type as a Track.
            if (auto* l = proc.getLayer(layer); l != nullptr && mu_on::isTrackType((mu_core::LayerTypeId) l->typeId()))
                return static_cast<mu_on::Track&>(*l);
            return standIn;
        }

        // A slot that is a Track answers with the engine it runs (a Rumble's Source list reads them); any other slot answers
        // with this content's own engine, so the shown slot's engine never changes underneath the content.
        mu_on::EngineType getTrackEngine(int layer) const noexcept override
        {
            const auto* l = proc.getLayer(layer);
            return l != nullptr && mu_on::isTrackType((mu_core::LayerTypeId) l->typeId())
                       ? mu_on::engineOfType((mu_core::LayerTypeId) l->typeId()) : engine;
        }
        bool canBeRumbleSource(int layer) const override { return isRumbleSourceSlot(proc, layer); }
        void selectTrackEngine(int, mu_on::EngineType) override {}   // the Engine selector is hidden for a locked track

        int          getTrackCount() const override                 { return proc.getNumLayers(); }
        juce::String getTrackName(int layer) const override         { return proc.getLayerName(layer); }
        int          getTrackColourIndex(int layer) const override  { return proc.getLayerColourIndex(layer); }

        float getModLive(int layer, mu_on::EngineType e, int dest) const noexcept override
        {
            // A slot that is not (or no longer) a Track has no live values.
            const auto* l = proc.getLayer(layer);
            if (l == nullptr || ! mu_on::isTrackType((mu_core::LayerTypeId) l->typeId())) return std::numeric_limits<float>::quiet_NaN();
            return static_cast<const mu_on::Track*>(l)->getModLive(e, dest);
        }

        bool   isTrackTransportPlaying() const noexcept override { return proc.isInternalPlaying(); }
        double getTrackTransportBeat() const noexcept override   { return proc.getInternalBeatPos(); }

    private:
        ProcessorBase& proc;
        mu_on::EngineType engine;
        mu_on::Track standIn;   // see getTrack
    };

    // mu-On's Track content with its host adapter and the engine's modulation destinations: the one content instance
    // the editor keeps per Track type. The adapter is the first base so it exists before the content is built over it.
    class TrackSlotContent : private TrackSlotHost, public mu_on::TrackContent
    {
    public:
        TrackSlotContent(ProcessorBase& p, mu_on::EngineType e)
            : TrackSlotHost(p, e), mu_on::TrackContent(p, static_cast<TrackSlotHost&>(*this)), destinations(mu_on::makeModDestProvider(e)) {}

        const ModDestProvider* getModDestProvider() const noexcept override { return &destinations; }

    private:
        ModDestProvider destinations;
    };

    mu_on::EngineType engineOfSlot(ProcessorBase& host, int slot)
    {
        const auto* l = host.getLayer(slot);
        return l != nullptr ? mu_on::engineOfType((mu_core::LayerTypeId) l->typeId()) : mu_on::EngineType::Kick;
    }

    std::unique_ptr<LayerContent> createTrackContent(ProcessorBase& host, int slot)
    {
        auto content = std::make_unique<TrackSlotContent>(host, engineOfSlot(host, slot));
        content->setLayer(slot);
        return content;
    }

    // The engine's initial in the slot's colour (mu-On's own sidebar picture).
    std::unique_ptr<juce::Component> createTrackMiniVisual(ProcessorBase& host, int slot, std::function<void()>)
    {
        const auto colour = MuLookAndFeel::layerPalette[(size_t) (host.getLayerColourIndex(slot) % MuLookAndFeel::kLayerPaletteSize)];
        return std::make_unique<mu_ui::LayerDiscVisual>(colour, host.getLayerTypes().discInitial(mu_on::layerTypeOf(engineOfSlot(host, slot))));
    }
}
#endif

mu_core::LayerTypeInfo trackLayerType(mu_core::LayerTypeId type)
{
    auto info = mu_on::makeTrackTypeInfo(mu_on::engineOfType(type));
    info.appColourId = (int) MuLookAndFeel::appYellow;   // the main area is painted in mu-On's yellow
    info.typeTag = "Î¼-On";   // UTF-8: the product name with the real mu
    info.hasClipBank = info.id != mu_core::LayerTypeId::Rumble;   // Rumble has no step row: it processes another layer's audio
   #if MU_TRONIC_WITH_UI
    info.createContent = &createTrackContent;
    info.createMiniVisual = &createTrackMiniVisual;
   #endif
    return info;
}

} // namespace mu_tronic
