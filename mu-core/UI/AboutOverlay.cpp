#include "AboutOverlay.h"
#include "License/SupportPromptPolicy.h"
#include "BuildNumber.h"
#include "UI/ModalCard.h"   // shared dim + card chrome

AboutOverlay::AboutOverlay()
{
    closeBtn.onClick   = [this] { if (onDismiss) onDismiss(); };
    supportBtn.onClick = [this] { if (onSupport)  onSupport(); };
    keyBtn.onClick     = [this] { if (onEnterKey) onEnterKey(); };
    addAndMakeVisible(closeBtn);
    addChildComponent(supportBtn);
    addChildComponent(keyBtn);

    // The supporter's tip link, styled like the Settings "Contact Support" link.
    tipLink.setButtonText("Want to give more?");
    tipLink.setURL(juce::URL(mu_core::support_prompt::kTipUrl));
    tipLink.setTooltip("Opens the tip page in your browser");
    tipLink.setFont(juce::Font(juce::FontOptions{}.withHeight(mu_ui::sf(12.0f))), false,
                    juce::Justification::centredLeft);
    tipLink.setColour(juce::HyperlinkButton::textColourId,
                      MuLookAndFeel::colour(MuLookAndFeel::ColourIds::globalAccent));
    addChildComponent(tipLink);
}

void AboutOverlay::setSupportState(SupportState state)
{
    supportState = state;
    supportBtn.setVisible(state == SupportState::Unlicensed);
    keyBtn.setVisible(state == SupportState::Unlicensed);
    tipLink.setVisible(state == SupportState::Supporter);
    repaint();
}

void AboutOverlay::setProductInfo(const juce::String& displayName,
                                const juce::StringArray& credits)
{
    productName    = displayName;
    productCredits = credits;
    repaint();
}

void AboutOverlay::mouseDown(const juce::MouseEvent& e)
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

void AboutOverlay::resized()
{
    using mu_ui::s;
    const int w = getWidth();
    const int h = getHeight();
    const int cardW = s(kCardW);
    const int cardH = s(kCardH);
    const int cardX = (w - cardW) / 2;
    const int cardY = (h - cardH) / 2;

    // One bottom row. Close is centred as before; the two support buttons sit either side of
    // it (inside the 400 px card: 24 margin + 100 + 8 + 72 + 8 + 80 + 24 fits with room).
    const int rowY = cardY + cardH - s(40);
    closeBtn.setBounds(cardX + cardW / 2 - s(36), rowY, s(72), s(28));
    supportBtn.setBounds(closeBtn.getRight() + s(8), rowY, s(80), s(28));
    keyBtn.setBounds(closeBtn.getX() - s(8) - s(100), rowY, s(100), s(28));

    // A supporter's thank-you (painted) and the tip link sit left-aligned above the Close row.
    tipLink.setBounds(cardX + s(24), rowY - s(22), s(160), s(18));
}

void AboutOverlay::paint(juce::Graphics& g)
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
    g.drawText("v" + juce::String(MU_VERSION_STRING), tx, ty, textW, s(18),
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

    // A supporter gets a thank-you above the Close row, with the tip link under it.
    if (supportState == SupportState::Supporter)
    {
        g.setColour(MuLookAndFeel::colour(Id::mutedText));
        g.setFont(juce::Font(juce::FontOptions{}.withHeight(sf(11.0f))));
        g.drawText("Thank you for supporting " + productName + ".", cardX + s(24), cardY + cardH - s(62),
                   cardW - s(48), s(18), juce::Justification::centredLeft, false);
    }
}
