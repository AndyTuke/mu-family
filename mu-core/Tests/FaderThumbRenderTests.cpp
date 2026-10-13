#include <juce_gui_basics/juce_gui_basics.h>
#include "UI/Components/MuLookAndFeel.h"

// Fader painting: renders a slider off-screen and checks the disabled state is drawn whole at the
// shared disabled alpha, with no ring or halo and a balanced transparency layer.
struct FaderThumbRenderTests : juce::UnitTest
{
    FaderThumbRenderTests() : juce::UnitTest("Fader thumb rendering", "mu-core") {}

    static juce::Image snapshot(juce::Slider& s, bool enabled)
    {
        s.setEnabled(enabled);
        return s.createComponentSnapshot(s.getLocalBounds(), false, 1.0f);
    }

    void checkStyle(juce::Slider::SliderStyle style, int w, int h, const char* label)
    {
        MuLookAndFeel laf;
        juce::Slider s(style, juce::Slider::NoTextBox);
        s.setLookAndFeel(&laf);
        s.setRange(0.0, 1.0);
        s.setValue(0.5, juce::dontSendNotification);
        s.setBounds(0, 0, w, h);

        const auto on  = snapshot(s, true);
        const auto off = snapshot(s, false);
        const auto c   = juce::Point<int>(w / 2, h / 2);   // the cap sits at the middle of the travel

        const float aOn  = on .getPixelAt(c.x, c.y).getFloatAlpha();
        const float aOff = off.getPixelAt(c.x, c.y).getFloatAlpha();

        expect(aOn > 0.9f, juce::String(label) + ": enabled cap centre is opaque");
        expectWithinAbsoluteError(aOff, aOn * MuLookAndFeel::kDisabledAlpha, 0.05f);

        // Enabled and disabled renders differ; the drawing must not leak a layer into the next paint.
        bool differ = false;
        for (int y = 0; y < h && ! differ; ++y)
            for (int x = 0; x < w && ! differ; ++x)
                differ = on.getPixelAt(x, y) != off.getPixelAt(x, y);
        expect(differ, juce::String(label) + ": disabled differs from enabled");

        const auto again = snapshot(s, true);
        expect(again.getPixelAt(c.x, c.y) == on.getPixelAt(c.x, c.y), juce::String(label) + ": re-enabling restores the render");
        s.setLookAndFeel(nullptr);
    }

    void runTest() override
    {
        beginTest("vertical fader: disabled draws at the shared disabled alpha");
        checkStyle(juce::Slider::LinearVertical, 24, 120, "vertical");

        beginTest("horizontal fader: disabled draws at the shared disabled alpha");
        checkStyle(juce::Slider::LinearHorizontal, 120, 24, "horizontal");
    }
};

static FaderThumbRenderTests faderThumbRenderTests;
