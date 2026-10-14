// The Launchpad X colour correction: the tuned pad value for each of the 12 known screen colours, dim applied after
// the correction, the generic fallback for any other colour, and the palette form left alone. No hardware.

#include "Devices/LaunchpadX/LaunchpadXColourMap.h"
#include "Devices/LaunchpadX/LaunchpadXDriver.h"
#include "Devices/LaunchpadX/LaunchpadXProtocol.h"
#include "FakeDevicePorts.h"

namespace
{
using namespace mu_control;
using namespace mu_control::test;

int chan(uint32_t rgb, int shift) { return (int) ((rgb >> shift) & 0xFFu); }

uint32_t scaled(uint32_t rgb, float s)
{
    const auto c = [&](int shift) { return (uint32_t) std::min(255.0f, (float) chan(rgb, shift) * s + 0.5f); };
    return (c(16) << 16) | (c(8) << 8) | c(0);
}

int largestGap(uint32_t a, uint32_t b)
{
    int gap = 0;
    for (const int shift : { 16, 8, 0 }) gap = std::max(gap, std::abs(chan(a, shift) - chan(b, shift)));
    return gap;
}

int maxOf(uint32_t rgb) { return std::max({ chan(rgb, 16), chan(rgb, 8), chan(rgb, 0) }); }
}

class LaunchpadXColourMapTests : public juce::UnitTest
{
public:
    LaunchpadXColourMapTests() : juce::UnitTest("Launchpad X colour map", "MuControl") {}

    void runTest() override
    {
        // The pad RGB each known screen colour gives (the table tuned by eye on the real pad), from the probe's swatch.
        struct Expected { uint32_t screen, pad; const char* name; };
        const Expected known[12] = {
            { 0xE5484D, 0xFF0004, "red" },     { 0x2EC4C9, 0x00FFCF, "cyan" },   { 0xF28C28, 0xFF7700, "orange" },
            { 0xD04FC8, 0xFF0084, "magenta" }, { 0xA6D63A, 0x8BFF00, "lime" },   { 0xF07CA6, 0xFF4656, "rose" },
            { 0xC9CCD1, 0xB39A6D, "silver" },  { 0x2B3FD6, 0x0000B4, "navy" },   { 0x7F77DD, 0x2400FF, "app purple" },
            { 0x4ADC8E, 0x50FF00, "app green" }, { 0x3D8BEB, 0x1028FF, "app blue" }, { 0xF2C53D, 0xFFD700, "app yellow" },
        };

        beginTest("each of the 12 known colours maps to its tuned pad colour");
        for (const auto& e : known)
            expect(largestGap(launchpadx::padRgbFor(e.screen), e.pad) <= 1,
                   juce::String(e.name) + ": got " + juce::String::toHexString((int) launchpadx::padRgbFor(e.screen)));

        beginTest("the table rows match the 12 known colours");
        for (int i = 0; i < 12; ++i)
            expectEquals(launchpadx::kKnownColours[i].screenRgb, known[i].screen);

        beginTest("dim is applied after the correction, in pad space");
        for (const auto& e : known)
        {
            const uint32_t bright = launchpadx::padRgbFor(e.screen);
            const uint32_t dim    = launchpadx::padRgbFor(scaled(e.screen, 0.20f));   // what the model sends for a dim pad
            expect(largestGap(dim, scaled(bright, 0.20f)) <= 4, juce::String(e.name) + ": dim is 0.20 of the corrected colour");
            expect(maxOf(dim) < maxOf(bright), juce::String(e.name) + ": dim is lower than bright");
        }

        beginTest("an unknown colour falls back to a sane correction and is monotone in brightness");
        {
            const uint32_t unknown = 0x806020;
            uint32_t last = 0;
            for (const float s : { 0.1f, 0.25f, 0.5f, 0.75f, 1.0f })
            {
                const uint32_t pad = launchpadx::padRgbFor(scaled(unknown, s));
                expect(pad != 0, "not dark at " + juce::String(s));
                expect(maxOf(pad) >= maxOf(last), "monotone at " + juce::String(s));
                for (const int shift : { 16, 8, 0 }) expect(chan(pad, shift) >= chan(last, shift), "per channel at " + juce::String(s));
                last = pad;
            }
            expect(chan(last, 16) > chan(last, 8) && chan(last, 8) > chan(last, 0), "keeps the hue order red > green > blue");
            expectEquals(launchpadx::padRgbFor(0), (uint32_t) 0);
            expect(launchpadx::padRgbFor(0x808080) != 0, "a plain grey is not lost");
        }

        beginTest("the palette form is not colour-corrected");
        {
            FakeDevicePorts ports;
            LaunchpadXDriver d(false);
            expect(d.open(ports));
            ports.sent.clear();
            d.setLeds({ { { ControlKind::Pad, 0, 7 }, 0xE5484D, LedMode::Static } });
            const auto raw = launchpadx::lightingMessages({ { launchpadx::indexOf({ ControlKind::Pad, 0, 7 }).value(), LedMode::Static, 0xE5484D } }, false);
            expectEquals((int) ports.sent.size(), 1);
            if (ports.sent.size() == 1 && raw.size() == 1) expect(ports.sent[0] == raw[0]);
        }
    }
};

static LaunchpadXColourMapTests launchpadXColourMapTests;
