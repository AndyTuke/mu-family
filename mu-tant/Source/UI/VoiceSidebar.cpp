#include "VoiceSidebar.h"
#include "Plugin/PluginProcessor.h"
#include "UI/Components/MuLookAndFeel.h"

#include <cmath>

namespace mu_tant
{

namespace
{
    // Per-voice mini-graphic — placeholder static glyph (a waveform disc in the
    // voice colour). The live per-product animation (osc / gate visualisation)
    // lands later; the surrounding sidebar UX is the shared mu-core code.
    class VoiceGlyph : public juce::Component
    {
    public:
        explicit VoiceGlyph(juce::Colour c) : colour(c) {}

        void paint(juce::Graphics& g) override
        {
            auto r = getLocalBounds().toFloat().reduced(2.0f);
            g.setColour(colour.withAlpha(0.16f));
            g.fillEllipse(r);
            g.setColour(colour);
            g.drawEllipse(r, 1.5f);

            juce::Path p;
            const float midY = r.getCentreY();
            const float amp  = r.getHeight() * 0.22f;
            constexpr int N  = 40;
            for (int i = 0; i <= N; ++i)
            {
                const float x = r.getX() + r.getWidth() * (float) i / (float) N;
                const float y = midY - amp * std::sin((float) i / (float) N
                                                       * juce::MathConstants<float>::twoPi);
                if (i == 0) p.startNewSubPath(x, y);
                else        p.lineTo(x, y);
            }
            g.strokePath(p, juce::PathStrokeType(1.4f));
        }

    private:
        juce::Colour colour;
    };
}

VoiceSidebar::VoiceSidebar(PluginProcessor& p)
    : ChannelSidebar(p, "Voice")
{
    createMiniVisual = [&p](int i) -> std::unique_ptr<juce::Component>
    {
        const auto col = MuLookAndFeel::channelPalette[
            (size_t) (p.getChannelColourIndex(i) % MuLookAndFeel::kChannelPaletteSize)];
        return std::make_unique<VoiceGlyph>(col);
    };
    onSwapChannels = [&p](int a, int b) { p.swapVoices(a, b); };

    refreshItems();
}

} // namespace mu_tant
