#include "PluginEditor.h"
#include "UI/Components/MuLookAndFeel.h"

namespace mu_on
{

namespace
{
    // Per-channel mini-graphic for the shared sidebar: a coloured disc with the
    // instrument's initial. Replaced by a richer engine/step glyph in a later pass.
    class ChannelGlyph : public juce::Component
    {
    public:
        ChannelGlyph(juce::Colour c, juce::String initial) : colour(c), label(std::move(initial)) {}

        void paint(juce::Graphics& g) override
        {
            auto r = getLocalBounds().toFloat().reduced(2.0f);
            g.setColour(colour.withAlpha(0.16f));
            g.fillEllipse(r);
            g.setColour(colour);
            g.drawEllipse(r, 1.5f);
            g.setFont(juce::Font(juce::FontOptions(r.getHeight() * 0.42f, juce::Font::bold)));
            g.drawText(label, getLocalBounds(), juce::Justification::centred, false);
        }

    private:
        juce::Colour colour;
        juce::String label;
    };
}

PluginEditor::PluginEditor(PluginProcessor& p)
    : EditorShellBase(p),
      proc(p),
      sidebar(p, "Track"),
      groovePanel(p),
      mixerOverlay(p, p.mixerEngine)
{
    getAboutPanel().setProductInfo(
        juce::String(juce::CharPointer_UTF8("\xce\xbc-On")),
        juce::StringArray {
            juce::String(juce::CharPointer_UTF8("JUCE \xe2\x80\x94 Proprietary (JUCE 7 license)")),
            juce::String(juce::CharPointer_UTF8("Signalsmith Reverb \xe2\x80\x94 MIT")),
            juce::String(juce::CharPointer_UTF8("clap-juce-extensions \xe2\x80\x94 MIT")),
        });
    getTransportBar().setLogoText(juce::String(juce::CharPointer_UTF8("\xce\xbc-On")));

    // Per-channel sidebar glyph (Kick/Bass/Hat/Snare initial in the lane colour).
    sidebar.createMiniVisual = [&p](int i) -> std::unique_ptr<juce::Component>
    {
        const auto col = MuLookAndFeel::channelPalette[
            (size_t) (p.getChannelColourIndex(i) % MuLookAndFeel::kChannelPaletteSize)];
        const juce::String name = p.getChannelName(i);
        return std::make_unique<ChannelGlyph>(col, name.substring(0, 1));
    };
    sidebar.onChannelSelected = [this](int idx) { groovePanel.setChannel(idx); };
    // Pulse a lane in the sidebar when the sequencer fires it (polled ~5 Hz; the grid's
    // own 30 Hz playhead is the primary timing feedback).
    sidebar.onAnimationTick = [this]
    {
        for (int ch = 0; ch < kNumChannels; ++ch)
        {
            const int c = proc.triggerCount(ch);
            if (c != lastTriggers[(size_t) ch]) { lastTriggers[(size_t) ch] = c; sidebar.pulseItem(ch); }
        }
    };
    sidebar.refreshItems();   // fixed 4 lanes; add/reorder hooks stay null (no add/delete)

    setMainArea(&sidebar, &groovePanel);
    setMixerOverlay(&mixerOverlay);

    mixerOverlay.onStatusUpdate = [this](const juce::String& name,
                                         const juce::String& val,
                                         juce::Colour col)
    {
        getStatusBar().showParam(name, val, col);
    };

    sidebar.setSelectedIndex(0);
    groovePanel.setChannel(0);
    mixerOverlay.loadFromAPVTS();
    clearPresetDirty();
}

} // namespace mu_on
