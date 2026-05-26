#include "MidiFullPresetsPanel.h"
#include "Plugin/PluginProcessor.h"

MidiFullPresetsPanel::MidiFullPresetsPanel(PluginProcessor& p)
    : proc(p)
{
    closeBtn.onClick = [this] { if (onClose) onClose(); };
    addAndMakeVisible(closeBtn);

    enabledToggle.setButtonText("Enabled (Ch 9)");
    enabledToggle.setToggleState(proc.midiFullPresetMap.isEnabled(), juce::dontSendNotification);
    enabledToggle.onClick = [this]
    {
        proc.midiFullPresetMap.setEnabled(enabledToggle.getToggleState());
    };
    addAndMakeVisible(enabledToggle);

    listBox.setModel(this);
    listBox.setRowHeight(mu_ui::s(kListRowH));
    listBox.setColour(juce::ListBox::backgroundColourId,
                      MuClidLookAndFeel::colour(MuClidLookAndFeel::panelBackground));
    addAndMakeVisible(listBox);

    // configure the in-app preset browser for full (.muclid) presets.
    browser.setFileExtension("muclid");
    browser.onLoadPreset = [this](const juce::File& f)
    {
        if (pendingBrowseRow >= 0 && f.existsAsFile())
        {
            proc.midiFullPresetMap.setPresetPath(pendingBrowseRow, f);
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

int MidiFullPresetsPanel::getNumRows() { return MidiFullPresetMap::NumSlots; }

void MidiFullPresetsPanel::paintListBoxItem(int row, juce::Graphics& g, int width, int height, bool rowIsSelected)
{
    using Id = MuClidLookAndFeel::ColourIds;

    if (rowIsSelected)
        g.fillAll(MuClidLookAndFeel::colour(Id::sidebarItemSelected).withAlpha(0.25f));

    // Index column (0-127, zero-padded = program number).
    g.setColour(MuClidLookAndFeel::colour(Id::mutedText));
    g.setFont(juce::Font(juce::FontOptions{}.withHeight(11.0f)));
    g.drawText(juce::String(row).paddedLeft('0', 3), kPad, 0, kIndexW, height,
               juce::Justification::centredLeft, false);

    const int browseX = width - kPad - kClearBtnW - kBrowseBtnW - 4;
    const int clearX  = width - kPad - kClearBtnW;

    // Filename.
    const auto path = proc.midiFullPresetMap.getPresetPath(row);
    const juce::String label = path.isEmpty()
                                 ? juce::String::charToString(0x2014)
                                 : juce::File(path).getFileName();
    g.setColour(MuClidLookAndFeel::colour(path.isEmpty() ? Id::mutedText : Id::valueText));
    g.drawText(label, kPad + kIndexW + 4, 0,
               browseX - (kPad + kIndexW + 4) - 4, height,
               juce::Justification::centredLeft, true);

    // Browse button.
    g.setColour(MuClidLookAndFeel::colour(Id::segmentInactiveBorder));
    g.drawRect(browseX, 2, kBrowseBtnW, height - 4, 1);
    g.setColour(MuClidLookAndFeel::colour(Id::labelText));
    g.drawText("Browse", browseX, 0, kBrowseBtnW, height,
               juce::Justification::centred, false);

    // Clear button (only shown when slot has an assignment).
    if (! path.isEmpty())
    {
        g.setColour(MuClidLookAndFeel::colour(Id::segmentInactiveBorder));
        g.drawRect(clearX, 2, kClearBtnW, height - 4, 1);
        g.setColour(MuClidLookAndFeel::colour(Id::labelText));
        g.drawText("Clear", clearX, 0, kClearBtnW, height,
                   juce::Justification::centred, false);
    }

    // Bottom separator.
    g.setColour(MuClidLookAndFeel::colour(Id::segmentInactiveBorder).withAlpha(0.3f));
    g.drawLine(0.0f, (float) (height - 1), (float) width, (float) (height - 1), 0.5f);
}

void MidiFullPresetsPanel::listBoxItemClicked(int row, const juce::MouseEvent& e)
{
    int width = listBox.getWidth();
    if (auto* vp = listBox.getViewport())
        if (vp->getVerticalScrollBar().isVisible())
            width -= vp->getScrollBarThickness();

    const int browseX = width - kPad - kClearBtnW - kBrowseBtnW - 4;
    const int clearX  = width - kPad - kClearBtnW;

    if (e.x >= clearX && e.x < clearX + kClearBtnW
        && proc.midiFullPresetMap.hasPreset(row))
    {
        clearRow(row);
    }
    else if (e.x >= browseX && e.x < browseX + kBrowseBtnW)
    {
        browseForRow(row);
    }
}

void MidiFullPresetsPanel::browseForRow(int row)
{
    pendingBrowseRow = row;
    browser.refresh(proc.getPresetsDir());   // full .muclid presets live in Presets/
    browser.setBounds(getLocalBounds());
    browser.setVisible(true);
    browser.toFront(true);
}

void MidiFullPresetsPanel::clearRow(int row)
{
    proc.midiFullPresetMap.clearPreset(row);
    listBox.repaintRow(row);
}

void MidiFullPresetsPanel::resized()
{
    using mu_ui::s;
    const int w   = getWidth();
    const int pad = s(kPad);
    const int toggleRowH = s(kToggleRowH);
    const int headerH = s(kHeaderH);
    const int hintH = s(kHintH);

    closeBtn.setBounds(w - pad * 3 - s(60), pad, s(60), s(28));

    // Enabled toggle row.
    const int toggleRowY = headerH + pad;
    enabledToggle.setBounds(pad, toggleRowY, s(160), toggleRowH);

    // ListBox fills the rest, leaving room for the hint text drawn in paint().
    const int listY = toggleRowY + toggleRowH + hintH + pad;
    listBox.setBounds(pad, listY, w - pad * 2, getHeight() - listY - pad);

    // the browser overlay (when visible) covers the whole panel.
    if (browser.isVisible())
        browser.setBounds(getLocalBounds());
}

void MidiFullPresetsPanel::paint(juce::Graphics& g)
{
    using Id = MuClidLookAndFeel::ColourIds;
    using mu_ui::s;
    using mu_ui::sf;

    g.setColour(MuClidLookAndFeel::colour(Id::panelBackground));
    g.fillAll();

    const int pad = s(kPad);
    const int headerH = s(kHeaderH);
    const int toggleRowH = s(kToggleRowH);
    const int hintH   = s(kHintH);

    g.setColour(MuClidLookAndFeel::colour(Id::headingText));
    g.setFont(juce::Font(juce::FontOptions{}.withHeight(sf(14.0f))));
    g.drawText("MIDI Program Change — Full Presets (Ch 9)", pad, 0, s(420), headerH,
               juce::Justification::centredLeft, false);

    g.setColour(MuClidLookAndFeel::colour(Id::segmentInactiveBorder));
    g.drawLine(0.0f, (float) headerH, (float) getWidth(), (float) headerH, 0.5f);

    g.setColour(MuClidLookAndFeel::colour(Id::mutedText));
    g.setFont(juce::Font(juce::FontOptions{}.withHeight(sf(10.0f))));
    g.drawText(juce::String::fromUTF8(u8"Program change on MIDI channel 9 → full .muclid preset (loads at the next loop point)"),
               pad, headerH + pad + toggleRowH + s(2), getWidth() - pad * 2, hintH,
               juce::Justification::centredLeft, false);
}
