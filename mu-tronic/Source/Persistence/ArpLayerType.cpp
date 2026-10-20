#include "Persistence/ArpLayerType.h"
#include "Sequencer/ArpType.h"               // mu-Toni's layer library: the Arp type
#include "UI/Components/MuLookAndFeel.h"     // the colour ID

#if MU_TRONIC_WITH_UI
 #include "Plugin/ProcessorBase.h"
 #include "UI/ArpContent.h"                  // mu-Toni's layer library: the content, its host interface and picture
 #include "UI/ArpMiniVisual.h"
#endif

namespace mu_tronic
{

#if MU_TRONIC_WITH_UI
namespace
{
    // What the Arp content reads from its host, answered from the processor's slots (message thread). The content
    // is only ever shown for a slot that holds an Arp, and releases its arcs before that layer is replaced.
    class ArpSlotHost : public mu_toni::ArpHost
    {
    public:
        explicit ArpSlotHost(ProcessorBase& p) : proc(p) {}

        juce::AudioProcessorValueTreeState& getArpParameters() noexcept override { return proc.apvts; }

        mu_toni::Arp& getArp(int layer) noexcept override
        {
            // The content's constructor binds slot 0 before the host rebinds it to the shown slot, and that slot may not
            // hold an Arp yet; the reference is only used for the address of its modulation matrix until setLayer runs.
            return static_cast<mu_toni::Arp&>(*proc.getLayer(layer));
        }

        float getModLive(int layer, int dest) const noexcept override
        {
            // A slot whose layer is not (or no longer) an Arp has no live values.
            const auto* l = proc.getLayer(layer);
            if (l == nullptr || l->typeId() != (int) mu_core::LayerTypeId::Arp) return 0.0f;
            return static_cast<const mu_toni::Arp*>(l)->getModLive(dest);
        }

        int getArpColourIndex(int layer) const override { return proc.getLayerColourIndex(layer); }
        const std::atomic<float>* getArpInsertGainReduction(int layer) const override { return proc.getInsertGR(layer); }
        bool   isArpTransportPlaying() const noexcept override { return proc.isInternalPlaying(); }
        double getArpTransportBeat() const noexcept override   { return proc.getInternalBeatPos(); }
        juce::StringArray getWavetableNames() const override   { return mu_wavetable::WavetableBank::factoryTableNames(); }

    private:
        ProcessorBase& proc;
    };

    // mu-Toni's Arp content with its host adapter and modulation destinations: the one content instance the editor
    // keeps for every Arp slot. The adapter is the first base so it exists before the content is built over it.
    class ArpSlotContent : private ArpSlotHost, public mu_toni::ArpContent
    {
    public:
        explicit ArpSlotContent(ProcessorBase& p)
            : ArpSlotHost(p), mu_toni::ArpContent(p, static_cast<ArpSlotHost&>(*this)) {}

        const ModDestProvider* getModDestProvider() const noexcept override { return &destinations; }

    private:
        ModDestProvider destinations = mu_toni::makeModDestProvider();
    };

    std::unique_ptr<LayerContent> createArpContent(ProcessorBase& host, int slot)
    {
        auto content = std::make_unique<ArpSlotContent>(host);
        content->setLayer(slot);
        return content;
    }

    std::unique_ptr<juce::Component> createArpMiniVisual(ProcessorBase& host, int slot, std::function<void()>)
    {
        const auto colour = MuLookAndFeel::layerPalette[(size_t) (host.getLayerColourIndex(slot) % MuLookAndFeel::kLayerPaletteSize)];
        return std::make_unique<mu_toni::ArpMiniVisual>(colour, slot);
    }
}
#endif

mu_core::LayerTypeInfo arpLayerType()
{
    auto info = mu_toni::arpLayerTypeInfo();
    info.appColourId = (int) MuLookAndFeel::appBlue;   // the main area is painted in mu-Toni's blue
    info.typeTag = "Toni";
   #if MU_TRONIC_WITH_UI
    info.createContent = &createArpContent;
    info.createMiniVisual = &createArpMiniVisual;
   #endif
    return info;
}

} // namespace mu_tronic
