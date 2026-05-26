#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "UI/Components/MuLookAndFeel.h"
#include "PresetBrowser.h"

class ProcessorBase;

// Full-area overlay panel that lets the user assign full preset files to the
// 128 MIDI program-change slots triggered on MIDI channel 9, and toggle the
// feature on/off. Stores changes directly into ProcessorBase::midiFullPresetMap
// (which persists to JSON on every edit). Parallel to MidiPresetsPanel, which
// handles the per-slot map on channels 1-8. File extension + directory come
// from the consuming plugin via virtuals on ProcessorBase.
class MidiFullPresetsPanel : public juce::Component,
                             public juce::ListBoxModel
{
public:
    std::function<void()> onClose;

    explicit MidiFullPresetsPanel(ProcessorBase& proc);

    void resized() override;
    void paint(juce::Graphics& g) override;

    // ListBoxModel
    int  getNumRows() override;
    void paintListBoxItem(int row, juce::Graphics& g, int width, int height, bool rowIsSelected) override;
    void listBoxItemClicked(int row, const juce::MouseEvent& e) override;

private:
    ProcessorBase& proc;

    juce::TextButton   closeBtn { "Close" };
    juce::ToggleButton enabledToggle;
    juce::ListBox      listBox;

    // in-app preset browser overlay shown when the user clicks Browse on a row,
    // configured for the plugin's full-preset extension.
    PresetBrowser      browser;
    int                pendingBrowseRow = -1;

    void browseForRow(int row);
    void clearRow(int row);

    static constexpr int kHeaderH      = 36;
    static constexpr int kPad          = 12;
    static constexpr int kToggleRowH   = 26;
    static constexpr int kHintH        = 18;
    static constexpr int kListRowH     = 24;
    static constexpr int kIndexW       = 36;
    static constexpr int kBrowseBtnW   = 60;
    static constexpr int kClearBtnW    = 50;
};
