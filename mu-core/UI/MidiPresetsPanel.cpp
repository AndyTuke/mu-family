#include "MidiPresetsPanel.h"
#include "Plugin/ProcessorBase.h"

MidiPresetsPanel::MidiPresetsPanel(ProcessorBase& p)
    : proc(p)
{
    closeBtn.onClick = [this] { if (onClose) onClose(); };
    addAndMakeVisible(closeBtn);

    wireChannelToggles();

    listBox.setModel(this);
    listBox.setRowHeight(mu_ui::s(kListRowH));
    listBox.setColour(juce::ListBox::backgroundColourId,
                      MuLookAndFeel::colour(MuLookAndFeel::panelBackground));
    addAndMakeVisible(listBox);

    // configure the in-app preset browser for the plugin's per-slot preset
    // extension (mu-clid uses "muRhyth"; mu-tant will define its own).
    browser.setFileExtension(proc.getPerSlotPresetExtension());
    browser.onLoadPreset = [this](const juce::File& f)
    {
        if (pendingBrowseRow >= 0 && f.existsAsFile())
        {
            proc.midiPresetMap.setPresetPath(pendingBrowseRow, f);
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

void MidiPresetsPanel::wireChannelToggles()
{
    const auto mask = proc.midiPresetMap.getChannelMask();
    for (int i = 0; i < 8; ++i)
    {
        auto& btn = channelToggles[(size_t) i];
        btn.setButtonText("Ch " + juce::String(i + 1));
        btn.setToggleState((mask & (1 << i)) != 0, juce::dontSendNotification);
        btn.onClick = [this, i]
        {
            auto cur = proc.midiPresetMap.getChannelMask();
            const uint8_t bit = (uint8_t) (1 << i);
            if (channelToggles[(size_t) i].getToggleState()) cur |= bit;
            else                                              cur = (uint8_t) (cur & ~bit);
            proc.midiPresetMap.setChannelMask(cur);
        };
        addAndMakeVisible(btn);
    }
}

int MidiPresetsPanel::getNumRows() { return MidiPresetMap::NumSlots; }

void MidiPresetsPanel::paintListBoxItem(int row, juce::Graphics& g, int width, int height, bool rowIsSelected)
{
    using Id = MuLookAndFeel::ColourIds;

    if (rowIsSelected)
        g.fillAll(MuLookAndFeel::colour(Id::sidebarItemSelected).withAlpha(0.25f));

    // Index column (0-127, zero-padded).
    g.setColour(MuLookAndFeel::colour(Id::mutedText));
    g.setFont(juce::Font(juce::FontOptions{}.withHeight(11.0f)));
    g.drawText(juce::String(row).paddedLeft('0', 3), kPad, 0, kIndexW, height,
               juce::Justification::centredLeft, false);

    const int browseX = width - kPad - kClearBtnW - kBrowseBtnW - 4;
    const int clearX  = width - kPad - kClearBtnW;

    // Filename.
    const auto path = proc.midiPresetMap.getPresetPath(row);
    const juce::String label = path.isEmpty()
                                 ? juce::String::charToString(0x2014)
                                 : juce::File(path).getFileName();
    g.setColour(MuLookAndFeel::colour(path.isEmpty() ? Id::mutedText : Id::valueText));
    g.drawText(label, kPad + kIndexW + 4, 0,
               browseX - (kPad + kIndexW + 4) - 4, height,
               juce::Justification::centredLeft, true);

    // Browse button.
    g.setColour(MuLookAndFeel::colour(Id::segmentInactiveBorder));
    g.drawRect(browseX, 2, kBrowseBtnW, height - 4, 1);
    g.setColour(MuLookAndFeel::colour(Id::labelText));
    g.drawText("Browse", browseX, 0, kBrowseBtnW, height,
               juce::Justification::centred, false);

    // Clear button (only shown when slot has an assignment).
    if (! path.isEmpty())
    {
        g.setColour(MuLookAndFeel::colour(Id::segmentInactiveBorder));
        g.drawRect(clearX, 2, kClearBtnW, height - 4, 1);
        g.setColour(MuLookAndFeel::colour(Id::labelText));
        g.drawText("Clear", clearX, 0, kClearBtnW, height,
                   juce::Justification::centred, false);
    }

    // Bottom separator.
    g.setColour(MuLookAndFeel::colour(Id::segmentInactiveBorder).withAlpha(0.3f));
    g.drawLine(0.0f, (float) (height - 1), (float) width, (float) (height - 1), 0.5f);
}

void MidiPresetsPanel::listBoxItemClicked(int row, const juce::MouseEvent& e)
{
    int width = listBox.getWidth();
    if (auto* vp = listBox.getViewport())
        if (vp->getVerticalScrollBar().isVisible())
            width -= vp->getScrollBarThickness();

    const int browseX = width - kPad - kClearBtnW - kBrowseBtnW - 4;
    const int clearX  = width - kPad - kClearBtnW;

    if (e.x >= clearX && e.x < clearX + kClearBtnW
        && proc.midiPresetMap.hasPreset(row))
    {
        clearRow(row);
    }
    else if (e.x >= browseX && e.x < browseX + kBrowseBtnW)
    {
        browseForRow(row);
    }
}

void MidiPresetsPanel::browseForRow(int row)
{
    // show the in-app PresetBrowser overlay rather than a native FileChooser.
    pendingBrowseRow = row;
    browser.refresh(proc.getPerSlotPresetDir());
    browser.setBounds(getLocalBounds());
    browser.setVisible(true);
    browser.toFront(true);
}

void MidiPresetsPanel::clearRow(int row)
{
    proc.midiPresetMap.clearPreset(row);
    listBox.repaintRow(row);
}

void MidiPresetsPanel::resized()
{
    using mu_ui::s;
    const int w   = getWidth();
    const int pad = s(kPad);
    const int chRowH = s(kChannelRowH);
    const int headerH = s(kHeaderH);
    const int hintH = s(kHintH);

    // Right edge aligned with the toggle row / list (inset by pad, not pad*3).
    closeBtn.setBounds(w - pad - s(60), pad, s(60), s(28));

    // Channel toggles row.
    const int chRowY      = headerH + pad;
    const int totalToggleW = w - pad * 2;
    const int toggleW      = totalToggleW / 8;
    for (int i = 0; i < 8; ++i)
        channelToggles[(size_t) i].setBounds(pad + i * toggleW, chRowY,
                                             toggleW - s(6), chRowH);

    // ListBox fills the rest, leaving room for the hint text drawn in paint().
    const int listY = chRowY + chRowH + hintH + pad;
    listBox.setBounds(pad, listY, w - pad * 2, getHeight() - listY - pad);

    // the browser overlay (when visible) covers the whole panel.
    if (browser.isVisible())
        browser.setBounds(getLocalBounds());
}

void MidiPresetsPanel::paint(juce::Graphics& g)
{
    using Id = MuLookAndFeel::ColourIds;
    using mu_ui::s;
    using mu_ui::sf;

    g.setColour(MuLookAndFeel::colour(Id::panelBackground));
    g.fillAll();

    const int pad = s(kPad);
    const int headerH = s(kHeaderH);
    const int chRowH  = s(kChannelRowH);
    const int hintH   = s(kHintH);

    g.setColour(MuLookAndFeel::colour(Id::headingText));
    g.setFont(juce::Font(juce::FontOptions{}.withHeight(sf(14.0f))));
    g.drawText("MIDI Program Change Presets", pad, 0, s(400), headerH,
               juce::Justification::centredLeft, false);

    g.setColour(MuLookAndFeel::colour(Id::segmentInactiveBorder));
    g.drawLine(0.0f, (float) headerH, (float) getWidth(), (float) headerH, 0.5f);

    g.setColour(MuLookAndFeel::colour(Id::mutedText));
    g.setFont(juce::Font(juce::FontOptions{}.withHeight(sf(10.0f))));
    g.drawText(juce::String::fromUTF8(u8"MIDI channel N (1-8) → slot N-1;  program number = preset index"),
               pad, headerH + pad + chRowH + s(2), getWidth() - pad * 2, hintH,
               juce::Justification::centredLeft, false);
}
