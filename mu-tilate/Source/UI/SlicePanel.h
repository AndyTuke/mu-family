#pragma once

#include "Persistence/PhraseParams.h"
#include "Sequencer/SliceSettings.h"
#include "UI/Components/DropdownSelect.h"
#include "UI/Components/KnobWithLabel.h"
#include "UI/Components/MuLookAndFeel.h"
#include "UI/Components/SegmentControl.h"
#include "UI/Voice/VoiceBand.h"   // mu-core: the family voice band (mu-Clid's Pitch | Filter | Amp | Effects layout)

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <vector>

// The Slice view's control panel (docs/mu-tilate/design-ui.md, "Slice player"): the settings of one hit or slice, laid out exactly
// like the Layer view (Pitch | Filter | Amp | Effects), but every knob is an OFFSET, -x to +x, added to the layer's own value. Below
// the band: how the slice plays (direction) and its pre-fade, each in its own box. It shows the settings it is given and reports every
// change as a whole SliceSettings; it edits no data itself.
namespace mu_tilate
{

class SlicePanel : public juce::Component
{
public:
    SlicePanel();

    // The slice to edit: its index (-1 for none), its settings and a line describing it ("Slice 5 of 56, 0.126 s").
    void setSlice(int index, const SliceSettings& settings, const juce::String& description);
    int  getSlice() const noexcept { return slice; }

    // What the layer has chosen, shown beside the offsets that act on it: the filter type (an index into filterTypeNames()) and
    // the insert effect (an index into the insert algorithm table), whose four knobs the slice's offsets take their labels from.
    void setLayerFilterType(int choice);
    void setLayerInsertAlgorithm(int algo);

    // The user changed a setting of the shown slice.
    std::function<void(const SliceSettings&)> onChange;
    // The user asked for the previous (-1) or next (+1) slice.
    std::function<void(int step)> onStep;
    // The status bar's name and value for the control being changed.
    std::function<void(const juce::String& name, const juce::String& value)> onStatusUpdate;

    // The height this panel needs (unscaled px).
    static int preferredHeight() noexcept;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    struct Knob
    {
        std::unique_ptr<KnobWithLabel> comp;
        float SliceSettings::* field = nullptr;   // null for an insert offset (SliceSettings::insOff[insSlot])
        int insSlot = -1;
    };

    void addKnob(VoiceBandSection* section, int row, int col, const char* label, float SliceSettings::* field, double lo, double hi,
                 double step, MuLookAndFeel::ColourIds colour, const juce::String& statusName, std::function<juce::String(double)> format);
    void addSwitch(SegmentControl& control);
    juce::String insertOffsetText(int slot, double v) const;
    void changed();   // reads every control into `settings` and reports it

    int slice = -1;
    SliceSettings settings;
    bool updating = false;   // setSlice is writing the controls: they must not report back
    int  insertAlgo = 0;
    juce::String description;

    juce::TextButton prevButton { "<" }, nextButton { ">" };
    VoiceBand        band;
    VoiceBandSection pitchSection  { 4, VoiceBand::kCols };
    VoiceBandSection filterSection { 6, VoiceBand::kFilterColW };
    VoiceBandSection ampSection    { 4, VoiceBand::kCols };
    VoiceBandSection insertSection { 6, VoiceBand::kCols };
    DropdownSelect   layerFilterName;    // the layer's filter type, shown (not edited) where the layer view has its dropdown
    DropdownSelect   layerInsertName;    // the layer's insert effect, likewise
    SegmentControl reverseSwitch { { "Fwd", "Rev" },  SegmentControl::ActiveStyle::General, SegmentControl::DrawStyle::Lcd };
    SegmentControl preSwitch     { { "Off", "On" },   SegmentControl::ActiveStyle::General, SegmentControl::DrawStyle::Lcd };
    std::vector<std::unique_ptr<Knob>> knobs;
    std::vector<Knob*> playKnobs;                   // the pre-fade's knobs, placed in the play strip
    std::vector<MuLookAndFeel::Section> sections;   // the play strip's box, as resized() placed it

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SlicePanel)
};

} // namespace mu_tilate
