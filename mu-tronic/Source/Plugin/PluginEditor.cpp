#include "PluginEditor.h"
#include "UI/Components/MuLookAndFeel.h"
#include "Persistence/TrackLayerTypes.h"   // chooseRumbleSource
#include "Persistence/RhythmLayerType.h"   // isRhythmMidiOutput, clearUnresolvedSample

namespace mu_tronic
{

PluginEditor::PluginEditor(PluginProcessor& p)
    : EditorShellBase(p),
      proc(p),
      sidebar(p, "Layer"),
      chooser(p.getLayerTypes()),
      layerPanel(p, "Layer"),
      // The content's modulation arcs hold a layer's matrix: release them before a removal frees it.
      layerEdit(p, sidebar, layerPanel, { [this] { releaseContentArcs(); }, [this](int idx) { layerPanel.setLayer(idx); } }),
      mixerOverlay(p, p.mixerEngine),
      settingsOverlay(p)
{
    setProductIdentity(juce::String(juce::CharPointer_UTF8("\xce\xbc-Tronic")));

    // A filled slot shows its type's own mini-graphic; an Empty slot shows a "+" (the sidebar builds none for it).
    sidebar.createMiniVisual = [this](int slot, std::function<void()> pulse) -> std::unique_ptr<juce::Component>
    {
        const auto* info = proc.getLayerTypes().find(proc.getLayerTypeId(slot));
        if (info == nullptr || info->createMiniVisual == nullptr) return nullptr;
        return info->createMiniVisual(proc, slot, std::move(pulse));
    };
    sidebar.onLayerSelected = [this](int idx) { layerPanel.setLayer(idx); };
    sidebar.refreshItems();

    // Delete empties the slot (nothing shifts) and layers are never added from the sidebar: an Empty slot is the
    // add affordance (the chooser). A drag in the sidebar reorders (the family's, enabled by canReorderLayers); the
    // strips moved with their layers, so the mixer reloads after it.
    layerEdit.setFixedLayers(false);
    sidebar.onAddLayer = nullptr;
    {
        auto reselect = std::move(sidebar.onLayersReordered);
        sidebar.onLayersReordered = [this, reselect](int newSelected)
        {
            if (reselect) reselect(newSelected);
            mixerOverlay.loadFromApvts();
            mixerOverlay.refreshSidechainSources();
        };
    }

    // Names are editable and saved for every layer: a typed name stays, an emptied one returns to the default.
    layerPanel.getHeader().onNameChanged = [this](juce::String name)
    {
        proc.setLayerName(layerPanel.getLayer(), name);
        sidebar.repaintItems();
        mixerOverlay.refreshSidechainSources();
    };

    // Whoever changes a slot's type (the chooser, a state restore, a swap commit): nothing in the UI may point into the
    // old layer once it is retired. The shown content drops its modulation arcs and the modulator panel its layer; the
    // editor rebinds both when it shows the slot again.
    proc.onLayerLeaving = [this](int slot)
    {
        layerPanel.getModulatorPanel().setLayer(nullptr);
        juce::ignoreUnused(slot);
        releaseContentArcs();   // unconditional: cheap, and safe even if a content's binding differs from the shown slot
    };

    // A layer preset landed (a chosen file, a boundary commit): an Empty slot that took the file's type needs its
    // sidebar picture and panel rebuilt, a filled slot rebinds its shown content.
    proc.onLayerPresetCommitted = [this](int slot)
    {
        sidebar.refreshItems();
        mixerOverlay.loadFromApvts();
        if (layerPanel.getLayer() == slot) layerPanel.setLayer(slot);
    };

    layerPanel.setDefaultPresetName([this](int slot) { return proc.getLayerName(slot); });
    layerPanel.onLayerChanged = [this](int idx) { showSlot(idx); };
    // A modulator edit: the shown type refreshes whatever it derives from the layer's modulators.
    layerPanel.getModulatorPanel().onChange = [this]
    {
        if (shownContent != nullptr) shownContent->modulatorsChanged(layerPanel.getLayer());
    };

    chooser.onChoose = [this](mu_core::LayerTypeId type)
    {
        const int slot = layerPanel.getLayer();
        layerPanel.getModulatorPanel().setLayer(nullptr);   // nothing may point into the layer being replaced
        if (proc.createLayerOfType(slot, type))   // from the app's own _default file when it has one
        {
            if (type == mu_core::LayerTypeId::Rumble) chooseRumbleSource(proc, slot);   // a new Rumble listens to something at once
            if (type == mu_core::LayerTypeId::Rhythm) clearUnresolvedSample(proc, slot);   // no "Missing" banner for a file nobody chose
            layerTypeSet(slot);
        }
    };
    // The cards in the design's order (design-ui 4.3): the three products, then mu-On's five.
    chooser.setOrder({ mu_core::LayerTypeId::Rhythm, mu_core::LayerTypeId::Pattern, mu_core::LayerTypeId::Arp,
                       mu_core::LayerTypeId::Kick, mu_core::LayerTypeId::Bass, mu_core::LayerTypeId::Hat,
                       mu_core::LayerTypeId::Snare, mu_core::LayerTypeId::Rumble });

    // A type with its own save card (the Rhythm's) is asked first by the header's Save.
    layerPanel.setOwnSaveCard([this](int) { return shownContent != nullptr && shownContent->openSaveCard(); });

    // The mixer: a Rhythm in MIDI output mode makes no audio, so its strip is drawn inactive; the effect's name follows
    // the mixer to every place that labels the effect send.
    mixerOverlay.isChannelSilent = [this](int slot) { return isRhythmMidiOutput(proc, slot); };
    mixerOverlay.onEffectAlgorithmNameChanged = [this](const juce::String& name)
    {
        if (name == effectSendName) return;   // the mixer reports on every refresh; rebuild the lists only on a change
        effectSendName = name;
        applyEffectSendName();
    };

    // Scenes: eight pads at the sidebar's foot launch clip n on every layer that stores one. The Notes In channel sits in
    // the header for the types that read notes (showSlot decides per slot).
    sidebar.setShowScenes(true);
    layerPanel.getHeader().setNotesInControl([this] { return notesInChannel(); }, [this](int ch) { setNotesInChannel(ch); });

    setMainArea(&sidebar, &layerPanel);
    setMixerOverlay(&mixerOverlay);
    setSettingsOverlay(&settingsOverlay);

    sidebar.setSelectedIndex(0);
    layerPanel.setLayer(0);
    mixerOverlay.loadFromApvts();
    clearPresetDirty();

    // The family metal look, in mu-Tronic's own near-white.
    setMetalStyle(true, MuLookAndFeel::colour(MuLookAndFeel::appIce));
    setScrews(true);

    forwardKnobStatus(layerPanel);

    // The browser's footer offers the product-preset import.
    presetBrowser.onImportProductPreset = [this] { chooseProductPreset(); };
    presetBrowser.refreshFooter();

    // The CPU readout: shown in Performance mode too (the one figure a player needs on stage).
    getStatusBar().setCpuMeterVisible(true);
    startTimerHz(4);
}

PluginEditor::~PluginEditor()
{
    stopTimer();
    layerPanel.setContent(nullptr);   // the panel must not keep a content this editor is about to destroy
    releaseContentArcs();
    layerPanel.getHeader().onNameChanged = nullptr;   // the processor can outlive the editor
    proc.onLayerLeaving = nullptr;
    proc.onLayerPresetCommitted = nullptr;
    presetBrowser.onImportProductPreset = nullptr;
}

void PluginEditor::chooseProductPreset()
{
    importChooser = std::make_unique<juce::FileChooser>("Import a product preset", juce::File(), "*.muClid;*.muTant;*.muToni;*.muOn");
    importChooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                               [this](const juce::FileChooser& fc)
                               {
                                   const auto file = fc.getResult();
                                   if (file != juce::File()) importProductPreset(file);
                               });
}

