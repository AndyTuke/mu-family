#pragma once

#include <juce_core/juce_core.h>
#include <vector>

// What a control surface is and can do, as plain data a driver supplies: grid size, edge buttons, colour and
// blink abilities. The mapping and the model read only this, never a device name, so a new surface is one
// description plus one driver. No behaviour, no device bytes, no product.
namespace mu_control
{

enum class ColourSupport { None, Palette, Rgb };

// Where a group of non-pad buttons sits: the row above the grid, the column beside it, or a lone extra (a logo).
enum class ButtonKind { Top, Side, Special };

struct SurfaceButtonGroup
{
    ButtonKind   kind  = ButtonKind::Top;
    int          count = 0;
    juce::String label;
};

struct SurfaceDescription
{
    juce::String                    id;                   // stable key for saved overrides
    juce::String                    name;                 // for people
    int                             gridWidth  = 0;
    int                             gridHeight = 0;
    std::vector<SurfaceButtonGroup> buttons;
    ColourSupport                   colour     = ColourSupport::None;
    bool                            canFlash   = false;
    bool                            canPulse   = false;
    bool                            hasPressure = false;

    // Total buttons of one kind (0 when the surface has none).
    int buttonCount(ButtonKind k) const noexcept
    {
        int n = 0;
        for (const auto& g : buttons) if (g.kind == k) n += g.count;
        return n;
    }
};

} // namespace mu_control
