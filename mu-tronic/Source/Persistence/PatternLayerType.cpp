#include "Persistence/PatternLayerType.h"
#include "Sequencer/PatternType.h"           // mu-Tant's layer library: the Pattern type and its services
#include "UI/PatternHost.h"                  // mu-Tant's layer library: what the layer and its content read from their host
#include "Plugin/ProcessorBase.h"
#include "UI/Components/MuLookAndFeel.h"     // the colour ID

#if MU_TRONIC_WITH_UI
 #include "UI/PatternContent.h"              // mu-Tant's layer library: the content
 #include "Modulation/MuTantModDest.h"       // the Pattern's modulation destinations
 #include "UI/Components/LayerDiscVisual.h"   // mu-core: the disc picture
#endif

namespace mu_tronic
{

namespace
{
    // The layer's own node carries the Note mode: mu-Tant keeps it as an app setting and never saves it, so the type's
    // own extras stay as they are and this host adds the property (design 2.1.10 point 5).
    constexpr const char* kNoteModeProperty = "noteMode";

    // A Pattern in a mu-Tronic slot: Note mode's gate acts on the layer's own output (there is no final-mix gate to
    // leave it to), the Note mode choice is per layer and saved in the layer node, and a staged layer preset's user
    // wavetables are loaded before its boundary.
    class TronicPattern : public mu_tant::Pattern
    {
    public:
        TronicPattern() { gatesOwnOutput = true; }

        // 0 Free, 1 Note. Message thread; the audio thread sees the change on its next block.
        void setNoteModeSetting(int mode) noexcept
        {
            noteModeSetting = juce::jlimit(0, 1, mode);
            setNoteMode(noteModeSetting);
        }
        int getNoteModeSetting() const noexcept { return noteModeSetting; }

        void writeExtras(juce::ValueTree& node) const override
        {
            Pattern::writeExtras(node);
            node.setProperty(kNoteModeProperty, noteModeSetting, nullptr);
        }

        void applyExtras(const juce::ValueTree& node) override
        {
            Pattern::applyExtras(node);
            // A tree without the property (a preset saved by mu-Tant) leaves the layer's own choice alone.
            if (node.hasProperty(kNoteModeProperty))
                setNoteModeSetting((int) node.getProperty(kNoteModeProperty));
        }

        void preloadStaged(const juce::ValueTree& staged) override
        {
            // Resolving a path loads the table into the shared bank when it is not there yet, so the commit at the
            // boundary finds it and does no file work.
            if (host.ptr == nullptr) return;
            for (const char* id : { "o1WtPath", "o2WtPath" })
                if (const auto path = staged.getProperty(id).toString(); path.isNotEmpty())
                    host.ptr->resolvePatternWavetable(path);
        }

    private:
        int noteModeSetting = 0;
    };

    // What the Pattern layer and its content read from the processor, plus the wavetable bank and the slots' engines
    // the Pattern layers of the processor share (the type's services, built with the first Pattern). Message thread
    // except where the layer's contract says otherwise. Nothing here holds a pointer into a layer: every call looks the
    // slot's layer up afresh.
    class PatternSlotHost final : public mu_tant::PatternServices, public mu_tant::PatternHost
    {
    public:
        explicit PatternSlotHost(ProcessorBase& p) : proc(p) {}

        juce::AudioProcessorValueTreeState& getPatternParameters() noexcept override { return proc.apvts; }

        mu_tant::Pattern& getPatternLayer(int layer) noexcept override
        {
            // The content's constructor binds slot 0 before the editor rebinds it to the shown slot, and a content kept
            // for its type can be asked about a slot that has since become another type. Such a slot answers with an
            // unbound stand-in Pattern (never rendered), so the content never reads another layer type as a Pattern.
            if (auto* l = proc.getLayer(layer); l != nullptr && l->typeId() == (int) mu_core::LayerTypeId::Pattern)
                return static_cast<mu_tant::Pattern&>(*l);
            if (standIn == nullptr) standIn = std::make_unique<mu_tant::Pattern>();   // built on first need, not per content
            return *standIn;
        }

