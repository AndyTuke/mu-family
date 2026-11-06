#include "Persistence/PhraseLayerType.h"
#include "Sequencer/PhraseType.h"            // mu-Tilate's layer library: the Phrase type
#include "Sequencer/PhraseSlotHost.h"        // mu-Tilate's layer library: loop loading, auditions, what the content reads
#include "Plugin/ProcessorLayerHostView.h"   // mu-core: the layer host view over a processor
#include "UI/Components/MuLookAndFeel.h"     // the colour ID

#if MU_TRONIC_WITH_UI
 #include "UI/PhraseContent.h"               // mu-Tilate's layer library: the content
 #include "UI/PhraseModDest.h"               // the Phrase's modulation destinations
 #include "UI/Components/LayerDiscVisual.h"   // mu-core: the disc picture
#endif

namespace mu_tronic
{

namespace
{
    // What the Phrase layers of the processor share: the host adapter over ProcessorBase (a slot answers only when it holds a
    // Phrase), with the samples taken from mu-Tilate's own folder, shared with it and never copied. A loop that a preset, a
    // session or a scene names is loaded by a short timer: the layer is built and published by the processor, so there is no
    // moment in the layer's own code at which its slot can take the loop.
    class PhraseSlotServices final : public mu_core::LayerServices, private mu_core::ProcessorLayerHostView,
                                    public mu_tilate::PhraseSlotHost, private juce::Timer
    {
    public:
        explicit PhraseSlotServices(ProcessorBase& p)
            : mu_core::ProcessorLayerHostView(p), mu_tilate::PhraseSlotHost(static_cast<mu_core::ProcessorLayerHostView&>(*this)), proc(p)
        { startTimer(150); }
        ~PhraseSlotServices() override { stopTimer(); }

        juce::File getSamplesDir() const override
        {
            const auto* info = proc.getLayerTypes().find(mu_core::LayerTypeId::Phrase);
            auto dir = info != nullptr ? proc.getContentDir().getSiblingFile(info->contentFolderName).getChildFile("Samples") : juce::File();
            if (dir != juce::File()) dir.createDirectory();
            return dir;
        }

    private:
        void timerCallback() override { reloadMissingLoops(); }

        ProcessorBase& proc;
    };

    std::unique_ptr<mu_core::LayerServices> createPhraseServices(ProcessorBase& host)
    {
        return std::make_unique<PhraseSlotServices>(host);
    }

   #if MU_TRONIC_WITH_UI
    // mu-Tilate's Phrase content over the services' adapter, with the Phrase's modulation destinations: the one content
    // instance the editor keeps for every Phrase slot.
    class PhraseSlotContent : public mu_tilate::PhraseContent
    {
    public:
        PhraseSlotContent(ProcessorBase& p, PhraseSlotServices& s) : mu_tilate::PhraseContent(p, s) {}

        const ModDestProvider* getModDestProvider() const noexcept override { return &destinations; }

    private:
        ModDestProvider destinations = mu_tilate::makeModDestProvider();
    };

    std::unique_ptr<LayerContent> createPhraseContent(ProcessorBase& host, int slot)
    {
        const auto* info = host.getLayerTypes().find(mu_core::LayerTypeId::Phrase);
        auto* services = info != nullptr ? host.ensureLayerServices(*info) : nullptr;
        jassert(services != nullptr);   // registered with services
        if (services == nullptr) return nullptr;
        auto content = std::make_unique<PhraseSlotContent>(host, static_cast<PhraseSlotServices&>(*services));
        content->setLayer(slot);
        return content;
    }

    std::unique_ptr<juce::Component> createPhraseMiniVisual(ProcessorBase& host, int slot, std::function<void()>)
    {
        const auto colour = MuLookAndFeel::layerPalette[(size_t) (host.getLayerColourIndex(slot) % MuLookAndFeel::kLayerPaletteSize)];
        // The sidebar item's own label already carries the slot number ("Phrase 3"), so the disc shows the type's initial.
        return std::make_unique<mu_ui::LayerDiscVisual>(colour, host.getLayerTypes().discInitial(mu_core::LayerTypeId::Phrase));
    }
   #endif
}

mu_core::LayerTypeInfo phraseLayerType()
{
    auto info = mu_tilate::phraseLayerTypeInfo();
    info.createServices = &createPhraseServices;
    info.appColourId = (int) MuLookAndFeel::appBlue;   // the main area is painted in mu-Tilate's blue
    info.typeTag = "\xCE\xBC" "-Tilate";   // UTF-8: the product name with the real mu
   #if MU_TRONIC_WITH_UI
    info.createContent = &createPhraseContent;
    info.createMiniVisual = &createPhraseMiniVisual;
   #endif
    return info;
}

} // namespace mu_tronic
