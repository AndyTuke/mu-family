#pragma once

#include "Plugin/ShellBackend.h"
#include "Plugin/TransportResolver.h"   // kNudgeBendPercent
#include "Link/MuLinkProtocol.h"        // readTransport
#include "../Server/AudioServer.h"

#include <algorithm>
#include <atomic>
#include <functional>

// LinkShellBackend — mu-link's implementation of the shared chrome's app seam (mu_core::ShellBackend)
// over the AudioServer, so the family TransportBar, settings page and preset flow drive mu-link the
// way they drive a plugin's processor. mu-link is always a standalone app and the master:
//   - the internal transport IS the master TransportClock (play and BPM go to the server);
//   - the clock source is Internal or MIDI In (external MIDI clock), with Clock + Transport taken
//     from the master, so the messages mode is fixed;
//   - the meter is a fixed 4/4;
//   - there is no host: the host reads keep their "no host" defaults and are never called;
//   - presets are Documents/TDP/muLink/Presets with the extension muLink; the app state itself (the
//     mixer and scenes) lives in the window, so load and save are handed to it through two hooks.
// Message thread only. Header-only so mu-link-tests can drive it without the GUI.
namespace mu_link
{

class LinkShellBackend : public mu_core::ShellBackend
{
public:
    // `root` is where mu-link keeps its files: Presets/ and the settings file. The default is
    // Documents/TDP/muLink.
    explicit LinkShellBackend(AudioServer& serverToDrive, juce::File root = defaultRoot())
        : server(serverToDrive), rootDir(std::move(root)), settingsFile(rootDir.getChildFile("settings.xml"))
    {
        loadSettings();
    }

    static juce::File defaultRoot()
    {
        return juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
                   .getChildFile("TDP").getChildFile("muLink");
    }

    // Set by the window, which owns the app state (mixer, scenes): a preset file to apply, and a
    // preset to write under the given name, description and category.
    std::function<void(const juce::File&)> onLoadPresetFile;
    std::function<void(const juce::String& name, const juce::String& desc, const juce::String& category)> onSavePreset;

    // Clock source for the lamp: a source of milliseconds, replaceable by a test.
    std::function<double()> nowMs = [] { return juce::Time::getMillisecondCounterHiRes(); };

    // How long without a clock pulse before the MIDI In clock counts as lost (matches the server's watchdog).
    static constexpr double kClockLostMs = 500.0;

    //==========================================================================
    bool isStandaloneApp() const noexcept override { return true; }

    // ─── Transport: the master clock ─────────────────────────────────────────
    bool isInternalPlaying() const noexcept override
    {
        return external() ? server.externalRunning() : playRequested;
    }

    // Stopping rewinds, so the next Play starts from the top (the products' internal transport does the same).
    void toggleInternalPlay() override
    {
        playRequested = ! playRequested;
        server.setPlaying(playRequested);
        if (! playRequested)
            server.rewind();
    }

    double getInternalBpm() const noexcept override { return bpmRequested; }

    void setInternalBpm(double bpm) override
    {
        bpmRequested = std::clamp(bpm, 20.0, 300.0);
        pushTempo();
    }

    // The beat at the start of the last rendered block; 0 until the bus is up.
    double getInternalBeatPos() const noexcept override
    {
        if (! server.hasBus()) return 0.0;
        return readTransport(server.sharedMemory().transport()).ppqPosition;
    }

    void getTimeSignature(int& numerator, int& denominator) const noexcept override { numerator = 4; denominator = 4; }
    void setTimeSignature(int, int) override {}   // fixed 4/4

    // While a nudge button is held the master tempo bends, as the products' internal transport does.
    void setNudgeDirection(int direction) override
    {
        nudge = std::clamp(direction, -1, 1);
        pushTempo();
    }

    // ─── Clock sync: Internal / MIDI In ──────────────────────────────────────
    bool getMidiSyncEnabled() const noexcept override { return external(); }
    void setMidiSyncEnabled(bool on) override
    {
        server.setClockSource(on ? ClockSource::ExternalMidi : ClockSource::Internal);
        lastPulseCount = server.externalPulseCount();
        lastPulseMs    = nowMs();
    }

    // Under MIDI In the master follows the incoming clock and transport, so the mode is fixed.
    int  getMidiSyncMessages() const noexcept override { return 2; }   // Clock + Transport
    void setMidiSyncMessages(int) override {}

    double getMidiClockBpm()    const noexcept override { return server.externalBpm(); }
    bool   isMidiClockPlaying() const noexcept override { return server.externalRunning(); }

