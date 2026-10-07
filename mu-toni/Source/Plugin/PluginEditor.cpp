#include "PluginEditor.h"
#include "UI/Components/MuLookAndFeel.h"

namespace mu_toni
{

namespace
{
    // Placeholder per-layer mini-graphic for the shared sidebar — a coloured disc
    // with the layer number. The real product visual (engine/sequencer glyph +
    // animation) is injected here later via createMiniVisual, same as mu-clid's
    // RhythmCircle / mu-tant's voice glyph.
    class LayerGlyph : public juce::Component
    {
    public:
        LayerGlyph(juce::Colour c, int index) : colour(c), idx(index) {}

        void paint(juce::Graphics& g) override
        {
            auto r = getLocalBounds().toFloat().reduced(2.0f);
            g.setColour(colour.withAlpha(0.16f));
            g.fillEllipse(r);
            g.setColour(colour);
            g.drawEllipse(r, 1.5f);
            g.setFont(juce::Font(juce::FontOptions(r.getHeight() * 0.42f, juce::Font::bold)));
            g.drawText(juce::String(idx + 1), getLocalBounds(), juce::Justification::centred, false);
        }

    private:
        juce::Colour colour;
        int idx;
    };
}

PluginEditor::PluginEditor(PluginProcessor& p)
    : EditorShellBase(p),
      proc(p),
      sidebar(p, "Layer"),
      enginePanel(p),
      mixerOverlay(p, p.mixerEngine),
      settingsOverlay(p)
{
    // Product chrome on the shared overlays.
    setProductIdentity(juce::String(juce::CharPointer_UTF8("\xce\xbc-Toni")));

    // Per-layer mini-graphic for the shared sidebar (placeholder until a real arp
    // visual is designed). Reorder/add stay unwired — the layer set is fixed.
    sidebar.createMiniVisual = [&p](int i) -> std::unique_ptr<juce::Component>
    {
        const auto col = MuLookAndFeel::channelPalette[
            (size_t) (p.getChannelColourIndex(i) % MuLookAndFeel::kChannelPaletteSize)];
        return std::make_unique<LayerGlyph>(col, i);
    };
    sidebar.onChannelSelected = [this](int idx) { enginePanel.setLayer(idx); };
    sidebar.refreshItems();

    // Engine-panel knob hovers/changes → shared StatusBar.
    enginePanel.onStatusUpdate = [this](const juce::String& name, const juce::String& val)
    {
        getStatusBar().showParam(name, val, MuLookAndFeel::colour(MuLookAndFeel::knobEuclidean));
    };

    // Main area (sidebar + blank engine panel) + shared mixer overlay.
    setMainArea(&sidebar, &enginePanel);
    setMixerOverlay(&mixerOverlay);

    // Settings page (master vol + UI size + BPM + standalone MIDI Clock) behind the
    // gear button — registering it reveals the gear (hidden when null).
    setSettingsOverlay(&settingsOverlay);

    // A program change loaded a layer preset → refresh that layer if it's on screen.
    proc.onLayerPresetLoaded = [this](int layer)
    {
        if (enginePanel.getLayer() == layer) enginePanel.setLayer(layer);
    };

    // Forward mixer status updates to the shared StatusBar.

    sidebar.setSelectedIndex(0);
    enginePanel.setLayer(0);
    mixerOverlay.loadFromAPVTS();
    clearPresetDirty();

    // The family metal look.
    setMetalStyle(true);
}

PluginEditor::~PluginEditor()
{
    proc.onLayerPresetLoaded = nullptr;   // the processor can outlive the editor
}

void PluginEditor::onPresetLoaded(const juce::File&)
{
    sidebar.refreshItems();
    enginePanel.setLayer(enginePanel.getLayer());   // rebind knobs + modulators to the loaded state
    mixerOverlay.loadFromAPVTS();
}

void PluginEditor::onPresetNew()
{
    onPresetLoaded({});
}

} // namespace mu_toni