void PluginEditor::importProductPreset(const juce::File& file)
{
    const auto result = proc.importProductPreset(file);
    if (result.ok)
    {
        showPresetBrowser(false);
        getStatusBar().showParam("Import", result.message);
    }
    else
        getStatusBar().showParam("Import failed", result.message, MuLookAndFeel::colour(MuLookAndFeel::knobLevel));
}

LayerContent* PluginEditor::contentFor(mu_core::LayerTypeId type)
{
    for (auto& entry : contents)
        if (entry.first == type) return entry.second.get();

    const auto* info = proc.getLayerTypes().find(type);
    if (info == nullptr || info->createContent == nullptr) return nullptr;
    auto content = info->createContent(proc, layerPanel.getLayer());
    // A knob hover or change shows on the shell's status bar.
    content->onStatusUpdate = [this](const juce::String& name, const juce::String& value) { getStatusBar().showParam(name, value); };
    // A type with its own save card: the card sits over the whole panel, and a saved file refreshes the preset list.
    content->onPresetFileSaved = [this] { layerPanel.refreshPresetList(); };
    if (auto* card = content->getSaveCardOverlay()) layerPanel.addOverlay(*card);
    // A change the mixer strip's look depends on (a Rhythm's output mode) refreshes the open mixer.
    content->onMixerStateChanged = [this] { if (isMixerVisible()) mixerOverlay.loadFromApvts(); };
    contents.emplace_back(type, std::move(content));
    return contents.back().second.get();
}

