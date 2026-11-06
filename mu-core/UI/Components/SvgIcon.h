#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// The family's vector icons (docs/design-ui-family.md, Visual polish P6): each is a white-filled SVG in mu-core/UI/Assets, embedded as
// binary data, parsed once, and drawn sharp at any size and scale. The colour is applied at draw time from a token (never baked in the
// file), by replacing the white on a cached copy per colour.
namespace mu_ui
{

enum class SvgIconId { Play, Stop, Walk, Loop, Metronome, Wave };

// Draws icon `id` centred in `bounds` (kept in proportion), filled with `colour`. Message thread only.
void drawSvgIcon(juce::Graphics& g, SvgIconId id, juce::Rectangle<float> bounds, juce::Colour colour);

} // namespace mu_ui
