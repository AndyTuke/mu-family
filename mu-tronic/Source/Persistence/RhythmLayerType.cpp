#include "Persistence/RhythmLayerType.h"
#include "Sequencer/RhythmType.h"            // mu-Clid's layer library: the Rhythm type, its layer and services
#include "UI/RhythmHost.h"                   // mu-Clid's layer library: what the layer's content reads from its host
#include "Plugin/ProcessorBase.h"
#include "Persistence/PresetFiles.h"         // mu_pp: the atomic file write, the folder listing
#include "UI/Components/MuLookAndFeel.h"     // the colour ID

#if MU_TRONIC_WITH_UI
 #include "UI/RhythmContent.h"               // mu-Clid's layer library: the content
 #include "Modulation/MuClidModDest.h"       // the Rhythm's modulation destinations
 #include "UI/Components/LayerDiscVisual.h"   // mu-core: the disc picture
#endif

namespace mu_tronic
{

namespace
{
    // What the Rhythm layers of the processor share, and what their content reads from the processor: the type's services
    // (the sample library, the layers' mixer strips, the applying of a changed parameter row) and the RhythmHost over
    // ProcessorBase. Message thread except where the layer's contract says otherwise. Nothing here holds a pointer into a
    // layer: every call looks the slot's layer up afresh.
    class RhythmSlotHost final : public mu_clid::RhythmSlotServices, public mu_clid::RhythmHost
    {
    public:
        explicit RhythmSlotHost(ProcessorBase& p) : mu_clid::RhythmSlotServices(p.apvts), proc(p) {}

        // ── RhythmSlotServices ───────────────────────────────────────────────
        mu_clid::SlotRhythm* rhythmAt(int slot) const noexcept override
        {
            auto* l = proc.getLayer(slot);
            return l != nullptr && l->typeId() == (int) mu_core::LayerTypeId::Rhythm ? static_cast<mu_clid::SlotRhythm*>(l) : nullptr;
        }
        MixerEngine::ChannelState* stripFor(int slot) noexcept override
        {
            return slot >= 0 && slot < (int) proc.mixerEngine.channels.size() ? &proc.mixerEngine.channels[(size_t) slot] : nullptr;
        }
        void requestWork() noexcept override { proc.requestLayerServicesWork(); }
        // The samples are mu-Clid's own folder, shared with it and never copied (like the presets).
        juce::File getSamplesDir() const override { return typeFolder("Samples"); }
        void reportLoadError(const juce::String& message) override { if (proc.onLoadError) proc.onLoadError(message); }
        juce::PropertiesFile* getSampleSettingsFile() const noexcept override { return proc.getAppSettingsFile(); }

        // ── RhythmHost ───────────────────────────────────────────────────────
        juce::AudioProcessorValueTreeState& getRhythmParameters() noexcept override { return proc.apvts; }
        int getRhythmCount() const noexcept override { return mu_limits::kMaxLayers; }

        mu_clid::Rhythm& getRhythmLayer(int layer) noexcept override
        {
            // The content's constructor binds slot 0 before the editor rebinds it to the shown slot, and a content kept for
            // its type can be asked about a slot that has since become another type. Such a slot answers with an unbound
            // stand-in Rhythm (never rendered), so the content never reads another layer type as a Rhythm.
            if (auto* r = rhythmAt(layer)) return *r;
            return standIn();
        }
        const mu_clid::Rhythm& getRhythmLayer(int layer) const noexcept override
        {
            if (auto* r = rhythmAt(layer)) return *r;
            return standIn();
        }

        bool isRhythmTransportPlaying() const noexcept override { return transportPlaying.load(std::memory_order_relaxed); }
        const std::atomic<bool>& getRhythmTransportFlag() const noexcept override { return transportPlaying; }
        mu_clid::RhythmPlayState* getRhythmPlayState(int layer) noexcept override
        {
            auto* r = rhythmAt(layer);
            return r != nullptr ? &r->playState : nullptr;
        }
        float getRhythmModLive(int layer, int snap) const noexcept override
        {
            // A slot whose layer is not (or no longer) a Rhythm has no live values.
            const auto* r = rhythmAt(layer);
            return r != nullptr && snap >= 0 && snap < mu_clid::kSnapCount ? r->modSnapshot[(size_t) snap].load() : 0.0f;
        }
        mu_clid::EuclidOverrides getRhythmEuclidOverrides(int layer) const noexcept override
        {
            const auto* r = rhythmAt(layer);
            return r != nullptr ? r->modulatedEuclidOverrides() : mu_clid::EuclidOverrides {};
        }
        const std::atomic<float>* getRhythmInsertGainReduction(int layer) override
        {
            const auto* r = rhythmAt(layer);
            return r != nullptr ? r->insertGainReduction() : nullptr;
        }

