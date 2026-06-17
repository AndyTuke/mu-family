#include "SettingsOverlayBase.h"

namespace mu_ui
{

SettingsOverlayBase::SettingsOverlayBase()
{
    closeBtn.onClick = [this] { if (onClose) onClose(); };
    addAndMakeVisible(closeBtn);

    // Contact Support footer link (shared across every product's settings page).
    supportLink.setButtonText("Contact Support");
    supportLink.setURL(juce::URL("mailto:support@transwarp.me"));
    supportLink.setFont(juce::Font(juce::FontOptions{}.withHeight(sf(12.0f))), false,
                        juce::Justification::centredLeft);
    supportLink.setColour(juce::HyperlinkButton::textColourId,
                          MuLookAndFeel::colour(MuLookAndFeel::ColourIds::globalAccent));
    addAndMakeVisible(supportLink);
}

void SettingsOverlayBase::computeColumn()
{
    const int w = getWidth();
    colW = juce::jmin(s(kContentMaxW), w - s(kPad) * 2);
    colX = (w - colW) / 2;
}

int SettingsOverlayBase::contentTop() const noexcept
{
    return s(kHeaderH) + s(kPad);
}

void SettingsOverlayBase::resized()
{
    computeColumn();

    const int w       = getWidth();
    const int closeW  = s(kCloseBtnW);
    const int closeH  = s(kCloseBtnH);
    const int headerH = s(kHeaderH);
    closeBtn.setBounds(w - s(kPad) - closeW, (headerH - closeH) / 2, closeW, closeH);

    // "Contact Support" footer — bottom-left, aligned under the section headings.
    const int linkH = s(20);
    supportLink.setBounds(colX, getHeight() - s(kPad) - linkH, s(160), linkH);

    layoutContent();
}

void SettingsOverlayBase::paint(juce::Graphics& g)
{
    using Id = MuLookAndFeel::ColourIds;
    computeColumn();

    g.fillAll(MuLookAndFeel::colour(Id::panelBackground));

    const int headerH = s(kHeaderH);

    // Top "Settings" bar — title + full-width divider.
    g.setColour(MuLookAndFeel::colour(Id::headingText));
    g.setFont(juce::Font(juce::FontOptions{}.withHeight(sf(15.0f))));
    g.drawText("Settings", s(kPad), 0, s(200), headerH, juce::Justification::centredLeft, false);

    g.setColour(MuLookAndFeel::colour(Id::segmentInactiveBorder));
    g.drawLine(0.0f, (float) headerH, (float) getWidth(), (float) headerH, 1.0f);

    paintContent(g);
}

void SettingsOverlayBase::drawGroupHeader(juce::Graphics& g, int headerY, const juce::String& title) const
{
    using Id = MuLookAndFeel::ColourIds;
    g.setColour(MuLookAndFeel::colour(Id::headingText));
    g.setFont(juce::Font(juce::FontOptions{}.withHeight(sf(15.0f))));
    g.drawText(title, colX, headerY, colW, s(20), juce::Justification::centredLeft, false);

    g.setColour(MuLookAndFeel::colour(Id::segmentInactiveBorder));
    const float lineY = (float) (headerY + s(22));
    g.drawLine((float) colX, lineY, (float) (colX + colW), lineY, 1.0f);
}

void SettingsOverlayBase::drawSectionHeader(juce::Graphics& g, int headerY, const juce::String& title) const
{
    using Id = MuLookAndFeel::ColourIds;
    g.setColour(MuLookAndFeel::colour(Id::headingText));
    g.setFont(juce::Font(juce::FontOptions{}.withHeight(sf(12.0f))));
    g.drawText(title, colX, headerY, colW, s(16), juce::Justification::centredLeft, false);

    g.setColour(MuLookAndFeel::colour(Id::segmentInactiveBorder));
    const float lineY = (float) (headerY + s(17));
    g.drawLine((float) colX, lineY, (float) (colX + colW), lineY, 0.5f);
}

void SettingsOverlayBase::drawHint(juce::Graphics& g, int yCentre, const juce::String& text,
                                   int x, int w) const
{
    using Id = MuLookAndFeel::ColourIds;
    g.setColour(MuLookAndFeel::colour(Id::labelText));
    g.setFont(juce::Font(juce::FontOptions{}.withHeight(sf(11.0f))));
    g.drawText(text, x, yCentre - s(7), w, s(14), juce::Justification::centredLeft, false);
}

} // namespace mu_ui
