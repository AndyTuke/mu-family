#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "UI/Components/MuLookAndFeel.h"
#include "PresetBrowser.h"

class ProcessorBase;

// The shared body of the MIDI program-change panels: a title, a row of controls the subclass
// provides (channel toggles / an enable toggle), a hint line and the 128-slot list — each slot's
// preset file name with Browse (the in-app PresetBrowser) and Clear. The subclass maps slots to
// its preset map and lays out its top-row controls.
class MidiPresetListPanel : public juce::Component,
                            public juce::ListBoxModel
{
public:
    std::function<void()> onClose;

    // `presetExtension` / `title` / `hint`: what the slots hold and the panel's wording.
    MidiPresetListPanel(ProcessorBase& proc, const juce::String& presetExtension,
                        const juce::String& title, const juce::String& hint);

    void resized() override;
    void paint(juce::Graphics& g) override;

    // ListBoxModel — one row per MIDI program number. Answered here, not by the subclass: the
    // list box asks for it while this base is still being constructed.
    int  getNumRows() override { return kNumPrograms; }
    void paintListBoxItem(int row, juce::Graphics& g, int width, int height, bool rowIsSelected) override;
    void listBoxItemClicked(int row, const juce::MouseEvent& e) override;

protected:
    ProcessorBase& proc;

    // The subclass's preset map and browse folder.
    virtual juce::String slotPath(int row) const = 0;
    virtual void         setSlotPath(int row, const juce::File& f) = 0;
    virtual void         clearSlot(int row) = 0;
    virtual juce::File   presetDir() const = 0;
    // Lay out the subclass's controls in the row under the title.
    virtual void         layoutTopRow(juce::Rectangle<int> row) = 0;

private:
    juce::String     title, hint;
    juce::TextButton closeBtn { "Close" };
    juce::ListBox    listBox;
    PresetBrowser    browser;            // shown over the panel when a row's Browse is clicked
    int              pendingBrowseRow = -1;

    void browseForRow(int row);

    static constexpr int kNumPrograms = 128;   // MIDI program change 0..127
    static constexpr int kHeaderH    = 36;
    static constexpr int kPad        = 12;
    static constexpr int kTopRowH    = 26;
    static constexpr int kHintH      = 18;
    static constexpr int kListRowH   = 24;
    static constexpr int kIndexW     = 36;
    static constexpr int kBrowseBtnW = 60;
    static constexpr int kClearBtnW  = 50;
};
