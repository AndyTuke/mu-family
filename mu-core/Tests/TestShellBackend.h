#pragma once

#include "Plugin/ShellBackend.h"

// A ShellBackend with no processor behind it: plain fields the tests set and read, so the shared
// chrome (TransportBar, the settings page) is exercised over the interface alone, the way an app
// without an AudioProcessor drives it.
struct TestShellBackend : mu_core::ShellBackend
{
    explicit TestShellBackend(bool standalone = true) : standalone(standalone) {}

    bool   standalone;
    bool   playing = false;
    double bpm = 120.0, beat = 0.0;
    int    sigNum = 4, sigDen = 4;
    int    nudge = 0;
    int    nudgeSets = 0;               // how many times setNudgeDirection was called
    bool   syncEnabled = false;
    int    syncMessages = 2;
    double clockBpm = 0.0;
    bool   clockPlaying = false;
    MidiClockSync::ClockState clockState = MidiClockSync::ClockState::Off;
    int    syncOffset = 0;
    float  uiScale = kUiScaleMedium;
    int    uiScaleSets = 0;
    juce::File   presetsDir;
    juce::String presetExt { "muTest" };
    bool   pendingFull = false;

    // A host, for the plugin case.
    bool   hostPlay = false;
    double hostPpq = 0.0;
    bool   hostHasPpq = false;

    bool   isStandaloneApp() const override { return standalone; }
    bool   isInternalPlaying() const override { return playing; }
    void   toggleInternalPlay() override { playing = ! playing; if (! playing) beat = 0.0; }
    double getInternalBpm() const override { return bpm; }
    void   setInternalBpm(double b) override { bpm = b; }
    double getInternalBeatPos() const override { return beat; }
    void   getTimeSignature(int& n, int& d) const override { n = sigNum; d = sigDen; }
    void   setTimeSignature(int n, int d) override { sigNum = n; sigDen = d; }
    void   setNudgeDirection(int dir) override { nudge = dir; ++nudgeSets; }

    bool   getMidiSyncEnabled() const override { return syncEnabled; }
    int    getMidiSyncMessages() const override { return syncMessages; }
    double getMidiClockBpm() const override { return clockBpm; }
    bool   isMidiClockPlaying() const override { return clockPlaying; }
    MidiClockSync::ClockState getMidiClockState() const override { return clockState; }
    void   setMidiSyncEnabled(bool on) override { syncEnabled = on; }
    void   setMidiSyncMessages(int mode) override { syncMessages = mode; }
    int    getSyncOffsetMs() const override { return syncOffset; }
    void   setSyncOffsetMs(int ms) override { syncOffset = ms; }

    // Host reads: overridden only so a test can play a host; the defaults are "no host".
    bool   isHostPlaying() const override { return hostPlay; }
    bool   getHostPpqPosition(double& ppq) const override { if (! hostHasPpq) return false; ppq = hostPpq; return true; }

    float  getUiScale() const override { return uiScale; }
    void   setUiScale(float s) override { uiScale = s; ++uiScaleSets; if (onUiScaleChanged) onUiScaleChanged(s); }

    juce::File   getPresetsDir() const override { return presetsDir; }
    juce::String getFullPresetExtension() const override { return presetExt; }
    bool         hasPendingFullPreset() const override { return pendingFull; }

    // Presets: every operation is recorded so a test can check the order and the arguments.
    juce::StringArray categories;
    juce::File        defaultPreset, contentDir;
    juce::StringArray log;                 // "load:<file name>", "save:<name>", "ensure:<category>"
    juce::File        lastLoaded;
    void              loadPreset(const juce::File& f) override { lastLoaded = f; log.add("load:" + f.getFileName()); }
    void              savePreset(const juce::String& name, const juce::String&, const juce::String&, bool) override
    {
        log.add("save:" + name);
        presetsDir.getChildFile(name + "." + presetExt).replaceWithText("<Preset/>");
    }
    juce::StringArray loadCategoryList() const override { return categories; }
    void              ensureCategoryInList(const juce::String& c) override { log.add("ensure:" + c); }
    juce::File        getDefaultPresetFile() const override { return defaultPreset; }
    juce::File        getContentDir() const override { return contentDir; }
};

// A bare backend that overrides none of the defaulted members, to prove their "no host" defaults.
struct MinimalShellBackend : TestShellBackend
{
    using TestShellBackend::TestShellBackend;
    bool isHostPlaying() const override { return mu_core::ShellBackend::isHostPlaying(); }
    bool getHostPpqPosition(double& ppq) const override { return mu_core::ShellBackend::getHostPpqPosition(ppq); }
};