        bool isRhythmParamsLoading() const noexcept override { return bulkChange.load(std::memory_order_acquire); }
        std::atomic<bool>& getRhythmParamsLoadingFlag() noexcept override { return bulkChange; }
        void syncRhythmFromParameters(int layer) override
        {
            if (auto* r = rhythmAt(layer)) r->syncFromParameters(proc.apvts, layer);
        }

        void renameRhythmLayer(int layer, const juce::String& newName) override
        {
            if (auto* l = proc.getLayer(layer)) l->name = newName.toStdString();
        }
        void resetRhythmLayer(int layer) override { proc.resetLayer(layer); }
        int  getRhythmClipCount(int layer) const override { return proc.clipCount(layer); }

        // The header's preset list and load are the shell's in a slot host (typed, through ProcessorBase); Save is the
        // content's own mu-Clid card, which writes the file here exactly as mu-Clid does (category, description and the
        // embedded sample included), so a file saved in a slot opens complete in mu-Clid on another machine.
        juce::File getRhythmPresetsDir() const override { return typeFolder("Rhythms"); }
        void saveRhythmPresetFile(int layer, const juce::File& dest, bool embedSample, const juce::String& category,
                                  const juce::String& description) override
        {
            const auto* r = rhythmAt(layer);
            if (r == nullptr) return;

            // A sample decoded from an embedded preset lives in a temp folder that does not survive the session:
            // such a file must carry the audio itself.
            const auto samplePath = samples.path(layer);
            if (mu_clid::rhythm_preset::isEmbeddedSampleTempPath(samplePath) && ! embedSample)
            {
                embedSample = true;
                reportLoadError("Sample originated from embedded data; saving with embed forced on.");
            }
            const auto tree = mu_clid::rhythm_preset::buildLayerTree(*r, proc.apvts, layer, samplePath, getSamplesDir(), embedSample,
                                                                     true, dest.getFileNameWithoutExtension(), category, description);
            mu_pp::replaceFileAtomically(dest, tree.toXmlString(), proc.onLoadError);

            // The layer is now that saved preset (the "_default" template is not a preset the user named).
            if (! dest.getFileName().startsWith("_"))
                proc.noteLayerPresetLabel(layer, dest.getFileNameWithoutExtension());
        }
        void stageRhythmPresetFile(int layer, const juce::File& file) override { proc.loadLayerPreset(layer, file); }
        // The file the layer came from: the preset it was loaded or saved as, when that file is still in the folder.
        juce::File getRhythmLoadedPresetFile(int layer) const override
        {
            const auto folder = typeFolder("Rhythms");
            if (folder == juce::File()) return {};
            const auto file = folder.getChildFile(proc.clipLabel(layer) + ".muRhythm");
            return file.existsAsFile() ? file : juce::File();
        }
        // A slot's layer keeps no name until the user types one, so the card offers the name the slot shows ("Rhythm 2").
        juce::String getRhythmSaveName(int layer) const override { return proc.getLayerName(layer); }
        // The categories in use are the ones the folder's files carry: nothing is kept apart from the files.
        juce::StringArray getRhythmPresetCategories() const override
        {
            juce::StringArray cats;
            for (const auto& e : mu_pp::listPresetsByCategory(typeFolder("Rhythms"), "muRhythm"))
                if (e.category != "Uncategorised") cats.addIfNotAlreadyThere(e.category);
            cats.sort(false);
            return cats;
        }
        void addRhythmPresetCategory(const juce::String&) override {}

        mu_clid::SampleLibrary& getRhythmSamples() noexcept override { return samples; }
        juce::File getRhythmSamplesDir() const override { return getSamplesDir(); }