        float getPatternModLive(int layer, int snap) const noexcept override
        {
            // A slot whose layer is not (or no longer) a Pattern has no live values.
            const auto* l = proc.getLayer(layer);
            if (l == nullptr || l->typeId() != (int) mu_core::LayerTypeId::Pattern) return 0.0f;
            return static_cast<const mu_tant::Pattern*>(l)->getModLive(snap);
        }

        const std::atomic<float>* getPatternInsertGainReduction(int layer) const override { return proc.getInsertGR(layer); }
        bool   isPatternTransportPlaying() const noexcept override { return proc.isInternalPlaying(); }
        double getPatternTransportBeat() const noexcept override   { return proc.getInternalBeatPos(); }

        // The wavetable browser opens mu-Tant's own folder, shared with it and never copied.
        juce::File getPatternWavetablesDir() const override
        {
            const auto* info = proc.getLayerTypes().find(mu_core::LayerTypeId::Pattern);
            return info != nullptr ? proc.getContentDir().getSiblingFile(info->contentFolderName).getChildFile("Wavetables") : juce::File();
        }

        juce::String getPatternWavetablePath(int layer, int oscIndex) const override
        {
            const auto* l = proc.getLayer(layer);
            if (l == nullptr || l->typeId() != (int) mu_core::LayerTypeId::Pattern) return {};
            const auto& p = *static_cast<const mu_tant::Pattern*>(l);
            return oscIndex == 0 ? p.osc1UserPath : p.osc2UserPath;
        }

        bool isPatternWavetableMissing(int layer, int oscIndex) const override
        {
            const auto path = getPatternWavetablePath(layer, oscIndex);
            return path.isNotEmpty() && ! juce::File(path).existsAsFile();
        }

        void loadPatternWavetable(int layer, int oscIndex, const juce::File& file) override
        {
            auto* l = proc.getLayer(layer);
            if (l == nullptr || l->typeId() != (int) mu_core::LayerTypeId::Pattern || ! file.existsAsFile()) return;
            const int idx = bankIndexFor(file);
            if (idx < 0)
            {
                if (proc.onLoadError) proc.onLoadError("Could not load wavetable \"" + file.getFileName() + "\"");
                return;
            }
            auto& p = static_cast<mu_tant::Pattern&>(*l);
            if (oscIndex == 0) { p.osc1UserPath = file.getFullPathName(); p.osc1UserIndex.store(idx); }
            else               { p.osc2UserPath = file.getFullPathName(); p.osc2UserIndex.store(idx); }
        }

        void clearPatternWavetable(int layer, int oscIndex) override
        {
            auto* l = proc.getLayer(layer);
            if (l == nullptr || l->typeId() != (int) mu_core::LayerTypeId::Pattern) return;
            auto& p = static_cast<mu_tant::Pattern&>(*l);
            if (oscIndex == 0) { p.osc1UserPath.clear(); p.osc1UserIndex.store(-1); }
            else               { p.osc2UserPath.clear(); p.osc2UserIndex.store(-1); }
        }

        int resolvePatternWavetable(const juce::String& path) override { return resolveWavetable(path); }

        mu_tant::VoiceEngine*        getPatternEngine(int slot) noexcept override { return findEngine(slot); }
        const juce::CriticalSection* getPatternBankLock() const noexcept override { return &bankLock; }

    private:
        ProcessorBase& proc;
        std::unique_ptr<mu_tant::Pattern> standIn;   // see getPatternLayer
    };

    std::unique_ptr<Layer> createTronicPattern(int slot, mu_core::LayerTypeId)
    {
        auto p = std::make_unique<TronicPattern>();
        p->insert->setSlotIndex(slot);   // the insert allocates its delay lines here, off the audio thread
        return p;
    }

    std::unique_ptr<mu_core::LayerServices> createPatternServices(ProcessorBase& host)
    {
        return std::make_unique<PatternSlotHost>(host);
    }

