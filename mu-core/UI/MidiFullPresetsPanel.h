#pragma once
#include "MidiPresetListPanel.h"

// MIDI program change on channel 9 → full presets: the 128 slots of
// ProcessorBase::midiFullPresetMap (persisted to JSON on every edit) plus the feature's
// on / off toggle. Parallel to MidiPresetsPanel, which handles the per-slot map on channels 1-8.
class MidiFullPresetsPanel : public MidiPresetListPanel
{
public:
    explicit MidiFullPresetsPanel(ProcessorBase& proc);

protected:
    juce::String slotPath(int row) const override;
    void         setSlotPath(int row, const juce::File& f) override;
    void         clearSlot(int row) override;
    juce::File   presetDir() const override;
    void         layoutTopRow(juce::Rectangle<int> row) override;

private:
    juce::ToggleButton enabledToggle;
};
