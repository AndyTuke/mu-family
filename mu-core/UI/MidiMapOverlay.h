#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "Control/MidiControlMap.h"
#include "UI/Components/DropdownSelect.h"
#include "UI/Components/MuLookAndFeel.h"
#include "UI/MidiMapActions.h"
#include <memory>
#include <vector>

class ProcessorBase;

namespace mu_ui
{
// The MIDI mapping editor: a full-page screen, opened from Settings, listing every mapping (a CC or note on a channel -> an action)
// with Learn for the source, drop-downs for the action and its target, and Delete. Edits go straight to the processor's
// MidiControlMap, which saves itself. Learn is armed only while this screen is showing: hiding or destroying it always cancels
// it, so a controller is never left swallowed. Layout and wording: docs/design-ui-family.md section 15.
class MidiMapOverlay : public juce::Component,
                       public juce::ListBoxModel,
                       private juce::Timer
{
public:
    std::function<void()> onClose;

    explicit MidiMapOverlay(ProcessorBase& proc);
    ~MidiMapOverlay() override;

    void resized() override;
    void paint(juce::Graphics& g) override;
    void visibilityChanged() override;
    bool keyPressed(const juce::KeyPress& key) override;

    // ListBoxModel: one row per mapping, then the draft rows being filled in.
    int  getNumRows() override { return (int) rows.size(); }
    void paintListBoxItem(int, juce::Graphics&, int, int, bool) override {}
    juce::Component* refreshComponentForRow(int row, bool, juce::Component* existing) override;

    // Test and audit hooks.
    int  numRows() const noexcept { return (int) rows.size(); }
    bool isLearningRow() const noexcept { return learningRow >= 0; }
    void addDraftAndLearn();                 // what "Add mapping" does
    void cancelLearn();                      // ends learn without storing anything
    void tickLearn(juce::uint32 nowMs);      // one poll of the learn state (the timer calls it with the clock)
    // Sets a draft row's source and action by hand (the audit and tests; a user learns the source instead).
    void setRowForTest(int row, bool isNote, int channel, int number, mu_core::ControlActionType type, int layer = 0, int clip = 0);

    // "CC 21  Ch 1" / "Note C3  Ch 10".
    static juce::String sourceText(const mu_core::MidiMapping& m);

private:
    class RowComp;
    struct Row
    {
        int                  storedIndex = -1;   // the MidiControlMap index while stored, -1 for a draft
        bool                 hasSource = false;
        mu_core::MidiMapping m;
    };

    ProcessorBase&              proc;
    std::vector<Row>            rows;
    std::vector<juce::String>   paramIds;      // the parameter pop-up's items by index
    int                         learningRow = -1;
    juce::uint32                learnStartMs = 0;

    juce::TextButton            closeBtn   { "Close" };
    juce::TextButton            addBtn     { "Add mapping" };
    juce::TextButton            clearBtn   { "Clear all" };
    DropdownSelect              quantiseDd;
    juce::ListBox               listBox;

    void timerCallback() override;
    void reload();                              // rows from the map
    void startLearn(int row);
    void finishLearn(int row, bool isNote, int channel, int number);
    void applyRow(int row);                     // stores a complete row
    void deleteRow(int row);
    bool isComplete(const Row& r) const;
    void refreshRows();
    void showParamMenu(int row);
    juce::String targetText(const Row& r) const;
    void say(const juce::String& name, const juce::String& value);
};
} // namespace mu_ui
