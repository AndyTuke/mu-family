#pragma once

#include "UI/Components/MuLookAndFeel.h"

#include <juce_gui_basics/juce_gui_basics.h>

// A small transport-style button drawn from a path: play, stop or repeat, with an optional short caption beside the glyph. A toggle
// (setClickingTogglesState) shows its on state in the app accent. Colours come from MuLookAndFeel, so every product matches.
class IconButton : public juce::Button
{
public:
    enum class Glyph { Play, Stop, Loop };

    IconButton(Glyph g, const juce::String& captionText = {}) : juce::Button(captionText), glyph(g), caption(captionText) {}

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
        auto box = juce::Rectangle<float>(side, side).withCentre({ caption.isEmpty() ? b.getCentreX() : b.getX() + 6.0f + side * 0.5f, b.getCentreY() });
        g.setColour(on ? accent : LF::colour(LF::labelText));
        juce::Path p;
        switch (glyph)
        {
            case Glyph::Play: p.addTriangle(box.getX() + side * 0.1f, box.getY(), box.getX() + side * 0.1f, box.getBottom(), box.getRight(), box.getCentreY()); g.fillPath(p); break;
            case Glyph::Stop: g.fillRect(box.reduced(side * 0.1f)); break;
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
        if (caption.isNotEmpty())
        {
            g.setFont(juce::FontOptions((float) mu_ui::s(11)));
            g.drawText(caption, b.withTrimmedLeft(side + 10.0f).withTrimmedRight(3.0f), juce::Justification::centredLeft, true);
        }
    }

private:
    Glyph glyph;
    juce::String caption;
};
