#include "MidiFullPresetsPanel.h"
#include "Plugin/ProcessorBase.h"

static_assert(MidiFullPresetMap::NumSlots == 128, "the list shows one row per MIDI program (MidiPresetListPanel)");

MidiFullPresetsPanel::MidiFullPresetsPanel(ProcessorBase& p)
    : MidiPresetListPanel(p, p.getFullPresetExtension(),
                          juce::String::fromUTF8("MIDI Program Change \xe2\x80\x94 Full Presets (Ch 9)"),
                          juce::String::fromUTF8(u8"Program change on MIDI channel 9 → full preset"))
{
    enabledToggle.setButtonText("Enabled (Ch 9)");
    enabledToggle.setToggleState(proc.midiFullPresetMap.isEnabled(), juce::dontSendNotification);
    enabledToggle.onClick = [this]
    {
        proc.midiFullPresetMap.setEnabled(enabledToggle.getToggleState());
    };
    addAndMakeVisible(enabledToggle);
}

juce::String MidiFullPresetsPanel::slotPath(int row) const                   { return proc.midiFullPresetMap.getPresetPath(row); }
void         MidiFullPresetsPanel::setSlotPath(int row, const juce::File& f) { proc.midiFullPresetMap.setPresetPath(row, f); }
void         MidiFullPresetsPanel::clearSlot(int row)                        { proc.midiFullPresetMap.clearPreset(row); }
juce::File   MidiFullPresetsPanel::presetDir() const                         { return proc.getFullPresetDir(); }

void MidiFullPresetsPanel::layoutTopRow(juce::Rectangle<int> row)
{
    enabledToggle.setBounds(row.withWidth(mu_ui::s(160)));
}