    private:
        juce::File typeFolder(const char* name) const
        {
            const auto* info = proc.getLayerTypes().find(mu_core::LayerTypeId::Rhythm);
            if (info == nullptr || proc.getContentDir() == juce::File()) return {};
            return proc.getContentDir().getSiblingFile(info->contentFolderName).getChildFile(name);
        }
        mu_clid::Rhythm& standIn() const noexcept
        {
            if (standInRhythm == nullptr) standInRhythm = std::make_unique<mu_clid::Rhythm>();   // built on first need, not per content
            return *standInRhythm;
        }

        ProcessorBase& proc;
        mutable std::unique_ptr<mu_clid::Rhythm> standInRhythm;   // see getRhythmLayer
    };

    std::unique_ptr<mu_core::LayerServices> createRhythmServices(ProcessorBase& host)
    {
        return std::make_unique<RhythmSlotHost>(host);
    }

   #if MU_TRONIC_WITH_UI
    // mu-Clid's Rhythm content over the services' adapter, with the Rhythm's modulation destinations: the one content
    // instance the editor keeps for every Rhythm slot.
    class RhythmSlotContent : public mu_clid::RhythmContent
    {
    public:
        RhythmSlotContent(ProcessorBase& p, RhythmSlotHost& h) : mu_clid::RhythmContent(p, h) {}

        const ModDestProvider* getModDestProvider() const noexcept override { return &destinations; }

        // The effect name labels the Amp send knob and, through the provider, the effect-send modulation target.
        void setEffectSendName(const juce::String& name) override
        {
            effectName = name;
            RhythmContent::setEffectSendName(name);
        }

    private:
        juce::String effectName;
        ModDestProvider destinations = mu_clid::makeModDestProvider([this] { return effectName; });
    };

    std::unique_ptr<LayerContent> createRhythmContent(ProcessorBase& host, int slot)
    {
        const auto* info = host.getLayerTypes().find(mu_core::LayerTypeId::Rhythm);
        auto* services = info != nullptr ? host.ensureLayerServices(*info) : nullptr;
        jassert(services != nullptr);   // registered with services: the type's first layer built them already
        if (services == nullptr) return nullptr;
        auto content = std::make_unique<RhythmSlotContent>(host, static_cast<RhythmSlotHost&>(*services));
        content->setLayer(slot);
        return content;
    }

    std::unique_ptr<juce::Component> createRhythmMiniVisual(ProcessorBase& host, int slot, std::function<void()>)
    {
        const auto colour = MuLookAndFeel::layerPalette[(size_t) (host.getLayerColourIndex(slot) % MuLookAndFeel::kLayerPaletteSize)];
        // The sidebar item's own label already carries the slot number ("Rhythm 3"), so the disc shows the type's initial.
        return std::make_unique<mu_ui::LayerDiscVisual>(colour, host.getLayerTypes().discInitial(mu_core::LayerTypeId::Rhythm));
    }
   #endif
}

mu_core::LayerTypeInfo rhythmLayerType()
{
    auto info = mu_clid::rhythmLayerTypeInfo();
    info.createServices = &createRhythmServices;
    info.appColourId = (int) MuLookAndFeel::appPurple;   // the main area is painted in mu-Clid's purple
    info.typeTag = "Clid";
   #if MU_TRONIC_WITH_UI
    info.createContent = &createRhythmContent;
    info.createMiniVisual = &createRhythmMiniVisual;
   #endif
    return info;
}

bool isRhythmMidiOutput(const ProcessorBase& host, int slot)
{
    const auto* layer = host.getLayer(slot);
    if (layer == nullptr || layer->typeId() != (int) mu_core::LayerTypeId::Rhythm) return false;
    const auto* raw = host.apvts.getRawParameterValue("r" + juce::String(slot) + "_outMode");
    return raw != nullptr && raw->load() > 0.5f;
}

void clearUnresolvedSample(ProcessorBase& host, int slot)
{
    const auto* layer = host.getLayer(slot);
    if (layer == nullptr || layer->typeId() != (int) mu_core::LayerTypeId::Rhythm) return;
    auto* services = static_cast<mu_clid::RhythmSlotServices*>(host.findLayerServices(mu_core::LayerTypeId::Rhythm));
    if (services != nullptr && services->samples.isSampleMissing(slot))
        services->samples.clearPath(slot);
}

} // namespace mu_tronic
