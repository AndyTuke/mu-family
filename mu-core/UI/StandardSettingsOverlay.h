#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "UI/SettingsOverlayBase.h"
#include "UI/Components/KnobWithLabel.h"
#include "UI/Components/SegmentControl.h"
#include "Plugin/UpdateCheckPolicy.h"
#include "UI/Components/NudgeInput.h"
#include "UI/Components/DropdownSelect.h"
#include "UI/Components/StatusLamp.h"
#include "Plugin/ShellBackend.h"
#include "Control/AttachmentWatch.h"

#include <vector>

namespace mu_ui
{

// The family-standard settings page. Provides the sections every product has —
//   General: Audio (master volume, when the backend has one), Display (UI size), Transport
//            (internal BPM and meter, each optional)
//   MIDI:    MIDI Clock (standalone only)
//   Controller: the control-surface lamp and Release / Connect (only while the shell has attached a controller)
// — laid out with the shared group / section styling. A product adds its own sections at
// named places (extra General sections, MIDI sections before / after MIDI Clock, or whole
// extra groups such as mu-Clid's Locations) with addSection; the page stacks them with the
// standard spacing and draws their headings. A product with nothing extra uses this class
// as-is.
class StandardSettingsOverlay : public SettingsOverlayBase
{
public:
    // Row switches; the defaults give the page every product has today.
    struct Options
    {
        bool showTransport    = true;    // the Tempo row (mu-Clid sets its tempo in the transport bar)
        bool showMeter        = true;    // the Time signature row (standalone only)
        bool showClockSource  = true;    // the MIDI Clock Source and Messages rows (standalone only)
        bool syncOffsetAlways = false;   // show Sync offset even while Source is Internal
    };

    explicit StandardSettingsOverlay(mu_core::ShellBackend& backend, Options options);
    explicit StandardSettingsOverlay(mu_core::ShellBackend& backend) : StandardSettingsOverlay(backend, Options{}) {}

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
        std::function<bool()> shown;                                // optional: false leaves the section (and an emptied group) out
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

    // The status-bar line for the controller lamp on hover (name, value); the editor shell forwards it.
    std::function<void(const juce::String& name, const juce::String& value)> onStatusUpdate;

    // How the Controller group shows a status: the lamp colour and brightness, the read-only text and the button
    // label (the action it performs). Pure, so the mapping can be tested without a display.
    struct ControllerLook { juce::Colour colour; float lit; juce::String text; juce::String button; };
    static ControllerLook controllerLook(mu_core::ControlStatus status);

    void visibilityChanged() override;

protected:
    mu_core::ShellBackend& proc;
    const bool isStandalone;
    const Options opts;

private:
    // Standard controls.
    KnobWithLabel  masterVolKnob { "Master Vol", MuLookAndFeel::knobLevel };
    juce::Label    uiSizeLabel;
    SegmentControl uiSizeCtrl { { "Medium", "Large" } };
    juce::ToggleButton updateCheckToggle { "Check for updates" };
    juce::Label    bpmLabel;
    NudgeInput     bpmInput { "BPM", 20, 300, 120 };
    juce::Label    clockSourceLabel, midiMessagesLabel, syncOffsetLabel, timeSigLabel;
    NudgeInput     syncOffsetInput { "ms", mu_core::ShellBackend::kSyncOffsetMinMs, mu_core::ShellBackend::kSyncOffsetMaxMs, 0 };
    DropdownSelect timeSigDropdown;
    DropdownSelect clockSourceDropdown, midiMessagesDropdown;
    void updateMidiSyncVisibility();

    // Controller group (standalone, only while the shell has attached a controller): re-read on show and on every
    // status change, because the shell attaches it after the editor is built.
    juce::Label       controllerLampLabel, controllerButtonLabel;
    StatusLamp        controllerLamp;
    juce::TextButton  controllerBtn;
    mu_core::AttachmentWatch controllerWatch;
    void setUpControllerGroup();
    void refreshController();
    bool controllerShown() const { return controllerWatch.get() != nullptr; }
    juce::TextButton midiPresetsBtn, fullPresetsBtn;   // program-change tables (added on request)

    std::vector<Section> general, midiBefore, midiClock, midiAfter;
    struct GroupSection { juce::String group; Section section; };
    std::vector<GroupSection> extraGroups;

    // Filled by layoutContent, drawn by paintContent.
    struct Heading { int x, w, y; juce::String title; bool group; };
    std::vector<Heading> headings;
    std::vector<std::pair<const Section*, Rows>> placed;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StandardSettingsOverlay)
};

} // namespace mu_ui
