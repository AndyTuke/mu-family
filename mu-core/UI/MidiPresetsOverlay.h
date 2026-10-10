#pragma once
#include "MidiPresetListPanel.h"

// MIDI program change → per-slot (layer) presets: the 128 slots of ProcessorBase::midiPresetMap
// (persisted to JSON on every edit) plus a toggle per channel saying which channels are active
// (only the product's own channels, up to 8). Channel N loads into slot N-1.
class MidiPresetsOverlay : public MidiPresetListPanel
{
public:
    explicit MidiPresetsOverlay(ProcessorBase& proc);

protected:
    juce::String slotPath(int row) const override;
    void         setSlotPath(int row, const juce::File& f) override;
    void         clearSlot(int row) override;
    juce::File   presetDir() const override;
    void         layoutTopRow(juce::Rectangle<int> row) override;

private:
    std::array<juce::ToggleButton, 8> channelToggles;
    int                               numToggles;   // the product's channel count (≤ 8)
};
