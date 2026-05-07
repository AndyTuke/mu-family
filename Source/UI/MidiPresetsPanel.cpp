#include "MidiPresetsPanel.h"
#include "../PluginProcessor.h"

MidiPresetsPanel::MidiPresetsPanel(PluginProcessor& p)
    : proc(p)
{
    closeBtn.onClick = [this] { if (onClose) onClose(); };
    addAndMakeVisible(closeBtn);

    wireChannelToggles();

    listBox.setModel(this);
    listBox.setRowHeight(kListRowH);
    listBox.setColour(juce::ListBox::backgroundColourId,
                      MuClidLookAndFeel::colour(MuClidLookAndFeel::panelBackground));
    addAndMakeVisible(listBox);
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
    using Id = MuClidLookAndFeel::ColourIds;

    if (rowIsSelected)
        g.fillAll(MuClidLookAndFeel::colour(Id::sidebarItemSelected).withAlpha(0.25f));

    // Index column (0-127, zero-padded).
    g.setColour(MuClidLookAndFeel::colour(Id::mutedText));
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
    const auto startDir = proc.getRhythmsDir().isDirectory()
                              ? proc.getRhythmsDir()
                              : juce::File();
    fileChooser = std::make_unique<juce::FileChooser>(
        "Choose .muRhyth preset for program " + juce::String(row),
        startDir, "*.muRhyth");
    fileChooser->launchAsync(
        juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [this, row](const juce::FileChooser& fc)
        {
            const auto f = fc.getResult();
            if (f.existsAsFile())
            {
                proc.midiPresetMap.setPresetPath(row, f);
                listBox.repaintRow(row);
            }
        });
}

void MidiPresetsPanel::clearRow(int row)
{
    proc.midiPresetMap.clearPreset(row);
    listBox.repaintRow(row);
}

void MidiPresetsPanel::resized()
{
    const int w = getWidth();

    closeBtn.setBounds(w - kPad * 3 - 60, kPad, 60, 28);

    // Channel toggles row.
    const int chRowY      = kHeaderH + kPad;
    const int totalToggleW = w - kPad * 2;
    const int toggleW      = totalToggleW / 8;
    for (int i = 0; i < 8; ++i)
        channelToggles[(size_t) i].setBounds(kPad + i * toggleW, chRowY,
                                             toggleW - 6, kChannelRowH);

    // ListBox fills the rest, leaving room for the hint text drawn in paint().
    const int listY = chRowY + kChannelRowH + kHintH + kPad;
    listBox.setBounds(kPad, listY, w - kPad * 2, getHeight() - listY - kPad);
}

void MidiPresetsPanel::paint(juce::Graphics& g)
{
    using Id = MuClidLookAndFeel::ColourIds;

    g.setColour(MuClidLookAndFeel::colour(Id::panelBackground));
    g.fillAll();

    g.setColour(MuClidLookAndFeel::colour(Id::headingText));
    g.setFont(juce::Font(juce::FontOptions{}.withHeight(14.0f)));
    g.drawText("MIDI Program Change Presets", kPad, 0, 400, kHeaderH,
               juce::Justification::centredLeft, false);

    g.setColour(MuClidLookAndFeel::colour(Id::segmentInactiveBorder));
    g.drawLine(0.0f, (float) kHeaderH, (float) getWidth(), (float) kHeaderH, 0.5f);

    g.setColour(MuClidLookAndFeel::colour(Id::mutedText));
    g.setFont(juce::Font(juce::FontOptions{}.withHeight(10.0f)));
    g.drawText(juce::String::fromUTF8(u8"MIDI channel N (1-8) → rhythm slot N-1;  program number = preset index"),
               kPad, kHeaderH + kPad + kChannelRowH + 2, getWidth() - kPad * 2, kHintH,
               juce::Justification::centredLeft, false);
}
