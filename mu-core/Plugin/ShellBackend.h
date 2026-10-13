#pragma once

#include <juce_core/juce_core.h>

#include "Plugin/MidiClockSync.h"   // MidiClockSync::ClockState

#include <functional>

namespace mu_core
{

class LiveView;      // Control/LiveView.h
class ControlSink;   // Control/ControlSink.h

// Everything the shared shell chrome (TransportBar, the settings page, ...) reads from or tells
// the app. The chrome depends on this small interface, never on an AudioProcessor, so a product
// processor (ProcessorBase implements it) and an app with no processor (mu-link) drive the same
// UI. Header-only and JUCE-core only, so a target that does not link the mu-core INTERFACE
// library can include it.
class ShellBackend
{
public:
    virtual ~ShellBackend() = default;

    // True for a standalone app: the bar then shows the internal transport (BPM, Play, Tap,
    // Nudge) and the position of the internal clock. False for a plugin, whose host owns the
    // transport (the bar then mirrors the host).
    virtual bool isStandaloneApp() const noexcept = 0;

    // The getters marked noexcept are the ones the audio thread reads (the block's transport and
    // sync reads), so an override must not throw or allocate.
    // ─── Transport ───────────────────────────────────────────────────────────
    // Internal transport: a free-running clock the app advances while no host / MIDI clock
    // drives it. Stopping resets the beat.
    virtual bool   isInternalPlaying()  const noexcept = 0;
    virtual void   toggleInternalPlay()       = 0;
    virtual double getInternalBpm()     const noexcept = 0;
    virtual void   setInternalBpm(double bpm) = 0;
    virtual double getInternalBeatPos() const noexcept = 0;

    // The standalone transport's meter: changes the displayed bar.beat.sixteenth only.
    virtual void   getTimeSignature(int& numerator, int& denominator) const noexcept = 0;
    virtual void   setTimeSignature(int numerator, int denominator)         = 0;

    // Nudge buttons (standalone): -1 slower, +1 faster while held, 0 released.
    virtual void   setNudgeDirection(int direction) = 0;

    // ─── Clock sync (standalone) ─────────────────────────────────────────────
    virtual bool   getMidiSyncEnabled()  const noexcept = 0;
    virtual int    getMidiSyncMessages() const noexcept = 0;   // 0 = clock only, 1 = transport only, 2 = both
    virtual double getMidiClockBpm()     const noexcept = 0;
    virtual bool   isMidiClockPlaying()  const noexcept = 0;
    virtual MidiClockSync::ClockState getMidiClockState() const noexcept = 0;
    virtual void   setMidiSyncEnabled(bool on)    = 0;
    virtual void   setMidiSyncMessages(int mode)  = 0;

    // Sync offset: how many ms the app plays EARLIER than the external clock says, to cancel
    // the audio output latency.
    static constexpr int kSyncOffsetMinMs = -50, kSyncOffsetMaxMs = 250;
    virtual int    getSyncOffsetMs() const noexcept = 0;
    virtual void   setSyncOffsetMs(int ms) = 0;

    // ─── Host transport (plugins) ────────────────────────────────────────────
    // Defaulted to "no host": an app without one never overrides them.
    virtual bool   isHostPlaying() const noexcept { return false; }
    virtual void   getHostTimeSignature(int& numerator, int& denominator) const noexcept { numerator = 4; denominator = 4; }
    virtual bool   getHostBarStartPpq(double& ppq) const noexcept { juce::ignoreUnused(ppq); return false; }
    virtual bool   getHostPpqPosition(double& ppq) const noexcept { juce::ignoreUnused(ppq); return false; }

    // ─── Display ─────────────────────────────────────────────────────────────
    static constexpr float kUiScaleMedium = 1.0f;
    static constexpr float kUiScaleLarge  = 1.25f;
    virtual float  getUiScale() const = 0;
    virtual void   setUiScale(float scale) = 0;

    // ─── Master level (the settings page's Audio row) ────────────────────────
    // Linear gain 0..1. A backend with no master level leaves hasMasterLevel false and the page
    // shows no master-volume row.
    virtual bool   hasMasterLevel() const { return false; }
    virtual float  getMasterLevel() const { return 1.0f; }
    virtual void   setMasterLevel(float level) { juce::ignoreUnused(level); }

    // ─── Presets (what the transport bar's preset dropdown reads) ────────────
    virtual juce::File   getPresetsDir()          const = 0;
    virtual juce::String getFullPresetExtension() const = 0;
    // True while a full preset is staged for the next loop point.
    virtual bool         hasPendingFullPreset()   const { return false; }

    // What the shell's Load / Save / New flow drives. loadPreset reports failures through
    // onLoadError; savePreset writes <presets dir>/<name>.<extension>; the category list is the
    // shared one the Save card offers; the default preset is the file "Save as Default" writes
    // and "New" restores.
    virtual void              loadPreset(const juce::File& file) = 0;
    virtual void              savePreset(const juce::String& name, const juce::String& desc,
                                         const juce::String& category, bool embedSamples) = 0;
    virtual juce::StringArray loadCategoryList() const = 0;
    virtual void              ensureCategoryInList(const juce::String& category) = 0;
    virtual juce::File        getDefaultPresetFile() const = 0;

    // Where the app keeps its presets, samples and keybindings.json.
    virtual juce::File        getContentDir() const = 0;

    // ─── Performance mode (defaulted: an app without it changes nothing) ─────
    // What the chrome reads and drives for the gig view. The chrome shows the mode only when
    // supportsPerformanceMode() is true and both pointers are set; the pointees outlive the shell.
    virtual const LiveView* getLiveView()    const { return nullptr; }
    virtual ControlSink*    getControlSink()       { return nullptr; }
    virtual bool            supportsPerformanceMode() const { return false; }

    // ─── Shell callbacks (message thread, set by the shell) ──────────────────
    // The shell MUST clear these in its destructor: the backend can outlive it, and a deferred
    // call into a destroyed shell is a use-after-free.
    std::function<void(std::function<void()>)>       onSaveAndQuit;
    std::function<void(const juce::String& message)> onLoadError;
    std::function<void()>                            onPresetSwapCommitted;
    std::function<void(float)>                       onUiScaleChanged;
};

} // namespace mu_core
