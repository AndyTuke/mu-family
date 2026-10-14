#pragma once

#include <algorithm>
#include <cstdlib>
#include <cstdint>

// The Launchpad X's colour correction: turns a colour the surface model sends (the screen's layer and app colours,
// 0xRRGGBB, possibly already scaled dim) into the pad RGB that looks like it. The pad's LEDs are not the screen's:
// they wash out toward white, the green LED is strong and blue-ish, and the blue LED is strong, so a straight copy
// looks pale and wrong. Device-specific, so it lives with the driver and the surface model stays device-independent.
namespace mu_control::launchpadx
{

// One tuning row per known screen colour, tuned by eye on the real pad. grey = how much of the shared grey (the
// smallest channel) is stripped (1 = all), r/g/b = channel trims, level = strength of the strongest channel.
struct PadTune { float grey, r, g, b, level; };

struct KnownColour
{
    uint32_t screenRgb;     // the colour as the screen shows it
    PadTune  tune;
    uint32_t direct;        // non-zero: the pad RGB set by hand (bit 24 marks it), tune ignored
    const char* name;
};

// Layers 1-8 (MuLookAndFeel::layerPalette), the four app accents (purple, green, blue, yellow), then the
// control-surface colours (transport go green, fault red, white). Adjust here.
inline constexpr int kKnownColourCount = 15;
inline constexpr KnownColour kKnownColours[kKnownColourCount] = {
    { 0xE5484D, { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f },    0,          "red" },
    { 0x2EC4C9, { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f },    0,          "cyan" },
    { 0xF28C28, { 1.0f, 1.0f, 1.35f, 1.0f, 1.0f },   0,          "orange" },       // more green so it leans yellow
    { 0xD04FC8, { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f },    0,          "magenta" },
    { 0xA6D63A, { 1.0f, 0.55f, 1.0f, 1.0f, 1.0f },   0,          "lime" },         // less red so it reads green
    { 0xF07CA6, { 0.4f, 1.0f, 1.0f, 1.0f, 1.0f },    0,          "rose" },         // keeps some grey: lighter and pinker than magenta
    { 0xC9CCD1, { 0.0f, 0.70f, 0.85f, 0.75f, 0.70f }, 0,         "silver" },       // a dim white, red eased back so it is not pink
    { 0x2B3FD6, { 1.0f, 1.0f, 1.0f, 1.0f, 0.85f },   0x010000B4, "navy" },         // deep blue, set directly
    { 0x7F77DD, { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f },    0,          "app purple" },
    { 0x4ADC8E, { 1.0f, 1.0f, 1.0f, 0.10f, 1.0f },   0x0150FF00, "app green" },    // some red against the pad's blue-ish green
    { 0x3D8BEB, { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f },    0x011028FF, "app blue" },     // a little green, not so much it is cyan
    { 0xF2C53D, { 1.0f, 1.0f, 1.6f, 1.0f, 1.0f },    0,          "app yellow" },   // more green so it is yellow, not orange
    { 0x3CD070, { 1.0f, 1.0f, 1.0f, 0.10f, 1.0f },   0,          "go green" },     // untuned: blue trimmed hard so the pad's blue-ish green reads pure
    { 0xE24B4A, { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f },    0,          "fault red" },    // untuned: the same correction as the layer red
    { 0xFFFFFF, { 0.0f, 0.70f, 0.85f, 0.75f, 0.70f }, 0x01B39A6D, "white" },       // untuned: the pad value silver gets, which reads as white on the pad
};

// Fixed white balance of the pad's strong green and blue LEDs.
inline constexpr float kPadGreenGain = 0.70f;
inline constexpr float kPadBlueGain  = 0.55f;

// How far (per channel, 0-255, after normalising brightness away) a colour may be from a known one and still count
// as it. Covers the rounding of a dimmed copy.
inline constexpr int kMatchTolerance = 12;

namespace detail
{
    inline int chan(uint32_t rgb, int shift) noexcept { return (int) ((rgb >> shift) & 0xFFu); }
    inline int maxChan(uint32_t rgb) noexcept { return std::max({ chan(rgb, 16), chan(rgb, 8), chan(rgb, 0) }); }

    inline uint32_t pack(float r, float g, float b, float k) noexcept
    {
        const auto q = [k](float v) { return (uint32_t) std::clamp(v * k + 0.5f, 0.0f, 255.0f); };
        return (q(r) << 16) | (q(g) << 8) | q(b);
    }

    // Strip the shared grey, white-balance, then lift so the strongest channel sits at tune.level of full scale.
    inline uint32_t correct(uint32_t rgb, const PadTune& t) noexcept
    {
        float r = (float) chan(rgb, 16), g = (float) chan(rgb, 8), b = (float) chan(rgb, 0);
        const bool isGrey = std::max({ r, g, b }) - std::min({ r, g, b }) < 1.0f;   // nothing but grey: keep it, or it goes dark
        const float grey = isGrey ? 0.0f : t.grey * std::min({ r, g, b });
        r = (r - grey) * t.r; g = (g - grey) * t.g * kPadGreenGain; b = (b - grey) * t.b * kPadBlueGain;
        return pack(r, g, b, t.level * 255.0f / std::max({ r, g, b, 1.0f }));
    }

    inline uint32_t correctedKnown(int i) noexcept
    {
        const auto& k = kKnownColours[i];
        return (k.direct >> 24) != 0 ? (k.direct & 0xFFFFFFu) : correct(k.screenRgb, k.tune);
    }
}

// The pad RGB for a screen colour. A colour that is a known one at any brightness (bright, dim, mid-pulse) gets that
// colour's tuned pad value scaled by the same brightness, so dim is applied after the correction, in pad space. Any
// other colour gets the generic correction at its own brightness.
inline uint32_t padRgbFor(uint32_t rgb) noexcept
{
    const int m = detail::maxChan(rgb);
    if (m == 0) return 0;

    int best = -1, bestDist = kMatchTolerance + 1;
    for (int i = 0; i < kKnownColourCount; ++i)
    {
        const uint32_t ref = kKnownColours[i].screenRgb;
        const int refMax = detail::maxChan(ref);
        int dist = 0;
        for (const int shift : { 16, 8, 0 })
            dist = std::max(dist, std::abs(detail::chan(rgb, shift) * 255 / m - detail::chan(ref, shift) * 255 / refMax));
        if (dist < bestDist) { bestDist = dist; best = i; }
    }

    if (best >= 0)
    {
        const uint32_t pad = detail::correctedKnown(best);
        const float ratio = (float) m / (float) detail::maxChan(kKnownColours[best].screenRgb);
        return detail::pack((float) detail::chan(pad, 16), (float) detail::chan(pad, 8), (float) detail::chan(pad, 0), ratio);
    }
    return detail::correct(rgb, { 1.0f, 1.0f, 1.0f, 1.0f, (float) m / 255.0f });
}

} // namespace mu_control::launchpadx
