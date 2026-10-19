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

// One card per registered type, in registry order; the bands are the registry's groups.
void EmptySlotChooser::rebuild()
{
    for (auto& c : cards)
        if (c.add != nullptr) removeChildComponent(c.add.get());
    cards.clear();
    bands.clear();

    for (const auto& group : types.groups())
    {
        bands.push_back({ group, {} });
        for (const auto* info : types.inGroup(group))
        {
            Card card;
            card.id    = info->id;
            card.name  = juce::String::fromUTF8(info->name);
            card.group = group;
            card.add   = std::make_unique<juce::TextButton>("Add");
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

    constexpr int kMaxCols = 5;   // a band wraps after five cards, as the design's engine group does
    for (int b = 0; b < nb; ++b)
    {
        bands[(size_t) b].area = { body.getX(), body.getY() + b * (bandH + gap), body.getWidth(), bandH };

        std::vector<Card*> inBand;
        for (auto& c : cards) if (c.group == bands[(size_t) b].title) inBand.push_back(&c);
        if (inBand.empty()) continue;

        const int plateH = s(LF::kNamePlateH);
        const int cardsTop = bands[(size_t) b].area.getY() + plateH + gap;
        const int cardsH = juce::jmax(0, bands[(size_t) b].area.getBottom() - cardsTop);
        const int cols = juce::jmin(kMaxCols, (int) inBand.size());
        const int rows = ((int) inBand.size() + cols - 1) / cols;
        const int cardW = juce::jmax(0, (body.getWidth() - (cols - 1) * gap) / cols);
        const int cardH = juce::jmax(0, (cardsH - (rows - 1) * gap) / rows);
        for (int i = 0; i < (int) inBand.size(); ++i)
        {
            auto& card = *inBand[(size_t) i];
            card.box = { body.getX() + (i % cols) * (cardW + gap), cardsTop + (i / cols) * (cardH + gap), cardW, cardH };
            const int btnH = juce::jmin(s(22), cardH / 2);
            card.add->setBounds(card.box.getX() + gap, card.box.getBottom() - btnH - gap, juce::jmax(0, cardW - 2 * gap), btnH);
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
    LF::drawAccentPanel(g, getLocalBounds().reduced(2).toFloat(), accent);
    const auto ink = LF::colour(Id::labelText);

    // Title on a name plate, top left.
    const juce::String title = "Add a layer to slot " + juce::String(slot + 1);
    const float plateH = sf((float) LF::kNamePlateH);
    LF::drawNamePlate(g, { (float) titleR.getX() + sf(6.0f), (float) titleR.getY() + (float) s(LF::kSpaceM),
                           LF::namePlateWidth(title, plateH), plateH }, title);

    if (cards.empty())
    {
        g.setFont(juce::Font(juce::FontOptions{}.withHeight(sf(14.0f))));
        LF::drawEngravedText(g, "No layer types are installed in this build.", getLocalBounds().reduced(s(20)),
                             juce::Justification::centred, LF::colour(Id::valueText));
        return;
    }

    // Band headings, then every card's shadow before any face so no shadow lands on a neighbouring card.
    for (const auto& band : bands)
    {
        const float bh = sf((float) LF::kNamePlateH);
        LF::drawNamePlate(g, { (float) band.area.getX() + sf(6.0f), (float) band.area.getY(), LF::namePlateWidth(band.title, bh), bh },
                          band.title);
    }
    for (const auto& c : cards) LF::drawRaisedSubPanelShadow(g, c.box.toFloat());
    for (const auto& c : cards)
    {
        LF::drawRaisedSubPanel(g, c.box.toFloat(), accent);
        if (LF::hasScrews(*this)) LF::drawSubPanelScrews(g, c.box.toFloat());
        LF::drawEngravedText(g, c.name, c.box.withTrimmedBottom(c.box.getHeight() / 2), juce::Justification::centred, ink);
    }

    LF::drawEngravedText(g, "Or load a layer preset from the menu above.", hintR, juce::Justification::centred, ink);
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
