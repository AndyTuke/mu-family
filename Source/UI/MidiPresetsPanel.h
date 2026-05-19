#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "Components/MuClidLookAndFeel.h"
#include "PresetBrowser.h"

class PluginProcessor;

// Full-area overlay panel that lets the user assign .muRhyth files to the 128 MIDI
// program-change slots and toggle which channels (1-8) are active. Stores changes
// directly into PluginProcessor::midiPresetMap (which persists to JSON on every edit).
class MidiPresetsPanel : public juce::Component,
                         public juce::ListBoxModel
{
public:
    std::function<void()> onClose;

    explicit MidiPresetsPanel(PluginProcessor& proc);

    void resized() override;
    void paint(juce::Graphics& g) override;

    // ListBoxModel
    int  getNumRows() override;
    void paintListBoxItem(int row, juce::Graphics& g, int width, int height, bool rowIsSelected) override;
    void listBoxItemClicked(int row, const juce::MouseEvent& e) override;

private:
    PluginProcessor& proc;

    juce::TextButton                  closeBtn { "Close" };
    std::array<juce::ToggleButton, 8> channelToggles;
    juce::ListBox                     listBox;

    // in-app preset browser overlay shown when the user clicks Browse on
    // a row. Reuses PresetBrowser configured for .muRhyth instead of a raw
    // juce::FileChooser, so the user gets categories / search / preview.
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