void PluginEditor::timerCallback()
{
    getStatusBar().setCpuLoad(proc.getCpuPeak());   // the worst figure of the last second: a plain average hides the spike that clicks
}

int PluginEditor::notesInChannel() const
{
    const auto* raw = proc.apvts.getRawParameterValue("ch" + juce::String(layerPanel.getLayer()) + "_midiIn");
    return raw != nullptr ? juce::roundToInt(raw->load()) : 0;
}

void PluginEditor::setNotesInChannel(int channel)
{
    auto* param = proc.apvts.getParameter("ch" + juce::String(layerPanel.getLayer()) + "_midiIn");
    if (param == nullptr) return;
    param->beginChangeGesture();
    param->setValueNotifyingHost(param->convertTo0to1((float) juce::jlimit(0, 16, channel)));
    param->endChangeGesture();
}

void PluginEditor::applyEffectSendName()
{
    if (shownContent == nullptr) return;
    shownContent->setEffectSendName(effectSendName);
    // The destination lists are rebuilt so a target carries the new name.
    layerPanel.getModulatorPanel().setDestProvider(shownContent->getModDestProvider());
}

void PluginEditor::releaseContentArcs()
{
    for (auto& entry : contents)
        entry.second->releaseModulationArcs();
}

void PluginEditor::showSlot(int idx)
{
    const auto type = proc.getLayerTypeId(idx);
    const bool empty = type == mu_core::LayerTypeId::Empty;
    layerPanel.setLayerEmpty(empty);
    layerPanel.getHeader().setNameEditable(! empty);
    chooser.setSlot(idx);

    // The clip strip and the Notes In channel follow the slot's type: hidden (not disabled) for an Empty slot, for a type
    // with no clip bank (Rumble) and, for Notes In, for a type that does not read notes.
    const auto* typeInfo = proc.getLayerTypes().find(type);
    layerPanel.setClipBankAvailable(typeInfo == nullptr || typeInfo->hasClipBank);
    layerPanel.getHeader().setShowNotesIn(! empty && typeInfo != nullptr && typeInfo->takesNotes);
    layerPanel.getHeader().refreshNotesIn();

    // An Empty slot shows the chooser over the content and modulator bands, in the shell's own colour.
    if (empty)
    {
        shownContent = nullptr;
        MuLookAndFeel::clearAccentOverride(layerPanel);
        layerPanel.getHeader().setTypeTag({});
        layerPanel.getModulatorPanel().setDestProvider(nullptr);
        layerPanel.setContent(&chooser);
        return;
    }

    // A filled slot shows its type's content, one instance per type rebound to the slot, and the main area wears
    // the type's colour (the shell chrome keeps mu-Tronic's own).
    const auto* info = proc.getLayerTypes().find(type);
    shownContent = contentFor(type);
    if (shownContent != nullptr) shownContent->setLayer(idx);
    layerPanel.setContent(shownContent);
    if (shownContent != nullptr) shownContent->setEffectSendName(effectSendName);   // before the destination lists are built
    layerPanel.getModulatorPanel().setDestProvider(shownContent != nullptr ? shownContent->getModDestProvider() : nullptr);
    layerPanel.getHeader().setTypeTag(info != nullptr ? juce::String(info->typeTag) : juce::String());
    if (info != nullptr && info->appColourId != 0)
        MuLookAndFeel::setAccentOverride(layerPanel, MuLookAndFeel::colour((MuLookAndFeel::ColourIds) info->appColourId));
    else
        MuLookAndFeel::clearAccentOverride(layerPanel);
    layerPanel.repaint();
}

void PluginEditor::layerTypeSet(int slot)
{
    sidebar.refreshItems();
    sidebar.setSelectedIndex(slot);
    layerPanel.setLayer(slot);
    mixerOverlay.loadFromApvts();
    getStatusBar().showParam("Layer", "Added " + proc.getLayerName(slot));
}

void PluginEditor::onPresetLoaded(const juce::File&)
{
    layerEdit.refreshAfterPresetLoad();   // rebuild the sidebar (types may have changed), select slot 1, rebind the panel
    mixerOverlay.loadFromApvts();
}

void PluginEditor::onPresetNew()
{
    onPresetLoaded({});
}

juce::AudioProcessorEditor* PluginProcessor::createEditor()
{
    return new PluginEditor(*this);
}

} // namespace mu_tronic
