#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "UI/Components/MuLookAndFeel.h"
#include "PresetBrowser.h"

class ProcessorBase;

// Full-area overlay panel that lets the user assign per-slot preset files to
// the 128 MIDI program-change slots and toggle which channels (1-8) are
// active. Stores changes directly into ProcessorBase::midiPresetMap (which
// persists to JSON on every edit). File extension + browser root directory
// come from the consuming plugin via virtuals on ProcessorBase.
class MidiPresetsPanel : public juce::Component,
                         public juce::ListBoxModel
{
public:
    std::function<void()> onClose;

    explicit MidiPresetsPanel(ProcessorBase& proc);

    void resized() override;
    void paint(juce::Graphics& g) override;

    // ListBoxModel
    int  getNumRows() override;
    void paintListBoxItem(int row, juce::Graphics& g, int width, int height, bool rowIsSelected) override;
    void listBoxItemClicked(int row, const juce::MouseEvent& e) override;

private:
    ProcessorBase& proc;

    juce::TextButton                  closeBtn { "Close" };
    std::array<juce::ToggleButton, 8> channelToggles;
    juce::ListBox                     listBox;

    // in-app preset browser overlay shown when the user clicks Browse on a
    // row. Reuses PresetBrowser configured for the plugin's per-slot preset
    // extension (mu-clid uses "muRhyth"; mu-tant will use its own).
    PresetBrowser                     browser;
    int                               pendingBrowseRow = -1;

    void wireChannelToggles();
    void browseForRow(int row);
    void clearRow(int row);

    static constexpr int kHeaderH      = 36;
    static constexpr int kPad          = 12;
    static constexpr int kChannelRowH  = 26;
    static constexpr int kHintH        = 18;
    static constexpr int kListRowH     = 24;
    static constexpr int kIndexW       = 36;
    static constexpr int kBrowseBtnW   = 60;
    static constexpr int kClearBtnW    = 50;
};