    // The slot's engine for this Pattern exists before it binds (its bindParams takes it without allocating or locking), and the
    // Pattern reads the shared bank through the services' adapter.
    void attachPatternServices(Layer& layer, mu_core::LayerServices& services, int slot)
    {
        auto& host = static_cast<PatternSlotHost&>(services);
        host.engineForNewLayer(slot);
        static_cast<TronicPattern&>(layer).host.ptr = &host;
    }

   #if MU_TRONIC_WITH_UI
    // mu-Tant's Pattern content over the services' adapter, with the Pattern's modulation destinations: the one content
    // instance the editor keeps for every Pattern slot.
    class PatternSlotContent : public mu_tant::PatternContent
    {
    public:
        PatternSlotContent(ProcessorBase& p, PatternSlotHost& h) : mu_tant::PatternContent(p, h), slotHost(h)
        {
            // Note mode is a per-layer choice here (mu-Tant keeps it as an app setting and shows no selector).
            setNoteModeControl([&p](int slot) { return getPatternNoteMode(p, slot); },
                               [&p](int slot, int mode) { setPatternNoteMode(p, slot, mode); });
        }

        const ModDestProvider* getModDestProvider() const noexcept override { return &destinations; }

        // A stepped source may now drive an oscillator's semitone (or no longer): the audio thread reads the flags.
        void modulatorsChanged(int slot) override { slotHost.getPatternLayer(slot).refreshPitchQuantFlags(); }

    private:
        mu_tant::PatternHost& slotHost;
        ModDestProvider destinations = mu_tant::makeModDestProvider();
    };

    std::unique_ptr<LayerContent> createPatternContent(ProcessorBase& host, int slot)
    {
        const auto* info = host.getLayerTypes().find(mu_core::LayerTypeId::Pattern);
        auto* services = info != nullptr ? host.ensureLayerServices(*info) : nullptr;
        jassert(services != nullptr);   // registered with services: the type's first layer built them already
        if (services == nullptr) return nullptr;
        auto content = std::make_unique<PatternSlotContent>(host, static_cast<PatternSlotHost&>(*services));
        content->setLayer(slot);
        return content;
    }

    std::unique_ptr<juce::Component> createPatternMiniVisual(ProcessorBase& host, int slot, std::function<void()>)
    {
        const auto colour = MuLookAndFeel::layerPalette[(size_t) (host.getLayerColourIndex(slot) % MuLookAndFeel::kLayerPaletteSize)];
        // The sidebar item's own label already carries the slot number ("Pattern 3"), so the disc shows the type's initial.
        return std::make_unique<mu_ui::LayerDiscVisual>(colour, host.getLayerTypes().discInitial(mu_core::LayerTypeId::Pattern));
    }
   #endif
}

mu_core::LayerTypeInfo patternLayerType()
{
    auto info = mu_tant::patternLayerTypeInfo();
    info.create = &createTronicPattern;
    info.createServices = &createPatternServices;
    info.attachServices = &attachPatternServices;
    info.appColourId = (int) MuLookAndFeel::appGreen;   // the main area is painted in mu-Tant's green
    info.typeTag = "\xCE\xBC" "-Tant";   // UTF-8: the product name with the real mu
    info.takesNotes = true;   // in Note mode a key starts the drone (the setting is kept in Free mode too)
   #if MU_TRONIC_WITH_UI
    info.createContent = &createPatternContent;
    info.createMiniVisual = &createPatternMiniVisual;
   #endif
    return info;
}

bool setPatternNoteMode(ProcessorBase& host, int slot, int mode)
{
    auto* l = host.getLayer(slot);
    if (l == nullptr || l->typeId() != (int) mu_core::LayerTypeId::Pattern) return false;
    static_cast<TronicPattern*>(l)->setNoteModeSetting(mode);
    return true;
}

int getPatternNoteMode(const ProcessorBase& host, int slot)
{
    const auto* l = host.getLayer(slot);
    if (l == nullptr || l->typeId() != (int) mu_core::LayerTypeId::Pattern) return 0;
    return static_cast<const TronicPattern*>(l)->getNoteModeSetting();
}

} // namespace mu_tronic
