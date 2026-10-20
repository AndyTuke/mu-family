#include "UI/EmptySlotChooser.h"

namespace mu_tronic
{

EmptySlotChooser::EmptySlotChooser(const mu_core::LayerTypeRegistry& registry)
    : types(registry)
{
    rebuild();
}

void EmptySlotChooser::setSlot(int s)
{
    slot = s;
    repaint();
}

// Bands: the first holds every product that offers a single type (its card is named for the product); each product
// with several types is a titled band of its own (its cards are named for the types). Registry order within each.
void EmptySlotChooser::rebuild()
{
    using LF = MuLookAndFeel;
    for (auto& c : cards)
    {
        if (c.add != nullptr) removeChildComponent(c.add.get());
        if (c.disc != nullptr) removeChildComponent(c.disc.get());
    }
    cards.clear();
    bands.clear();

    const auto groups = types.groups();
    bool haveSingles = false;
    for (const auto& group : groups)
        if (types.inGroup(group).size() == 1) haveSingles = true;
    if (haveSingles) bands.push_back({ {}, {}, false });

    for (const auto& group : groups)
    {
        const auto members = types.inGroup(group);
        const bool single = members.size() == 1;
        if (! single) bands.push_back({ group, {}, true });
        const int bandIndex = single ? 0 : (int) bands.size() - 1;

        for (const auto* info : members)
        {
            Card card;
            card.id         = info->id;
            card.band       = bandIndex;
            card.name       = juce::String::fromUTF8(info->name);
            card.plate      = single ? group : card.name;
            card.descriptor = juce::String::fromUTF8(info->descriptor);
            card.colour     = info->appColourId != 0 ? LF::colour((LF::ColourIds) info->appColourId) : LF::colour(LF::appIce);
            card.disc       = std::make_unique<mu_ui::LayerDiscVisual>(LF::colour(LF::headingText), card.name.substring(0, 1));
            card.disc->setInterceptsMouseClicks(false, false);   // a click on the picture is a click on the card
            addAndMakeVisible(*card.disc);
            card.add        = std::make_unique<juce::TextButton>("Add");
            card.add->setTooltip("Add " + card.name + " to this slot");
            const auto id = info->id;
            card.add->onClick = [this, id] { if (onChoose) onChoose(id); };
            addAndMakeVisible(*card.add);
            cards.push_back(std::move(card));
        }
    }
    resized();
    repaint();
}

void EmptySlotChooser::resized()
{
    using LF = MuLookAndFeel;
    using mu_ui::s;
    const int w = getWidth(), h = getHeight();
    const int inset = s(LF::kScrewedPanelInset);
    const int gap = s(LF::kSpaceM);

    titleR = { inset, inset, juce::jmax(0, w - 2 * inset), s(LF::kNamePlateH) + 2 * gap };
    const int hintH = s(LF::kClientNameH) + 2 * gap;
    hintR  = { inset, juce::jmax(titleR.getBottom(), h - inset - hintH), juce::jmax(0, w - 2 * inset), hintH };

    const juce::Rectangle<int> body { inset, titleR.getBottom(), juce::jmax(0, w - 2 * inset),
                                      juce::jmax(0, hintR.getY() - titleR.getBottom()) };
    const int nb = (int) bands.size();
    if (nb == 0) return;
    const int bandH = juce::jmax(0, (body.getHeight() - (nb - 1) * gap) / nb);

    constexpr int kSingleCols = 3;   // the single-type band is three cards wide, as the design's first band
    constexpr int kMaxCols = 5;      // a grouped band wraps after five cards
    for (int b = 0; b < nb; ++b)
    {
        bands[(size_t) b].area = { body.getX(), body.getY() + b * (bandH + gap), body.getWidth(), bandH };

        std::vector<Card*> inBand;
        for (auto& c : cards) if (c.band == b) inBand.push_back(&c);
        if (inBand.empty()) continue;

        // A grouped band is a frame: its plate at the top left, then the cards inset by gap. The single-type band
        // is frameless and headingless (each card's own plate names it) and spans the body.
        const bool grouped = bands[(size_t) b].grouped;
        const auto area = bands[(size_t) b].area;
        const int inner = grouped ? gap : 0;
        const int headingH = grouped ? gap + s(LF::kNamePlateH) + gap : 0;
        const int cardsTop = area.getY() + headingH;
        const int cardsH = juce::jmax(0, area.getBottom() - inner - cardsTop);
        const int cols = grouped ? juce::jmin(kMaxCols, (int) inBand.size()) : kSingleCols;
        const int rows = ((int) inBand.size() + cols - 1) / cols;
        const int innerW = area.getWidth() - 2 * inner;
        const int cardW = juce::jmax(0, (innerW - (cols - 1) * gap) / cols);
        const int cardH = juce::jmax(0, (cardsH - (rows - 1) * gap) / rows);
        for (int i = 0; i < (int) inBand.size(); ++i)
        {
            auto& card = *inBand[(size_t) i];
            card.box = { area.getX() + inner + (i % cols) * (cardW + gap), cardsTop + (i / cols) * (cardH + gap), cardW, cardH };

            // Top to bottom: the type's plate, the picture, the descriptor line, the Add button.
            const int btnH = s(LF::kClientNameH);
            const int btnW = juce::jmin(juce::jmax(0, cardW - 2 * gap), s(LF::kAddButtonW));
            const int btnY = card.box.getBottom() - btnH - gap;
            card.add->setBounds(card.box.getCentreX() - btnW / 2, btnY, btnW, btnH);

            const int plateBottom = card.box.getY() + gap + s(LF::kNamePlateH);
            const int descH = s(LF::kClientNameH);
            card.descriptorR = { card.box.getX() + gap, btnY - gap - descH, juce::jmax(0, cardW - 2 * gap), descH };   // the screen: full inner width
            const int discTop = plateBottom + gap;
            const int discSide = juce::jmax(0, juce::jmin(cardW / 2, card.descriptorR.getY() - gap - discTop));
            card.disc->setBounds(card.box.getCentreX() - discSide / 2, discTop, discSide, discSide);
        }
    }
}

void EmptySlotChooser::paint(juce::Graphics& g)
{
    using LF = MuLookAndFeel;
    using Id = LF::ColourIds;
    using mu_ui::s;
    using mu_ui::sf;

    const auto accent = LF::appAccent(*this);
    LF::drawAccentPanel(g, getLocalBounds().reduced(s(LF::kChooserPanelMargin)).toFloat(), accent);

    // Title on a name plate, top left.
    const juce::String title = "Add a layer to slot " + juce::String(slot + 1);
    const float plateH = sf((float) LF::kNamePlateH);
    LF::drawNamePlate(g, { (float) (titleR.getX() + s(LF::kSpaceS)), (float) titleR.getY() + (float) s(LF::kSpaceM),
                           LF::namePlateWidth(title, plateH), plateH }, title);

    if (cards.empty())
    {
        g.setFont(juce::Font(juce::FontOptions{}.withHeight(sf(LF::kChooserMessageFont))));
        LF::drawEngravedText(g, "No layer types are installed in this build.", getLocalBounds().reduced(s(LF::kChooserMessageInset)),
                             juce::Justification::centred, LF::colour(Id::valueText));
        return;
    }

    // Group frames (neutral steel, screws, the group plate inside at the top left), then every card's shadow before
    // any face so no shadow lands on a neighbouring card.
    for (const auto& band : bands)
        if (band.grouped) LF::drawRaisedSubPanelShadow(g, band.area.toFloat());
    for (const auto& band : bands)
    {
        if (! band.grouped) continue;
        LF::drawRaisedSubPanel(g, band.area.toFloat(), accent);
        if (LF::hasScrews(*this)) LF::drawSubPanelScrews(g, band.area.toFloat());
        LF::drawNamePlate(g, { (float) (band.area.getX() + s(LF::kSpaceM)), (float) (band.area.getY() + s(LF::kSpaceM)),
                               LF::namePlateWidth(band.title, plateH), plateH }, band.title);
    }
    for (const auto& c : cards) LF::drawRaisedSubPanelShadow(g, c.box.toFloat());
    for (const auto& c : cards)
    {
        LF::drawRaisedSubPanel(g, c.box.toFloat(), c.colour);
        LF::drawAccentPanel(g, c.box.toFloat(), c.colour, LF::kCardCorner);   // the colour wash and outline the type's own panel will have
        if (LF::hasScrews(*this)) LF::drawSubPanelScrews(g, c.box.toFloat());

        const float pw = LF::namePlateWidth(c.plate, plateH);
        LF::drawNamePlate(g, { (float) c.box.getCentreX() - pw * 0.5f, (float) c.box.getY() + (float) s(LF::kSpaceM), pw, plateH }, c.plate);

        // The descriptor sits on a dark recessed screen so it reads on any type colour.
        LF::drawRecessedScreen(g, c.descriptorR.toFloat());
        g.setFont(juce::Font(juce::FontOptions{}.withHeight(sf(LF::kCardTextFont))));
        g.setColour(LF::colour(Id::headingText));
        g.drawText(c.descriptor, c.descriptorR, juce::Justification::centred, true);
    }

    g.setFont(juce::Font(juce::FontOptions{}.withHeight(sf(LF::kCardTextFont))));
    LF::drawEngravedText(g, "Or load a layer preset from the menu above.", hintR, juce::Justification::centred, LF::colour(Id::valueText));
}

// A click anywhere on a card adds its type (the Add button is the visible affordance).
void EmptySlotChooser::mouseUp(const juce::MouseEvent& e)
{
    for (const auto& c : cards)
        if (c.box.contains(e.getPosition()))
        {
            if (onChoose) onChoose(c.id);
            return;
        }
}

} // namespace mu_tronic
