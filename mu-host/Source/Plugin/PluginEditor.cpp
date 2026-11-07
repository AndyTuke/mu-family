#include "PluginEditor.h"
#include "UI/Components/MuLookAndFeel.h"

namespace mu_host
{

PluginEditor::PluginEditor(PluginProcessor& p)
    : mu_tronic::PluginEditor(p, mu_tronic::EditorOptions { true }),
      hostProc(p)
{
    setProductIdentity(juce::String(juce::CharPointer_UTF8("\xce\xbc-Host")));

    // The engines, in the chooser's order; each item names its product.
    engineTypes = { mu_core::LayerTypeId::Rhythm, mu_core::LayerTypeId::Pattern, mu_core::LayerTypeId::Arp,
                    mu_core::LayerTypeId::Kick, mu_core::LayerTypeId::Bass, mu_core::LayerTypeId::Hat,
                    mu_core::LayerTypeId::Snare, mu_core::LayerTypeId::Rumble, mu_core::LayerTypeId::FmPerc,
                    mu_core::LayerTypeId::Chord, mu_core::LayerTypeId::Phrase };
    for (size_t i = 0; i < engineTypes.size(); ++i)
        if (const auto* info = hostProc.getLayerTypes().find(engineTypes[i]))
            engineBox.addItem(juce::String::fromUTF8(info->name)
                                  + (juce::String(info->group).isNotEmpty() ? "  (" + juce::String::fromUTF8(info->group) + ")" : juce::String()),
                              (int) i + 1);
    engineBox.onChange = [this](int id)
    {
        const size_t i = (size_t) (id - 1);
        if (i < engineTypes.size() && hostProc.getLayerTypeId(0) != engineTypes[i])
            chooseType(engineTypes[i]);
    };

    // The drop-down takes the transport bar's loop-section place.
    getTransportBar().setLoopSection(&engineHolder, 190);
    engineBox.setPlaceholderText("Choose an engine");
    engineBox.setLcdStyle(true);   // after it is parented, so the lettering lights in the shell's metal look like the preset box

    // No sidebar: the window is the standard one less the sidebar's width. The shell sets the full width at construction and on a
    // scale change; both are redone here.
    const auto hostSize = [this] { setSize(mu_ui::s(MuLookAndFeel::kWindowWidth - MuLookAndFeel::kSidebarW), mu_ui::s(MuLookAndFeel::kWindowHeight)); };
    hostSize();
    juce::Component::SafePointer<PluginEditor> safeThis(this);
    hostProc.onUiScaleChanged = [safeThis, hostSize](float scale)
    {
        if (auto* self = safeThis.getComponent())
        {
            mu_ui::scale = scale;
            hostSize();
            self->sendLookAndFeelChange();
            self->resized();
            self->repaint();
        }
    };

    // The shell showed slot 0 while it was being built, before this class existed: do its licence and the drop-down now.
    slotShown(0, hostProc.getLayerTypeId(0));
    refresher.tick = [this] { refreshEngineBox(); };
    refresher.startTimerHz(4);
}

PluginEditor::~PluginEditor()
{
    refresher.stopTimer();
}

void PluginEditor::slotShown(int /*slot*/, mu_core::LayerTypeId type)
{
    // Opening an engine runs its product's licence check; the pop-up, the Support link and the activation card are then that
    // product's own (an unlicensed engine shows the polite prompt once per product per session, as in its own app).
    licenseChanged(hostProc.licenseEngine(type));
    refreshEngineBox();
}

void PluginEditor::refreshEngineBox()
{
    // The drop-down shows the slot's current engine (also after a state restore); an Empty slot shows none.
    const auto type = hostProc.getLayerTypeId(0);
    int id = 0;
    for (size_t i = 0; i < engineTypes.size(); ++i)
        if (engineTypes[i] == type) id = (int) i + 1;
    if (engineBox.getSelectedId() != id) engineBox.setSelectedId(id, false);
}

} // namespace mu_host

// The mu-Host editor is made by the processor, which the headless tests build without any editor.
juce::AudioProcessorEditor* mu_host::PluginProcessor::createEditor()
{
    return new PluginEditor(*this);
}
