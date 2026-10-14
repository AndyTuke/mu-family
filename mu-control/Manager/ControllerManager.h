#pragma once

#include "Control/ControlAttachment.h"
#include "Driver/ControllerBackend.h"
#include "Manager/ManagedPorts.h"
#include "Surface/SurfaceModel.h"
#include <functional>
#include <juce_events/juce_events.h>
#include <memory>

// Runs one control surface for the standalone shell: finds the device by port name without opening it, puts it
// in its host mode only while Performance mode is on, keeps its lights in step with the LiveView, turns pad
// presses into ControlActions for the sink, and gives the device back (own mode, pads dark) on every way out:
// leaving the mode, Release, destruction and closeNow(). Message thread only; the MIDI and hot-plug threads
// reach it only through ManagedPorts. It names no processor, editor or product.
namespace mu_control
{

class ControllerManager : public mu_core::ControlAttachment
{
public:
    ControllerManager(mu_core::ControlSink& sink, const mu_core::LiveView& view, const mu_core::SurfaceLook& look,
                      const juce::File& settingsFolder,
                      ControllerBackendFactory backendFactory = [] { return makeDefaultBackend(); });
    ~ControllerManager() override;

    ControllerManager(const ControllerManager&) = delete;
    ControllerManager& operator=(const ControllerManager&) = delete;

    mu_core::ControlStatus getStatus() const override;
    juce::String           getDeviceLabel() const override { return deviceLabel; }
    void                   release() override;
    void                   connect() override;
    void                   closeNow() override;

    // Looks at the port lists again (the 1 Hz timer calls this while the device is not in use) and opens the
    // device if Performance mode is on and it has appeared.
    void rescan();

    // For tests: which timers run.
    bool isBlinkTimerRunning() const { return blinkTick.isTimerRunning(); }
    bool isScanTimerRunning() const  { return scanTick.isTimerRunning(); }

private:
    struct Tick : juce::Timer
    {
        std::function<void()> fn;
        void timerCallback() override { if (fn) fn(); }
    };

    bool wantOpen() const;
    bool detect();               // reads the port names into found / deviceLabel; true when found
    void reconcile();            // open or close to match the mode, then render and report
    void openDevice();
    void closeDevice();          // pads dark, own mode back, ports closed; no notification
    void refresh();              // render the frame; run the blink timer only while something pulses
    void onConnection(bool connected);
    void publish();              // tell listeners when the status or label changed

    mu_core::ControlSink&     sink;
    const mu_core::LiveView&  view;
    ControllerBackend         backend;
    ManagedPorts              ports;
    SurfaceModel              model;

    bool                      found    = false;
    bool                      released = false;
    bool                      lost     = false;
    bool                      quitting = false;
    juce::String              deviceLabel;
    mu_core::ControlStatus    lastStatus = mu_core::ControlStatus::NotFound;
    juce::String              lastLabel;

    Tick scanTick, blinkTick;
};

} // namespace mu_control
