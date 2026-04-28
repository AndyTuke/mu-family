#include "TimeSelector.h"
#include "MuClidLookAndFeel.h"

const char* TimeSelector::kNoteLabels[kNoteCount]  = { "1", "1/2", "1/4", "1/8", "1/16", "1/32" };
const NoteValue TimeSelector::kNoteValues[kNoteCount] = {
    NoteValue::Whole, NoteValue::Half, NoteValue::Quarter,
    NoteValue::Eighth, NoteValue::Sixteenth, NoteValue::ThirtySecond
};

TimeSelector::TimeSelector()
{
}

void TimeSelector::setSelection(NoteValue nv, NoteMod mod, bool notify)
{
    noteValue = nv;
    noteMod   = mod;
    repaint();
    if (notify && onChange) onChange(noteValue, noteMod);
}

void TimeSelector::resized()
{
    const int w = getWidth(), h = getHeight();
    const int modW  = 36;
    const int noteW = (w - modW * 2 - 4) / kNoteCount;
    const int noteH = h;

    for (int i = 0; i < kNoteCount; ++i)
        noteBounds[i] = { i * noteW, 0, noteW, noteH };

    const int modX = kNoteCount * noteW + 4;
    tripletBounds = { modX,        0, modW, noteH / 2 };
    dottedBounds  = { modX,        noteH / 2, modW, noteH / 2 };
}

void TimeSelector::paint(juce::Graphics& g)
{
    using Id = MuClidLookAndFeel::ColourIds;

    auto drawBtn = [&](juce::Rectangle<int> b, const juce::String& txt, bool active)
    {
        g.setColour(active ? MuClidLookAndFeel::colour(Id::segmentActiveBg)
                           : MuClidLookAndFeel::colour(Id::segmentInactiveBg));
        g.fillRoundedRectangle(b.toFloat(), 2.0f);
        g.setColour(active ? MuClidLookAndFeel::colour(Id::segmentActiveBorder)
                           : MuClidLookAndFeel::colour(Id::segmentInactiveBorder));
        g.drawRoundedRectangle(b.toFloat().reduced(0.5f), 2.0f, 1.0f);
        g.setColour(active ? MuClidLookAndFeel::colour(Id::segmentActiveBorder)
                           : MuClidLookAndFeel::colour(Id::segmentInactiveText));
        g.setFont(juce::Font(10.0f));
        g.drawText(txt, b, juce::Justification::centred, false);
    };

    for (int i = 0; i < kNoteCount; ++i)
        drawBtn(noteBounds[i], kNoteLabels[i], noteValue == kNoteValues[i]);

    drawBtn(tripletBounds, "T", noteMod == NoteMod::Triplet);
    drawBtn(dottedBounds,  ".", noteMod == NoteMod::Dotted);
}

void TimeSelector::mouseDown(const juce::MouseEvent& e)
{
    auto pos = e.getPosition();

    for (int i = 0; i < kNoteCount; ++i)
    {
        if (noteBounds[i].contains(pos))
        {
            setSelection(kNoteValues[i], noteMod, true);
            return;
        }
    }

    if (tripletBounds.contains(pos))
    {
        setSelection(noteValue, noteMod == NoteMod::Triplet ? NoteMod::None : NoteMod::Triplet, true);
        return;
    }
    if (dottedBounds.contains(pos))
    {
        setSelection(noteValue, noteMod == NoteMod::Dotted ? NoteMod::None : NoteMod::Dotted, true);
    }
}
