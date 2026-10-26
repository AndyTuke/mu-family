#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "Plugin/ShellBackend.h"
#include "UI/Components/NudgeInput.h"
#include "UI/Components/DropdownSelect.h"
#include "UI/Components/MuLookAndFeel.h"
#include "UI/Components/StatusLamp.h"
#include "Plugin/TapTempo.h"
#include "Plugin/TransportResolver.h"   // kNudgeBendPercent
#include "Control/ControlSink.h"
#include "Control/AttachmentWatch.h"
#include "Control/LiveView.h"
#include "UI/Components/StepIndicator.h"
#include "UI/PerformanceIndication.h"   // HoldTracker

// Shared mu-family transport bar — play / BPM / position / preset dropdown /
// new / save / gear / mixer toggle. Plugin-agnostic: takes a mu_core::ShellBackend and reads
// transport + clock + preset state through that interface (a ProcessorBase is one).
//
// The bar reserves a central "loop section" slot between the transport pane
// and the preset dropdown that a product can fill with its own component
// (mu-clid's MasterLoopSection sits here). Lite-style builds without preset /
// mixer chrome opt out via setShowPresetControls(false) + setShowMixerToggle(false).
class TransportBar : public juce::Component,
                     private juce::Timer
{
public:
    explicit TransportBar(mu_core::ShellBackend& backend);
    ~TransportBar() override;

    // Follows the shell's controller attachment (attached after the editor is built) and puts its status on the
    // status line: Connected and Released for a few seconds, Lost until it clears. The refresh timer calls this.
    void syncController();

    std::function<void()>                      onMixerToggle;
    std::function<void()>                      onLogoClicked;
    std::function<void(const juce::File&)>     onPresetSelected;
    std::function<void()>                      onSavePreset;
    std::function<void()>                      onNewPreset;
    std::function<void()>                      onSettingsToggle;
    // status-bar coverage for the chrome (BPM input). Editor wires this to the
    // global StatusBar; no per-channel colour tag (those come from the panels).
    std::function<void(const juce::String& name, const juce::String& value)> onStatusUpdate;

    // ─── Product-supplied chrome ────────────────────────────────────────────
    void setLogoText(const juce::String& text);
    // When false, hides the preset dropdown + new/save buttons + staging badge.
    // Default true.
    void setShowPresetControls(bool show);
    // When false, hides the mixer-toggle button. Default true.
    void setShowMixerToggle(bool show);
    // When false, hides the Tap button and the two Nudge buttons (a product with no tempo of its own
    // to tap or bend). The clock-lamp slot stays, so the lamp still shows under a MIDI clock. Default true.
    void setShowTapAndNudge(bool show);
    // When false, hides the gear/settings button. Default true.
    void setShowSettingsButton(bool show);
    // Embed a product-specific component (e.g. mu-clid's master-loop section)
    // between the transport pane and the preset dropdown. Pass `width = 0` or
    // `component = nullptr` to clear. The bar parents the component and lays
    // it out in `loopPaneBounds`.
    void setLoopSection(juce::Component* component, int width);

    void refreshPresets();
    // Re-reads the internal tempo into the BPM field (after the app set it from a preset); standalone only.
    void refreshTempo();
    // keep dropdown displaying the loaded preset's name. Pass an invalid
    // File to revert to the "<unnamed preset>" placeholder.
    void setLoadedPreset(const juce::File& file);
    // Returns the currently loaded preset file, or an invalid File if none selected.
    juce::File getLoadedPresetFile() const;
    // Left x of the preset dropdown in TransportBar's own coordinate space
    // (equals editor x since TransportBar is always at x=0).
    int getPresetDropdownLeft() const noexcept;
    void setMixerActive(bool active);

    // ─── Performance mode (standalone; docs/design-ui-family.md section 12) ─────
    // Shown only when the backend supports the mode and has both a live view and a control sink.
    // In the mode the preset group is replaced by the StepIndicator, the Perf toggle is lit and a
    // rim runs along the bar's bottom edge. The mode itself lives in the live state; the bar only
    // asks for changes through the sink and re-reads the result.
    std::function<int()> getEditorLayer;   // the editor's current layer selection; seeds the mode on entry
    bool isPerformanceAvailable() const;
    bool isPerformanceMode() const noexcept { return modeShown; }
    // A click on Perf (or Ctrl+P) while the mode is off: enters it, or says to choose a performance first.
    bool enterPerformanceMode();
    // Set by the shell: with no performance chosen, entering opens the chooser instead of saying so.
    std::function<void()> onChoosePerformance;
    // Set by a product that imports other products' presets (mu-Tronic): adds an "Import product preset..." entry to the
    // preset dropdown, beside "Performances...". Call refreshPresets() after setting or clearing it.
    std::function<void()> onImportProductPreset;
    // Leaving: press and hold the lit Perf button for kPerfExitHoldMs. The caller supplies the clock;
    // the bar's timer ticks it at the UI refresh rate and release or drag-off cancels.
    void beginExitHold(double nowMs);
    void cancelExitHold();
    void clearHoldLine();   // takes the 'Hold to leave' status line down
    void tickExitHold(double nowMs);
    bool isExitHolding() const noexcept { return exitHold.isHolding(); }
    // Re-reads the live state and updates the toggle, the preset group and the indicator.
    void syncPerformance();
    StepIndicator&   getStepIndicator()  noexcept { return stepIndicator; }
    juce::Component& getPerformToggle()  noexcept { return perfBtn; }
    juce::Component& getPlayButton()     noexcept { return playBtn; }

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;

private:
    mu_core::ShellBackend& proc;
    const bool isStandalone;

    juce::String     logoText;
    bool             showPresetControls  = true;
    bool             showMixerToggle     = true;
    bool             showSettingsButton  = true;
    bool             showTapAndNudge     = true;
    juce::Component* loopSection        = nullptr;
    int              loopSectionWidth   = 0;

    juce::TextButton playBtn;
    NudgeInput       bpmInput { "BPM", 20, 300, 120 };

    juce::Label      posLabel;
    // The tap button and the clock lamp share one slot: the internal clock shows Tap, a MIDI
    // clock source shows the lamp, so the two are never visible together.
    juce::TextButton tapBtn { "Tap" };
    mu_core::TapTempo tapTempo;
    juce::TextButton nudgeSlowBtn { "-" }, nudgeFastBtn { "+" };   // momentary tempo bend (standalone)
    StatusLamp       clockLamp;              // MIDI clock locked / lost (standalone, ticks modes)
    bool             clockShown = false;
    MidiClockSync::ClockState shownClockState = MidiClockSync::ClockState::Off;
    int              shownPos = -2;   // bar/beat/sixteenth the label shows; -1 = "---"
    DropdownSelect   presetDropdown;
    juce::Label      presetStagingBadge;   // "SWP" pill shown on a pending full-preset hot-swap
    juce::TextButton newBtn   { "New" };
    juce::TextButton saveBtn  { "Save" };
    juce::TextButton gearBtn;
    juce::TextButton mixerBtn { "Mixer" };

    // The Perf toggle: a click enters; while lit, a press starts the exit hold, and release or
    // dragging off the button cancels it.
    struct PerfToggle : juce::TextButton
    {
        PerfToggle() : juce::TextButton("Perf") { setWantsKeyboardFocus(false); }
        std::function<void()> onHoldPress, onHoldCancel;
        bool suppressClick = false;   // set when the hold completed: the release must not re-enter
        void mouseDown(const juce::MouseEvent& e) override;
        void mouseUp(const juce::MouseEvent& e) override;
        void mouseDrag(const juce::MouseEvent& e) override;
    };
    PerfToggle       perfBtn;
    StepIndicator    stepIndicator;
    mu_ui::perf::HoldTracker exitHold { (double) MuLookAndFeel::kPerfExitHoldMs };
    bool             availableShown = false;
    bool             modeShown      = false;
    int              statusSerial   = 0;
    juce::String     shownQueued;
    mu_core::AttachmentWatch controllerWatch;
    mu_core::ControlStatus   shownControl = mu_core::ControlStatus::NotFound;
    bool             controlLostShown = false;   // the sticky 'Lost' status line is up
    void controllerStatusChanged();
    void clearLostLine();
    bool             holdLineShown  = false;   // the 'Hold to leave' status line is up
    uint32_t         shownRevision  = 0xffffffffu;   // the live revision and preset name the indicator was last built from
    juce::String     shownPresetName;

    void setExitFill(float progress);
    void refreshIndication();
    void showTimedStatus(const juce::String& name, const juce::String& value, int ms = MuLookAndFeel::kPerfStatusMs);

    // Dropdown id of the "Performances..." entry (preset ids are positions in the file list, far below this).
    static constexpr int kPerformancesItemId = 100000;
    // Dropdown id of the "Import product preset..." entry.
    static constexpr int kImportItemId = 100001;
    void updatePresetGroupVisibility();

    static constexpr int kLogoW      = 88;
    static constexpr int kPlayW      = 36;   // wider for clarity
    static constexpr int kBpmW       = 80;   // inline "BPM" label + "127.5" + arrows
    static constexpr int kPosW       = 56;
    static constexpr int kClockW     = 44;   // the Tap / MIDI clock lamp slot
    static constexpr int kNudgeW     = 22;   // each nudge button
    static constexpr int kNudgeGap   = 2;
    static constexpr int kPresetW    = 240;  // wider preset dropdown
    static constexpr int kNewW       = 36;
    static constexpr int kSaveW      = 44;
    static constexpr int kGearW      = 28;
    static constexpr int kMixerW     = 80;
    static constexpr int kGap        = 6;

    // Sub-pane bounds — computed in resized(), used in paint().
    juce::Rectangle<int> transportPaneBounds;
    juce::Rectangle<int> loopPaneBounds;

    std::vector<juce::File> presetFiles;

    void timerCallback() override;
    void refreshPlayBtn();
    void updateNudge();        // tell the audio thread which nudge buttons are held
    double shownClockBpm = 0.0;   // the eased MIDI-clock tempo shown in the BPM field (0 = none yet)
    void refreshClockLamp();   // the MIDI clock lamp's colour + status from the watchdog state
    void updatePositionLabel();
    void populatePresetDropdown();
};
