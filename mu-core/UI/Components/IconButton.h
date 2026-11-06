#pragma once

#include "UI/Components/MuLookAndFeel.h"
#include "UI/Components/SvgIcon.h"

#include <juce_gui_basics/juce_gui_basics.h>

// A small transport-style button drawn from the family SVG icons (SvgIcon.h): play, stop or repeat, with an optional badge beside the glyph that says which kind of
// play it is (a metronome for "at the tempo", a waveform for "the original"). A toggle
// (setClickingTogglesState) shows its on state in the app accent. Colours come from MuLookAndFeel, so every product matches.
class IconButton : public juce::Button
{
public:
    enum class Glyph { Play, Stop, Loop, Walk };

    enum class Badge { None, Metronome, Wave };

    IconButton(Glyph g, Badge bg = Badge::None) : juce::Button({}), glyph(g), badge(bg) {}

    void paintButton(juce::Graphics& g, bool over, bool down) override
    {
        using LF = MuLookAndFeel;
        const auto b = getLocalBounds().toFloat().reduced(0.5f);
        const bool on = getToggleState();
        const auto accent = LF::appAccent(*this);
        g.setColour(LF::colour(LF::lfoEditorBackground).brighter(down ? 0.25f : over ? 0.12f : 0.0f));
        g.fillRoundedRectangle(b, 3.0f);
        g.setColour((on ? accent : LF::colour(LF::lfoEditorPoint)).withAlpha(on ? 0.95f : 0.5f));
        g.drawRoundedRectangle(b, 3.0f, 1.0f);

        // The glyph sits in a square at the left (centred when there is no caption).
        const float side = std::min(b.getHeight() - 8.0f, 14.0f * (float) mu_ui::s(1));
        auto box = juce::Rectangle<float>(side, side).withCentre({ badge == Badge::None ? b.getCentreX() : b.getX() + 5.0f + side * 0.5f, b.getCentreY() });
        const auto icon = [&] { switch (glyph) { case Glyph::Play: return mu_ui::SvgIconId::Play; case Glyph::Stop: return mu_ui::SvgIconId::Stop;
                                                  case Glyph::Walk: return mu_ui::SvgIconId::Walk; case Glyph::Loop: return mu_ui::SvgIconId::Loop; }
                                return mu_ui::SvgIconId::Play; }();
        mu_ui::drawSvgIcon(g, icon, box, on ? accent : LF::colour(LF::labelText));
        if (badge != Badge::None)
        {
            const auto k = juce::Rectangle<float>(side, side).withCentre({ b.getRight() - 5.0f - side * 0.5f, b.getCentreY() });
            mu_ui::drawSvgIcon(g, badge == Badge::Metronome ? mu_ui::SvgIconId::Metronome : mu_ui::SvgIconId::Wave, k,
                               on ? accent : LF::colour(LF::labelText));
        }
    }

private:
    Glyph glyph;
    Badge badge;
};
