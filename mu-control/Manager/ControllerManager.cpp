#include "Manager/ControllerManager.h"

namespace mu_control
{

namespace
{
constexpr int kScanMs  = 1000;   // looking for an absent device
constexpr int kBlinkMs = 33;     // about 30 Hz while a pad pulses

// The first name matching a wildcard, or empty.
juce::String firstMatch(const juce::StringArray& names, const juce::String& pattern)
{
    for (const auto& n : names) if (n.matchesWildcard(pattern, true)) return n;
    return {};
}

// The default mapping for the device, with any overrides a user saved earlier (there is no editor for them yet).
SurfaceMapping mappingFor(const ControllerBackend& b, const juce::File& settingsFolder)
{
    SurfaceMapping mapping(b.driver->describe());
    if (settingsFolder != juce::File())
    {
        const auto file = settingsFolder.getChildFile("mu-controller-mapping.xml");
        if (file.existsAsFile()) mapping.restoreFromXml(file.loadFileAsString());
    }
    return mapping;
}
}

ControllerManager::ControllerManager(mu_core::ControlSink& s, const mu_core::LiveView& v, const mu_core::SurfaceLook& look,
                                     const juce::File& settingsFolder, ControllerBackendFactory backendFactory)
    : sink(s), view(v), backend(backendFactory()), ports(*backend.ports),
      model(s, v, mappingFor(backend, settingsFolder), look)
{
    // Decoded presses arrive on the message thread (ManagedPorts moved them there) and go to the sink.
    backend.driver->onEvent = [this](const SurfaceEvent& e) { if (backend.driver->isOpen()) model.handle(e); };
    ports.connectionObserver = [this](bool connected) { onConnection(connected); };
    model.onChanged = [this] { reconcile(); };

    scanTick.fn  = [this] { rescan(); };
    blinkTick.fn = [this] { refresh(); };

    detect();
    lastStatus = getStatus();
    lastLabel = deviceLabel;
    scanTick.startTimer(kScanMs);
    reconcile();   // the mode may already be on
}

ControllerManager::~ControllerManager()
{
    scanTick.stopTimer();
    blinkTick.stopTimer();
    closeDevice();   // own mode back whatever state we were in
}

void ControllerManager::closeNow()
{
    // The quit hook: pads dark and the device's own mode back, then nothing may reopen it.
    quitting = true;
    scanTick.stopTimer();
    blinkTick.stopTimer();
    closeDevice();
    publish();
}

mu_core::ControlStatus ControllerManager::getStatus() const
{
    using S = mu_core::ControlStatus;
    if (backend.driver->isOpen()) return lost ? S::Lost : S::InUse;
    if (released || quitting) return S::Released;
    return found ? S::Found : S::NotFound;
}

bool ControllerManager::wantOpen() const
{
    return ! quitting && ! released && view.isPerformanceMode();
}

bool ControllerManager::detect()
{
    // Port names only; nothing is opened.
    const auto name = firstMatch(ports.outputNames(), backend.detect.outputPattern);
    const bool hasInput = firstMatch(ports.inputNames(), backend.detect.inputPattern).isNotEmpty();
    found = name.isNotEmpty() && hasInput;
    deviceLabel = found ? name : juce::String();
    return found;
}

void ControllerManager::rescan()
{
    if (backend.driver->isOpen()) return;   // in use: the ports report a vanished device themselves
    detect();
    reconcile();
}

void ControllerManager::reconcile()
{
    if (quitting) return;

    const bool want = wantOpen();
    if (want && ! backend.driver->isOpen()) openDevice();
    else if (! want && backend.driver->isOpen()) closeDevice();

    refresh();
    publish();
}

void ControllerManager::openDevice()
{
    if (! detect()) return;
    if (! backend.driver->open(ports)) return;

    lost = false;
    scanTick.stopTimer();   // nothing to look for while it is ours
}

void ControllerManager::closeDevice()
{
    blinkTick.stopTimer();
    const bool wasOpen = backend.driver->isOpen();
    backend.driver->close();
    lost = false;
    if (! wasOpen) return;

    detect();
    if (! quitting) scanTick.startTimer(kScanMs);
}

void ControllerManager::refresh()
{
    if (! backend.driver->isOpen()) { blinkTick.stopTimer(); return; }

    backend.driver->setLeds(model.render());

    // Pulsing pads are animated here, so the fast timer runs only while one is lit.
    if (model.isAnimating()) { if (! blinkTick.isTimerRunning()) blinkTick.startTimer(kBlinkMs); }
    else blinkTick.stopTimer();
}

void ControllerManager::onConnection(bool connected)
{
    if (! backend.driver->isOpen()) return;
    lost = ! connected;
    if (connected) refresh();   // the driver repeated its setup and last frame; this brings the frame up to date
    publish();
}

void ControllerManager::release()
{
    if (quitting) return;
    const bool wasOpen = backend.driver->isOpen();
    if (wasOpen) closeDevice();
    if (wasOpen || found) released = true;
    publish();
}

void ControllerManager::connect()
{
    // An explicit Connect undoes an earlier closeNow(): a quit that was started and then cancelled (the Save dialog) must not
    // leave the pad dead until restart.
    quitting = false;
    released = false;
    if (backend.driver->isOpen() && lost) closeDevice();   // a dead link: start over
    detect();
    reconcile();
}

void ControllerManager::publish()
{
    const auto status = getStatus();
    if (status == lastStatus && deviceLabel == lastLabel) return;
    lastStatus = status;
    lastLabel = deviceLabel;
    notifyStatusChanged();
}

} // namespace mu_control
