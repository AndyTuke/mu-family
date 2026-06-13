#include "AboutPanel.h"
#include "BuildNumber.h"
#include "UI/ModalCard.h"   // shared dim + card chrome

AboutPanel::AboutPanel()
{
    closeBtn.onClick = [this] { if (onDismiss) onDismiss(); };
    addAndMakeVisible(closeBtn);
}

void AboutPanel::setProductInfo(const juce::String& displayName,
                                const juce::StringArray& credits)
{
    productName    = displayName;
    productCredits = credits;
    repaint();
}

void AboutPanel::mouseDown(const juce::MouseEvent& e)
{
    using mu_ui::s;
    const int w = getWidth();
    const int h = getHeight();
    const int cardW = s(kCardW);
    const int cardH = s(kCardH);
    const int cardX = (w - cardW) / 2;
    const int cardY = (h - cardH) / 2;
    const juce::Rectangle<int> card { cardX, cardY, cardW, cardH };

    if (!card.contains(e.getPosition()))
        if (onDismiss) onDismiss();
}

void AboutPanel::resized()
{
    using mu_ui::s;
    const int w = getWidth();
    const int h = getHeight();
    const int cardW = s(kCardW);
    const int cardH = s(kCardH);
    const int cardX = (w - cardW) / 2;
    const int cardY = (h - cardH) / 2;

    closeBtn.setBounds(cardX + cardW / 2 - s(36), cardY + cardH - s(40), s(72), s(28));
}

void AboutPanel::paint(juce::Graphics& g)
{
    using Id = MuLookAndFeel::ColourIds;
    using mu_ui::s;
    using mu_ui::sf;

    mu_ui::fillModalDim(g);

    const int w = getWidth();
    const int h = getHeight();
    const int cardW = s(kCardW);
    const int cardH = s(kCardH);
    const int cardX = (w - cardW) / 2;
    const int cardY = (h - cardH) / 2;

    mu_ui::paintModalCard(g, { cardX, cardY, cardW, cardH });

    const int tx = cardX + s(24);
    int ty = cardY + s(24);
    const int textW = cardW - s(48);

    // Title — falls back to a neutral label if the product hasn't set its info yet.
    g.setColour(MuLookAndFeel::colour(Id::headingText));
    g.setFont(juce::Font(juce::FontOptions{}.withHeight(sf(28.0f))));
    g.drawText(productName.isNotEmpty() ? productName : juce::String("mu-Family"),
               tx, ty, textW, s(36), juce::Justification::centredLeft, false);
    ty += s(40);

    g.setFont(juce::Font(juce::FontOptions{}.withHeight(sf(11.0f))));
    g.setColour(MuLookAndFeel::colour(Id::mutedText));
    g.drawText("v1.0.0." + juce::String(BUILD_NUMBER), tx, ty, textW, s(18),
               juce::Justification::centredLeft, false);
    ty += s(20);

    g.drawText("Transwarp Development Project", tx, ty, textW, s(18),
               juce::Justification::centredLeft, false);
    ty += s(30);

    // Credits — supplied by the product.
    g.setColour(MuLookAndFeel::colour(Id::labelText));
    g.setFont(juce::Font(juce::FontOptions{}.withHeight(sf(10.0f))));
    for (const auto& line : productCredits)
    {
        g.drawText(line, tx, ty, textW, s(16), juce::Justification::centredLeft, false);
        ty += s(17);
    }
}
