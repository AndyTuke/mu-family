#include "SettingsOverlay.h"
#include "Plugin/PluginProcessor.h"

namespace mu_toni
{

SettingsOverlay::SettingsOverlay(PluginProcessor& p)
    : StandardSettingsOverlay(p)
{
    addProgramChangeSection("Layer Presets", "Full Presets");   // Ch 1-4 → layers, Ch 9 → full

    // Tony's in-joke line — the μ-Toni namesake. Verbatim; "appagator" is the joke, never
    // "corrected" to arpeggiator. No heading: it sits below all the groups.
    addSection(Where::Group, { {}, kRowH, {}, [](juce::Graphics& g, const Rows& r)
    {
        g.setColour(MuLookAndFeel::colour(MuLookAndFeel::mutedText));
        g.setFont(juce::Font(juce::FontOptions{}.withHeight(mu_ui::sf(12.0f)).withStyle("Italic")));
        g.drawText(juce::String(juce::CharPointer_UTF8("\xe2\x80\x9cIts an appagator, is it a powerful tool?\xe2\x80\x9d")),
                   r.area, juce::Justification::centredLeft, false);
    } });
}

} // namespace mu_toni
