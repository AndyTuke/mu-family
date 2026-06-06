#pragma once

#include "ServerEngine.h"
#include "../Ipc/MuLinkServerMemory.h"
#include "../Clock/MidiClockOut.h"

#include <juce_audio_devices/juce_audio_devices.h>

// AudioServer — owns the one hardware output device and runs the single audio callback
// that drives the whole mu-link bus. It is a thin shell: each callback it hands the
// device's output buffer to ServerEngine (publish transport → sum client rings → advance
// clock) and emits this block's MIDI-clock pulses. All the sacred, lock-free real-time
// work lives in ServerEngine; this class only manages device lifetime + wiring.
//
// Backend is runtime-selectable through the standard JUCE AudioDeviceManager (WASAPI
// shared/exclusive, DirectSound, or ASIO when the SDK is present) — exposed via
// audioDeviceManager() so the GUI (L3) can host a device picker.
namespace mu_link
{

class AudioServer : private juce::AudioIODeviceCallback
{
public:
    AudioServer() { engine.attachMemory(&mem); }
    ~AudioServer() override { stop(); }

    AudioServer(const AudioServer&)            = delete;
    AudioServer& operator=(const AudioServer&) = delete;

    // Create the shared-memory bus and open the default output device (0 in / 2 out).
    // Returns false only if the bus can't be created; a device that fails to open is
    // non-fatal — the callback is registered regardless, so a device chosen later via
    // the GUI's picker starts driving the bus with no restart.
    bool start(double preferredTempo = 120.0)
    {
        if (started)
            return true;
        if (! mem.create())
            return false;

        engineTempo = preferredTempo;
        deviceManager.initialiseWithDefaultDevices(0, 2);   // selector can repair/change later
        deviceManager.addAudioCallback(this);
        started = true;
        return true;
    }

    void stop()
    {
        if (! started)
            return;
        deviceManager.removeAudioCallback(this);
        deviceManager.closeAudioDevice();
        started = false;
    }

    bool isRunning() const noexcept { return started; }

    // Device picker / metering host for the GUI (L3).
    juce::AudioDeviceManager& audioDeviceManager() noexcept { return deviceManager; }
    const ServerEngine&       serverEngine() const noexcept { return engine; }
    MuLinkServerMemory&       sharedMemory() noexcept       { return mem; }
    ClientRegistry&           registry() noexcept           { return mem.registry(); }

    // Live levels for the GUI meters (linear peak, 0–1).
    float clientPeak(int slot) const noexcept { return engine.clientPeak(slot); }
    float masterPeak()         const noexcept { return engine.masterPeak(); }

    // Per-client mixer strip + master, driven by the GUI.
    void setClientGain(int slot, float g) noexcept { engine.setClientGain(slot, g); }
    void setClientPan (int slot, float p) noexcept { engine.setClientPan(slot, p); }
    void setClientMute(int slot, bool m)  noexcept { engine.setClientMute(slot, m); }
    void setClientSolo(int slot, bool s)  noexcept { engine.setClientSolo(slot, s); }
    void setMasterGain(float g)           noexcept { engine.setMasterGain(g); }
    float clientGainValue(int slot) const noexcept { return engine.clientGainValue(slot); }
    bool  clientMuted    (int slot) const noexcept { return engine.clientMuted(slot); }
    bool  clientSoloed   (int slot) const noexcept { return engine.clientSoloed(slot); }
    float masterGainValue()         const noexcept { return engine.masterGainValue(); }

    void setTempo(double bpm) noexcept           { engine.setTempo(bpm); }
    void setPlaying(bool shouldPlay) noexcept    { engine.setPlaying(shouldPlay); }
    void setMidiClockOutput(juce::MidiOutput* o) noexcept { midiClock.setOutput(o); }

private:
    void audioDeviceAboutToStart(juce::AudioIODevice* device) override
    {
        engine.prepare(device->getCurrentSampleRate(),
                       device->getCurrentBufferSizeSamples(),
                       engineTempo);
        engine.setPlaying(true);
    }

    void audioDeviceStopped() override {}

    void audioDeviceIOCallbackWithContext(const float* const* /*inputs*/, int /*numInputs*/,
                                          float* const* outputChannelData, int numOutputChannels,
                                          int numSamples,
                                          const juce::AudioIODeviceCallbackContext& /*context*/) override
    {
        const BlockStats stats = engine.renderBlock(outputChannelData, numOutputChannels, numSamples);
        midiClock.emit(stats.midiPulsesInBlock);
    }

    juce::AudioDeviceManager deviceManager;
    MuLinkServerMemory       mem;
    ServerEngine             engine;
    MidiClockOut             midiClock;
    double                   engineTempo = 120.0;
    bool                     started     = false;
};

} // namespace mu_link
