#include "MidiPresetListPanel.h"
#include "Plugin/ProcessorBase.h"

MidiPresetListPanel::MidiPresetListPanel(ProcessorBase& p, const juce::String& presetExtension,
                                         const juce::String& titleText, const juce::String& hintText)
    : proc(p), title(titleText), hint(hintText)
{
    closeBtn.onClick = [this] { if (onClose) onClose(); };
    addAndMakeVisible(closeBtn);

    listBox.setModel(this);
    listBox.setRowHeight(mu_ui::s(kListRowH));
    listBox.setColour(juce::ListBox::backgroundColourId,
                      MuLookAndFeel::colour(MuLookAndFeel::panelBackground));
    addAndMakeVisible(listBox);

    browser.setFileExtension(presetExtension);
    browser.onLoadPreset = [this](const juce::File& f)
    {
        if (pendingBrowseRow >= 0 && f.existsAsFile())
        {
            setSlotPath(pendingBrowseRow, f);
            listBox.repaintRow(pendingBrowseRow);
        }
        pendingBrowseRow = -1;
    };
    browser.onClose = [this]
    {
        browser.setVisible(false);
        pendingBrowseRow = -1;
    };
    addChildComponent(browser);
}

void MidiPresetListPanel::paintListBoxItem(int row, juce::Graphics& g, int width, int height, bool rowIsSelected)
{
    using Id = MuLookAndFeel::ColourIds;

    if (rowIsSelected)
        g.fillAll(MuLookAndFeel::colour(Id::sidebarItemSelected).withAlpha(0.25f));

    // Index column (0-127, zero-padded = program number).
    g.setColour(MuLookAndFeel::colour(Id::mutedText));
    g.setFont(juce::Font(juce::FontOptions{}.withHeight(11.0f)));
    g.drawText(juce::String(row).paddedLeft('0', 3), kPad, 0, kIndexW, height,
               juce::Justification::centredLeft, false);

    const int browseX = width - kPad - kClearBtnW - kBrowseBtnW - 4;
    const int clearX  = width - kPad - kClearBtnW;

    // Filename (an em dash when the slot is empty).
    const auto path = slotPath(row);
    const juce::String label = path.isEmpty()
                                 ? juce::String::charToString(0x2014)
                                 : juce::File(path).getFileName();
    g.setColour(MuLookAndFeel::colour(path.isEmpty() ? Id::mutedText : Id::valueText));
    g.drawText(label, kPad + kIndexW + 4, 0,
               browseX - (kPad + kIndexW + 4) - 4, height,
               juce::Justification::centredLeft, true);

    // A row button: outline + centred label.
    auto rowButton = [&](int x, int w, const char* text)
    {
        g.setColour(MuLookAndFeel::colour(Id::segmentInactiveBorder));
        g.drawRect(x, 2, w, height - 4, 1);
        g.setColour(MuLookAndFeel::colour(Id::labelText));
        g.drawText(text, x, 0, w, height, juce::Justification::centred, false);
    };
    rowButton(browseX, kBrowseBtnW, "Browse");
    if (! path.isEmpty())
        rowButton(clearX, kClearBtnW, "Clear");   // only when the slot has an assignment

    // Bottom separator.
    g.setColour(MuLookAndFeel::colour(Id::segmentInactiveBorder).withAlpha(0.3f));
    g.drawLine(0.0f, (float) (height - 1), (float) width, (float) (height - 1), 0.5f);
}

void MidiPresetListPanel::listBoxItemClicked(int row, const juce::MouseEvent& e)
{
    int width = listBox.getWidth();
    if (auto* vp = listBox.getViewport())
        if (vp->getVerticalScrollBar().isVisible())
            width -= vp->getScrollBarThickness();

    const int browseX = width - kPad - kClearBtnW - kBrowseBtnW - 4;
    const int clearX  = width - kPad - kClearBtnW;

    if (e.x >= clearX && e.x < clearX + kClearBtnW && slotPath(row).isNotEmpty())
    {
        clearSlot(row);
        listBox.repaintRow(row);
    }
    else if (e.x >= browseX && e.x < browseX + kBrowseBtnW)
    {
        browseForRow(row);
    }
}

void MidiPresetListPanel::browseForRow(int row)
{
    // The in-app PresetBrowser overlay rather than a native FileChooser.
    pendingBrowseRow = row;
    browser.refresh(presetDir());
    browser.setBounds(getLocalBounds());
    browser.setVisible(true);
    browser.toFront(true);
}

void MidiPresetListPanel::resized()
{
    using mu_ui::s;
    const int w       = getWidth();
    const int pad     = s(kPad);
    const int headerH = s(kHeaderH);
    const int topRowH = s(kTopRowH);

    // Right edge aligned with the top row / list (inset by pad).
    closeBtn.setBounds(w - pad - s(60), pad, s(60), s(28));

    const int topRowY = headerH + pad;
    layoutTopRow({ pad, topRowY, w - pad * 2, topRowH });

    // The list fills the rest, leaving room for the hint drawn in paint().
    const int listY = topRowY + topRowH + s(kHintH) + pad;
    listBox.setBounds(pad, listY, w - pad * 2, getHeight() - listY - pad);

    // The browser overlay (when visible) covers the whole panel.
    if (browser.isVisible())
        browser.setBounds(getLocalBounds());
}

void MidiPresetListPanel::paint(juce::Graphics& g)
{
    using Id = MuLookAndFeel::ColourIds;
    using mu_ui::s;
    using mu_ui::sf;

    g.setColour(MuLookAndFeel::colour(Id::panelBackground));
    g.fillAll();

    const int pad     = s(kPad);
    const int headerH = s(kHeaderH);

    g.setColour(MuLookAndFeel::colour(Id::headingText));
    g.setFont(juce::Font(juce::FontOptions{}.withHeight(sf(14.0f))));
    g.drawText(title, pad, 0, s(420), headerH, juce::Justification::centredLeft, false);

    g.setColour(MuLookAndFeel::colour(Id::segmentInactiveBorder));
    g.drawLine(0.0f, (float) headerH, (float) getWidth(), (float) headerH, 0.5f);

    g.setColour(MuLookAndFeel::colour(Id::mutedText));
    g.setFont(juce::Font(juce::FontOptions{}.withHeight(sf(10.0f))));
    g.drawText(hint, pad, headerH + pad + s(kTopRowH) + s(2), getWidth() - pad * 2, s(kHintH),
               juce::Justification::centredLeft, false);
}
