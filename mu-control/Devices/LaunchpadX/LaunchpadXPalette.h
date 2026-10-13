#pragma once

#include <array>
#include <cstdint>

// The Launchpad X fixed palette, only used when RGB lighting is not available. Entries are the manual's picture
// (an illustration, not a measurement of the LEDs) except 5, 21 and 45, which were confirmed by eye on the
// device. The dim scale is not applied here: a dim colour arrives already scaled and maps to a darker entry.
namespace mu_control::launchpadx
{

struct PaletteEntry { uint8_t index; uint32_t rgb; };

inline constexpr std::array<PaletteEntry, 20> kPalette {{
    { 0,  0x000000 },
    { 1,  0x6E6E6E },   // dark grey
    { 2,  0xDDDDDD },   // grey
    { 3,  0xFFFFFF },   // white
    { 5,  0xFF6259 },   // red
    { 7,  0xAD433D },   // dark red
    { 9,  0xFFB064 },   // orange
    { 11, 0xB4765D },   // brown-orange
    { 13, 0xFFF14C },   // yellow
    { 19, 0x7F8E4A },   // dark lime
    { 21, 0x4CFF4C },   // green
    { 23, 0x33AA33 },   // dark green
    { 37, 0x65EBFF },   // cyan
    { 39, 0x45A0AE },   // dark cyan
    { 45, 0x5B5DFD },   // blue
    { 47, 0x3E3FAC },   // dark blue
    { 53, 0xFF62FF },   // magenta
    { 55, 0xAF43AF },   // dark magenta
    { 57, 0xFF5EC5 },   // rose
    { 75, 0xC7FE6D },   // lime
}};

// A layer palette entry (MuLookAndFeel::layerPalette order) as a hue name and its bright / dim palette entries.
// Mirrors the hue names only; mu-control never reads the UI palette itself.
struct LayerHue { const char* name; uint8_t bright; uint8_t dim; };

inline constexpr std::array<LayerHue, 8> kLayerHues {{
    { "red",     5,  7  },
    { "cyan",    37, 39 },
    { "orange",  9,  11 },
    { "magenta", 53, 55 },
    { "lime",    75, 19 },
    { "rose",    57, 55 },
    { "silver",  2,  1  },
    { "copper",  11, 11 },
}};

// The palette index whose colour is nearest (squared RGB distance); 0 for black.
inline uint8_t nearestPaletteIndex(uint32_t rgb) noexcept
{
    if ((rgb & 0xFFFFFFu) == 0) return 0;
    const auto ch = [](uint32_t v, int shift) { return (long) ((v >> shift) & 0xFFu); };
    uint8_t best = 0;
    long    bestDist = -1;
    for (const auto& e : kPalette)
    {
        if (e.index == 0) continue;
        const long dr = ch(rgb, 16) - ch(e.rgb, 16), dg = ch(rgb, 8) - ch(e.rgb, 8), db = ch(rgb, 0) - ch(e.rgb, 0);
        const long d = dr * dr + dg * dg + db * db;
        if (bestDist < 0 || d < bestDist) { bestDist = d; best = e.index; }
    }
    return best;
}

} // namespace mu_control::launchpadx