    // Waiting until a pulse is heard, Locked while pulses arrive and the beat grid is on the clock's,
    // Drifting while the server's phase servo is still pulling it back, Lost once the pulses stop.
    MidiClockSync::ClockState getMidiClockState() const noexcept override
    {
        using S = MidiClockSync::ClockState;
        if (! external()) return S::Off;

        const auto count = server.externalPulseCount();
        const double now = nowMs();
        if (count != lastPulseCount) { lastPulseCount = count; lastPulseMs = now; }
        if (count == 0) return S::Waiting;
        if (now - lastPulseMs > kClockLostMs) return S::Lost;
        return server.externalDrifting() ? S::Drifting : S::Locked;
    }

    int  getSyncOffsetMs() const noexcept override { return server.syncOffsetMs(); }
    void setSyncOffsetMs(int ms) override
    {
        server.setSyncOffsetMs(std::clamp(ms, kSyncOffsetMinMs, kSyncOffsetMaxMs));
        saveSettings();
    }

    // ─── Latency ─────────────────────────────────────────────────────────────
    // How far ahead each app renders: 0 Low, 1 Normal, 2 High. Kept with the settings, not the presets.
    int  getLatencyIndex() const noexcept { return latencyIndex; }
    void setLatencyIndex(int index) { latencyIndex = std::clamp(index, 0, 2); saveSettings(); }

    // ─── Display ─────────────────────────────────────────────────────────────
    float getUiScale() const override { return uiScale; }
    void  setUiScale(float scale) override
    {
        uiScale = scale;
        saveSettings();
        if (onUiScaleChanged) onUiScaleChanged(scale);
    }

    // ─── Presets ─────────────────────────────────────────────────────────────
    juce::File   getPresetsDir()          const override { return rootDir.getChildFile("Presets"); }
    juce::String getFullPresetExtension() const override { return "muLink"; }
    juce::File   getContentDir()          const override { return rootDir; }
    juce::File   getDefaultPresetFile()   const override { return getPresetsDir().getChildFile("_default.muLink"); }

    void loadPreset(const juce::File& file) override
    {
        if (onLoadPresetFile) onLoadPresetFile(file);
        else if (onLoadError) onLoadError("mu-link cannot load " + file.getFileName());
    }

    void savePreset(const juce::String& name, const juce::String& desc,
                    const juce::String& category, bool /*embedSamples*/) override
    {
        if (onSavePreset) onSavePreset(name, desc, category);
    }

    // The categories used by the saved presets (the root element's presetCategory attribute).
    juce::StringArray loadCategoryList() const override
    {
        juce::StringArray categories;
        for (const auto& entry : juce::RangedDirectoryIterator(getPresetsDir(), false, "*.muLink"))
            if (auto xml = juce::parseXML(entry.getFile()))
            {
                const auto category = xml->getStringAttribute("presetCategory");
                if (category.isNotEmpty()) categories.addIfNotAlreadyThere(category);
            }
        categories.sort(true);
        return categories;
    }
    void ensureCategoryInList(const juce::String&) override {}   // derived from the saved files

private:
    bool external() const { return server.clockSourceMode() == ClockSource::ExternalMidi; }

    // The master tempo the clock should run at: the BPM field, bent while a nudge button is held.
    void pushTempo()
    {
        server.setTempo(bpmRequested * (1.0 + (double) nudge * mu_core::kNudgeBendPercent / 100.0));
    }

    void loadSettings()
    {
        if (auto xml = juce::parseXML(settingsFile))
        {
            uiScale = (float) xml->getDoubleAttribute("uiScale", (double) kUiScaleMedium);
            server.setSyncOffsetMs(std::clamp(xml->getIntAttribute("syncOffsetMs", 0), kSyncOffsetMinMs, kSyncOffsetMaxMs));
            latencyIndex = std::clamp(xml->getIntAttribute("latency", 1), 0, 2);
        }
    }

    void saveSettings() const
    {
        juce::XmlElement xml("MuLinkSettings");
        xml.setAttribute("uiScale", (double) uiScale);
        xml.setAttribute("syncOffsetMs", server.syncOffsetMs());
        xml.setAttribute("latency", latencyIndex);
        rootDir.createDirectory();
        xml.writeTo(settingsFile);
    }

    AudioServer& server;
    juce::File   rootDir, settingsFile;

    bool   playRequested = false;
    double bpmRequested  = 120.0;
    int    nudge         = 0;
    float  uiScale       = kUiScaleMedium;
    int    latencyIndex  = 1;   // Low / Normal / High: how far ahead each app renders (kept with the settings, not the presets)

    // Lamp bookkeeping, updated by the const state query.
    mutable std::uint64_t lastPulseCount = 0;
    mutable double        lastPulseMs    = 0.0;
};

} // namespace mu_link
