#include "MidiPresetsOverlay.h"
#include "Plugin/ProcessorBase.h"

static_assert(MidiPresetMap::NumSlots == 128, "the list shows one row per MIDI program (MidiPresetListPanel)");

namespace
{
    int channelCount(const ProcessorBase& p) { return juce::jlimit(1, 8, p.getMaxChannels()); }
}

MidiPresetsOverlay::MidiPresetsOverlay(ProcessorBase& p)
    : MidiPresetListPanel(p, p.getLayerPresetExtension(), "MIDI Program Change Presets",
                          "MIDI channel N (1-" + juce::String(channelCount(p))
                              + juce::String::fromUTF8(u8") → slot N-1;  program number = preset index")),
      numToggles(channelCount(p))
{
    // One toggle per channel, each switching that channel's bit in the map's channel mask.
    const auto mask = proc.midiPresetMap.getChannelMask();
    for (int i = 0; i < numToggles; ++i)
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

juce::String MidiPresetsOverlay::slotPath(int row) const                      { return proc.midiPresetMap.getPresetPath(row); }
void         MidiPresetsOverlay::setSlotPath(int row, const juce::File& f)    { proc.midiPresetMap.setPresetPath(row, f); }
void         MidiPresetsOverlay::clearSlot(int row)                           { proc.midiPresetMap.clearPreset(row); }
juce::File   MidiPresetsOverlay::presetDir() const                            { return proc.getLayerPresetDir(); }

void MidiPresetsOverlay::layoutTopRow(juce::Rectangle<int> row)
{
    // Eight equal columns, whatever the product's channel count.
    const int toggleW = row.getWidth() / 8;
    for (int i = 0; i < numToggles; ++i)
        channelToggles[(size_t) i].setBounds(row.getX() + i * toggleW, row.getY(),
                                             toggleW - mu_ui::s(6), row.getHeight());
}
