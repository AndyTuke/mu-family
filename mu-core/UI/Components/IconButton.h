#pragma once

#include "UI/Components/MuLookAndFeel.h"

#include <juce_gui_basics/juce_gui_basics.h>

// A small transport-style button drawn from paths: play, stop or repeat, with an optional badge beside the glyph that says which kind of
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
        g.setColour(on ? accent : LF::colour(LF::labelText));
        juce::Path p;
        switch (glyph)
        {
            case Glyph::Play: p.addTriangle(box.getX() + side * 0.1f, box.getY(), box.getX() + side * 0.1f, box.getBottom(), box.getRight(), box.getCentreY()); g.fillPath(p); break;
            case Glyph::Stop: g.fillRect(box.reduced(side * 0.1f)); break;
            case Glyph::Walk:
            {
                // A walking figure: head, body, a swinging arm and two striding legs.
                const float t = std::max(1.4f, side * 0.13f);
                const float cx = box.getCentreX(), top = box.getY(), bot = box.getBottom();
                g.fillEllipse(cx - side * 0.1f, top, side * 0.2f, side * 0.2f);
                g.drawLine(cx, top + side * 0.24f, cx - side * 0.04f, top + side * 0.58f, t);                       // body
                g.drawLine(cx - side * 0.02f, top + side * 0.32f, cx + side * 0.26f, top + side * 0.5f, t);        // arm forward
                g.drawLine(cx - side * 0.02f, top + side * 0.32f, cx - side * 0.26f, top + side * 0.46f, t);       // arm back
                g.drawLine(cx - side * 0.04f, top + side * 0.58f, cx + side * 0.2f, bot, t);                       // leg forward
                g.drawLine(cx - side * 0.04f, top + side * 0.58f, cx - side * 0.24f, bot, t);                      // leg back
                break;
            }
            case Glyph::Loop:
            {
                // An open ring with an arrowhead: the repeat sign.
                const auto ring = box.reduced(side * 0.12f);
                p.addCentredArc(ring.getCentreX(), ring.getCentreY(), ring.getWidth() * 0.5f, ring.getHeight() * 0.5f, 0.0f, 0.6f, juce::MathConstants<float>::twoPi - 0.2f, true);
                g.strokePath(p, juce::PathStrokeType(std::max(1.5f, side * 0.14f)));
                const auto tip = p.getPointAlongPath(p.getLength());
                juce::Path head;
                head.addTriangle(tip.x - side * 0.22f, tip.y - side * 0.28f, tip.x + side * 0.28f, tip.y - side * 0.05f, tip.x - side * 0.05f, tip.y + side * 0.3f);
                g.fillPath(head);
                break;
            }
        }
        if (badge != Badge::None)
        {
            const auto k = juce::Rectangle<float>(side, side).withCentre({ b.getRight() - 5.0f - side * 0.5f, b.getCentreY() });
            const float t = std::max(1.2f, side * 0.1f);
            if (badge == Badge::Metronome)
            {
                // A metronome: a tapering body with its pendulum.
                juce::Path body;
                body.startNewSubPath(k.getX() + side * 0.28f, k.getBottom());
                body.lineTo(k.getX() + side * 0.42f, k.getY());
                body.lineTo(k.getX() + side * 0.58f, k.getY());
                body.lineTo(k.getX() + side * 0.72f, k.getBottom());
                body.closeSubPath();
                g.strokePath(body, juce::PathStrokeType(t));
                g.drawLine(k.getCentreX(), k.getBottom() - side * 0.1f, k.getX() + side * 0.72f, k.getY() + side * 0.2f, t);
            }
            else
            {
                // A waveform: bars of rising and falling height.
                static constexpr float heights[] = { 0.3f, 0.7f, 1.0f, 0.55f, 0.85f, 0.35f };
                const float step = side / 6.0f;
                for (int i = 0; i < 6; ++i)
                {
                    const float h = side * heights[i];
                    g.fillRect(k.getX() + step * (float) i + step * 0.2f, k.getCentreY() - h * 0.5f, std::max(1.2f, step * 0.55f), h);
                }
            }
        }
    }

private:
    Glyph glyph;
    Badge badge;
};
