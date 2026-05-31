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
      mixerOverlay(p, p.mixerEngine)
{
    // Product chrome on the shared overlays.
    getAboutPanel().setProductInfo(
        juce::String(juce::CharPointer_UTF8("\xce\xbc-Toni")),
        juce::StringArray {
            juce::String(juce::CharPointer_UTF8("JUCE \xe2\x80\x94 Proprietary (JUCE 7 license)")),
            juce::String(juce::CharPointer_UTF8("Signalsmith Reverb \xe2\x80\x94 MIT")),
            juce::String(juce::CharPointer_UTF8("clap-juce-extensions \xe2\x80\x94 MIT")),
        });
    getTransportBar().setLogoText(juce::String(juce::CharPointer_UTF8("\xce\xbc-Toni")));

    // Per-layer mini-graphic for the shared sidebar (placeholder until the engine
    // defines a real visual). Reorder/add stay unwired in the scaffold — the layer
    // set is fixed until the engine lands.
    sidebar.createMiniVisual = [&p](int i) -> std::unique_ptr<juce::Component>
    {
        const auto col = MuLookAndFeel::channelPalette[
            (size_t) (p.getChannelColourIndex(i) % MuLookAndFeel::kChannelPaletteSize)];
        return std::make_unique<LayerGlyph>(col, i);
    };
    sidebar.onChannelSelected = [this](int idx) { enginePanel.setLayer(idx); };
    sidebar.refreshItems();

    // Main area (sidebar + blank engine panel) + shared mixer overlay.
    setMainArea(&sidebar, &enginePanel);
    setMixerOverlay(&mixerOverlay);

    // Forward mixer status updates to the shared StatusBar.
    mixerOverlay.onStatusUpdate = [this](const juce::String& name,
                                         const juce::String& val,
                                         juce::Colour col)
    {
        getStatusBar().showParam(name, val, col);
    };

    sidebar.setSelectedIndex(0);
    enginePanel.setLayer(0);
    mixerOverlay.loadFromAPVTS();
    clearPresetDirty();
}

} // namespace mu_toni
