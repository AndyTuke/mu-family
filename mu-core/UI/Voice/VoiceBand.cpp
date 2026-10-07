#include "VoiceBand.h"

void VoiceBand::setSections(juce::Component& pitch, juce::Component& filter, juce::Component& amp,
                            InsertSubsection& effects, std::array<juce::Component*, kSendCols> sends)
{
    pitchSec = &pitch; filterSec = &filter; ampSec = &amp; effectsSec = &effects;
    sendKnobs = sends;
    for (auto* c : { pitchSec, filterSec, ampSec, static_cast<juce::Component*>(effectsSec) })
        addAndMakeVisible(c);
    // The sends sit in the Effects box's top row, added after it so they're on top of it;
    // the insert dropdown takes the space to their left.
    for (auto* k : sendKnobs)
        if (k != nullptr) addAndMakeVisible(k);
    effectsSec->setAlgoWidth(kSendsX);
    resized();
}

void VoiceBand::resized()
{
    if (pitchSec == nullptr) return;
    using mu_ui::s;
    constexpr int labelH = LF::kVoiceLabelH;
    constexpr int subH   = LF::kVoiceSubH;

    pitchSec  ->setBounds(s(kPitchX),   s(labelH), s(kPitchW),   s(subH));
    filterSec ->setBounds(s(kFilterX),  s(labelH), s(kFilterW),  s(subH));
    ampSec    ->setBounds(s(kAmpX),     s(labelH), s(kAmpW),     s(subH));
    effectsSec->setBounds(s(kEffectsX), s(labelH), s(kEffectsW), s(subH));

    // FX sends: right-aligned in the Effects box's top row.
    int col = 0;
    for (auto* k : sendKnobs)
    {
        if (k != nullptr)
            k->setBounds(s(kEffectsX + kSendsX + col * kSendColW), s(labelH), s(kSendColW), s(LF::kKnobSize2H));
        ++col;
    }
}

void VoiceBand::paint(juce::Graphics& g)
{
    using mu_ui::s;
    constexpr int labelH = LF::kVoiceLabelH;
    const int h = getHeight();

    if (LF::isMetal(*this))
    {
        // Metal: each section in its own raised box, its name plate just above it.
        if (pitchSec != nullptr)
            LF::drawSections(g, *this, { { pitchSec->getBounds(), "PITCH" }, { filterSec->getBounds(), "FILTER" },
                                         { ampSec->getBounds(), "AMP" }, { effectsSec->getBounds(), "EFFECTS" } },
                             LF::appAccent(*this));
        return;
    }

    // Flat: thin dividers between the sections, a name plate centred over each.
    g.setColour(LF::colour(LF::segmentInactiveBorder));
    const float inset = mu_ui::sf(7.0f);
    for (int x : { kFilterX, kAmpX, kEffectsX })
    {
        const float dx = (float) (s(x) - s(LF::kVoiceDivW) / 2);
        g.drawLine(dx, inset, dx, (float) h - inset, 0.5f);
    }
    auto plate = [&](const char* name, int x, int w)
    { LF::drawCentredNamePlate(g, { (float) s(x), 0.0f, (float) s(w), (float) s(labelH) }, name); };
    plate("PITCH",   kPitchX,   kPitchW);
    plate("FILTER",  kFilterX,  kFilterW);
    plate("AMP",     kAmpX,     kAmpW);
    plate("EFFECTS", kEffectsX, kEffectsW);
}

void VoiceBandSection::place(juce::Component& c, int row, int col, int span, bool dropdown)
{
    cells.push_back({ &c, row, col, span, dropdown });
    addAndMakeVisible(c);
    resized();
}

void VoiceBandSection::resized()
{
    using mu_ui::s;
    constexpr int rowH = MuLookAndFeel::kKnobSize2H;
    constexpr int row2 = rowH + MuLookAndFeel::kVoiceGap;
    constexpr int dg   = MuLookAndFeel::kDropdownEdgeGap;

    // Place each control in its cell: knobs fill the cell, dropdowns sit centred at half height.
    for (const auto& c : cells)
    {
        const int x = c.col * colWidth, y = c.row == 0 ? 0 : row2, w = c.span * colWidth;
        if (c.dropdown)
            c.comp->setBounds(s(x + dg), s(y + rowH / 4), s(w - 2 * dg), s(rowH / 2));
        else
            c.comp->setBounds(s(x), s(y), s(w), s(rowH));
    }
}
