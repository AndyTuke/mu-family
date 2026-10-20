#include "Persistence/TrackLayerTypes.h"
#include "Sequencer/TrackTypes.h"            // mu-On's layer library: the five Track type descriptions
#include "UI/Components/MuLookAndFeel.h"     // the colour ID
#include <limits>

#if MU_TRONIC_WITH_UI
 #include "Plugin/ProcessorBase.h"
 #include "UI/TrackContent.h"                // mu-On's layer library: the content, its host interface
 #include "UI/Components/LayerDiscVisual.h"   // mu-core: the disc picture
#endif

namespace mu_tronic
{

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

        // A mu-Tronic Track is locked to its type's engine and this content serves one type: the engine never changes
        // underneath it (the content's timer would otherwise rebuild when a slot swaps to a sibling Track type).
        mu_on::EngineType getTrackEngine(int) const noexcept override { return engine; }
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
        return std::make_unique<mu_ui::LayerDiscVisual>(colour, juce::String(mu_on::engineName(engineOfSlot(host, slot))).substring(0, 1));
    }
}
#endif

mu_core::LayerTypeInfo trackLayerType(mu_core::LayerTypeId type)
{
    auto info = mu_on::makeTrackTypeInfo(mu_on::engineOfType(type));
    info.appColourId = (int) MuLookAndFeel::appYellow;   // the main area is painted in mu-On's yellow
    info.typeTag = "On";
   #if MU_TRONIC_WITH_UI
    info.createContent = &createTrackContent;
    info.createMiniVisual = &createTrackMiniVisual;
   #endif
    return info;
}

} // namespace mu_tronic
