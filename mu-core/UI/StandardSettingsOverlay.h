#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "UI/SettingsOverlayBase.h"
#include "UI/Components/KnobWithLabel.h"
#include "UI/Components/SegmentControl.h"
#include "UI/Components/NudgeInput.h"
#include "UI/Components/DropdownSelect.h"
#include "Plugin/ProcessorBase.h"

#include <vector>

namespace mu_ui
{

// The family-standard settings page. Provides the sections every product has —
//   General: Audio (master volume), Display (UI size), Transport (internal BPM, optional)
//   MIDI:    MIDI Clock (standalone only)
// — laid out with the shared group / section styling. A product adds its own sections at
// named places (extra General sections, MIDI sections before / after MIDI Clock, or whole
// extra groups such as mu-Clid's Locations) with addSection; the page stacks them with the
// standard spacing and draws their headings. A product with nothing extra uses this class
// as-is.
class StandardSettingsOverlay : public SettingsOverlayBase
{
public:
    struct Options { bool showTransport = true; };   // mu-Clid sets its tempo in the transport bar

    explicit StandardSettingsOverlay(ProcessorBase& proc, Options options);
    explicit StandardSettingsOverlay(ProcessorBase& proc) : StandardSettingsOverlay(proc, Options{}) {}

    void layoutContent() override;
    void paintContent(juce::Graphics& g) override;

    // Geometry handed to a section when it is placed.
    struct Rows
    {
        juce::Rectangle<int> area;     // the section's rows (below its heading)
        int labelX, ctrlX;             // row label / control columns
        int labelW, ctrlW, rowH;       // standard widths + row height
    };

    struct Section
    {
        juce::String title;                                         // heading ("" = none)
        int heightPx = kRowH;                                       // unscaled height of the rows
        std::function<void(const Rows&)> place;                     // position the section's controls
        std::function<void(juce::Graphics&, const Rows&)> paint;    // optional extra drawing
    };

    enum class Where { General, MidiBeforeClock, MidiAfterClock, Group };

    // Add a section. Where::Group sections share one heading per `group` name (in the order
    // first added), after the MIDI group; an empty group name means no group heading.
    void addSection(Where where, Section section, const juce::String& group = {});

    // A field label in the standard style (for products' own rows).
    void makeFieldLabel(juce::Label& label, const juce::String& text);

    // The MIDI Program Change section (after MIDI Clock): two buttons that open the shared
    // program-change tables — Ch 1-8 → layer presets, Ch 9 → full presets. Products with
    // presets call this once, at the point in their constructor where the section belongs.
    void addProgramChangeSection(const juce::String& layerTableName, const juce::String& fullTableName);
    std::function<void()> onMidiPresetsClicked;   // the editor opens showMidiPresets
    std::function<void()> onFullPresetsClicked;   // the editor opens showMidiFullPresets

protected:
    ProcessorBase& proc;
    const bool isStandalone;

private:
    // Standard controls.
    KnobWithLabel  masterVolKnob { "Master Vol", MuLookAndFeel::knobLevel };
    juce::Label    uiSizeLabel;
    SegmentControl uiSizeCtrl { { "Medium", "Large" } };
    juce::Label    bpmLabel;
    NudgeInput     bpmInput { "BPM", 20, 300, 120 };
    juce::Label    clockSourceLabel, midiMessagesLabel;
    DropdownSelect clockSourceDropdown, midiMessagesDropdown;
    void updateMidiSyncVisibility();
    juce::TextButton midiPresetsBtn, fullPresetsBtn;   // program-change tables (added on request)

    std::vector<Section> general, midiBefore, midiClock, midiAfter;
    struct GroupSection { juce::String group; Section section; };
    std::vector<GroupSection> extraGroups;

    // Filled by layoutContent, drawn by paintContent.
    struct Heading { int y; juce::String title; bool group; };
    std::vector<Heading> headings;
    std::vector<std::pair<const Section*, Rows>> placed;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StandardSettingsOverlay)
};

} // namespace mu_ui
