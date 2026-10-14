// ControllerManager against FakeDevicePorts, FakeLiveView and a recording sink: when the device is opened and
// given back, the status lamp states, release / connect, unplug and replug, and presses crossing threads.
// No hardware, no real MIDI port.

#include "Devices/LaunchpadX/LaunchpadXDriver.h"
#include "Devices/LaunchpadX/LaunchpadXProtocol.h"
#include "FakeControlSink.h"
#include "FakeDevicePorts.h"
#include "FakeLiveView.h"
#include "Control/LiveState.h"
#include "Manager/ControllerManager.h"
#include <algorithm>
#include <thread>

namespace
{
using namespace mu_control;
using namespace mu_control::test;
using mu_core::ControlStatus;
using Bytes = std::vector<uint8_t>;

const Bytes kProgrammerOn  { 0xF0, 0x00, 0x20, 0x29, 0x02, 0x0C, 0x0E, 0x01, 0xF7 };
const Bytes kProgrammerOff { 0xF0, 0x00, 0x20, 0x29, 0x02, 0x0C, 0x0E, 0x00, 0xF7 };

// Fake ports that hand their log to the test when the manager destroys them.
struct LoggedPorts : FakeDevicePorts
{
    std::vector<Bytes>* finalLog = nullptr;
    ~LoggedPorts() override { if (finalLog != nullptr) *finalLog = sent; }
};

struct CountingSink : FakeControlSink
{
    std::thread::id performedOn;
    bool perform(const mu_core::ControlAction& a) override { performedOn = std::this_thread::get_id(); return FakeControlSink::perform(a); }
};

struct StatusCounter : mu_core::ControlAttachment::Listener
{
    int calls = 0;
    void controlStatusChanged() override { ++calls; }
};

int count(const std::vector<Bytes>& log, const Bytes& m) { return (int) std::count(log.begin(), log.end(), m); }

// Runs the message loop so queued hand-offs from the MIDI thread are delivered.
void pump(int ms = 40) { juce::MessageManager::getInstance()->runDispatchLoopUntil(ms); }

struct Rig
{
    explicit Rig(bool modeOn, bool devicePresent = true)
    {
        view.mode = modeOn;
        view.useItem(0);
        view.useItem(1);
        look.accentRgb = 0x40A0FF;
        look.faultRgb  = 0xFF3030;
        for (size_t i = 0; i < look.layerRgb.size(); ++i) look.layerRgb[i] = 0x202020u * (uint32_t) (i + 1);
        present = devicePresent;
        build();
    }

    void build()
    {
        manager = std::make_unique<ControllerManager>(sink, view, look, juce::File(), [this]
        {
            auto p = std::make_unique<LoggedPorts>();
            ports = p.get();
            ports->finalLog = &finalLog;
            if (! present) { ports->outputs.clear(); ports->inputs.clear(); }
            return ControllerBackend { std::move(p), std::make_unique<LaunchpadXDriver>(), LaunchpadXDriver::defaultPorts() };
        });
        manager->addListener(&counter);
    }

    void destroy()
    {
        manager->removeListener(&counter);
        ports = nullptr;
        manager.reset();
    }

    ControlStatus status() const { return manager->getStatus(); }
    void setMode(bool on) { view.mode = on; view.notify(); }

    CountingSink                         sink;
    FakeLiveView                         view;
    mu_core::SurfaceLook                 look;
    StatusCounter                        counter;
    LoggedPorts*                         ports = nullptr;
    std::vector<Bytes>                   finalLog;
    bool                                 present = true;
    std::unique_ptr<ControllerManager>   manager;
};
}

class ControllerManagerTests : public juce::UnitTest
{
public:
    ControllerManagerTests() : juce::UnitTest("Controller manager", "MuControl") {}

    void runTest() override
    {
        beginTest("with the mode off the device is detected by name and nothing is opened");
        {
            Rig r(false);
            expect(r.status() == ControlStatus::Found);
            expectEquals(r.manager->getDeviceLabel(), juce::String("LPX MIDI"));
            expectEquals(r.ports->openCalls, 0);
            expect(r.ports->sent.empty());
            expect(r.manager->isScanTimerRunning());
            expect(! r.manager->isBlinkTimerRunning());
            r.destroy();
            expect(r.finalLog.empty());   // a manager built and destroyed with the mode off sends nothing
        }

        beginTest("no device: NotFound, an empty label, and it becomes Found when the ports appear");
        {
            Rig r(false, false);
            expect(r.status() == ControlStatus::NotFound);
            expect(r.manager->getDeviceLabel().isEmpty());
            expectEquals(r.ports->openCalls, 0);

            r.ports->outputs = { "LPX MIDI", "MIDIOUT2 (LPX MIDI)" };
            r.ports->inputs  = { "LPX MIDI", "MIDIIN2 (LPX MIDI)" };
            r.manager->rescan();
            expect(r.status() == ControlStatus::Found);
            expectEquals(r.ports->openCalls, 0);
            expectEquals(r.counter.calls, 1);
        }

        beginTest("the device is opened only when the mode turns on, and Live mode comes back when it turns off");
        {
            Rig r(false);
            r.setMode(true);
            expect(r.status() == ControlStatus::InUse);
            expectEquals(r.ports->openCalls, 1);
            expectEquals(count(r.ports->sent, kProgrammerOn), 1);
            expect(! r.manager->isScanTimerRunning());

            r.setMode(false);
            expect(r.status() == ControlStatus::Found);
            expectEquals(count(r.ports->sent, kProgrammerOff), 1);
            expect(r.ports->sent.back() == kProgrammerOff);
            expectEquals(r.ports->closeCalls, 1);
            expect(r.manager->isScanTimerRunning());

            r.setMode(true);   // and again
            expect(r.status() == ControlStatus::InUse);
            expectEquals(r.ports->openCalls, 2);
        }

        beginTest("a manager built while the mode is already on opens at once; destruction sends Live exactly once");
        {
            Rig r(true);
            expect(r.status() == ControlStatus::InUse);
            r.destroy();
            expectEquals(count(r.finalLog, kProgrammerOn), 1);
            expectEquals(count(r.finalLog, kProgrammerOff), 1);
            expect(r.finalLog.back() == kProgrammerOff);
        }

        beginTest("the mode on but the device missing opens nothing, then opens when it appears");
        {
            Rig r(true, false);
            expect(r.status() == ControlStatus::NotFound);
            r.ports->outputs = { "LPX MIDI", "MIDIOUT2 (LPX MIDI)" };
            r.ports->inputs  = { "LPX MIDI", "MIDIIN2 (LPX MIDI)" };
            r.manager->rescan();
            expect(r.status() == ControlStatus::InUse);
            expectEquals(count(r.ports->sent, kProgrammerOn), 1);
        }

        beginTest("a device that is listed but cannot be opened stays Found and is retried");
        {
            Rig r(true);
            r.setMode(false);
            r.ports->connected = false;
            r.setMode(true);
            expect(r.status() == ControlStatus::Found);
            r.ports->connected = true;
            r.manager->rescan();
            expect(r.status() == ControlStatus::InUse);
        }

        beginTest("release: Live mode, pads dark, ports closed; the mode does not reopen it; connect takes it again");
        {
            Rig r(true);
            r.manager->release();
            expect(r.status() == ControlStatus::Released);
            expect(r.ports->sent.back() == kProgrammerOff);
            expectEquals(count(r.ports->sent, kProgrammerOff), 1);
            expectEquals(r.ports->closeCalls, 1);

            r.setMode(false);
            r.setMode(true);
            expect(r.status() == ControlStatus::Released);
            expectEquals(r.ports->openCalls, 1);

            r.manager->connect();
            expect(r.status() == ControlStatus::InUse);
            expectEquals(r.ports->openCalls, 2);
        }

        beginTest("release and connect with the mode off change the lamp only; connect then waits for the mode");
        {
            Rig r(false);
            r.manager->release();
            expect(r.status() == ControlStatus::Released);
            r.manager->connect();
            expect(r.status() == ControlStatus::Found);
            expectEquals(r.ports->openCalls, 0);
            r.setMode(true);
            expect(r.status() == ControlStatus::InUse);
        }

        beginTest("release with no device does nothing; connect rescans");
        {
            Rig r(false, false);
            r.manager->release();
            expect(r.status() == ControlStatus::NotFound);
            r.ports->outputs = { "LPX MIDI" };
            r.ports->inputs  = { "MIDIIN2 (LPX MIDI)" };
            r.manager->connect();
            expect(r.status() == ControlStatus::Found);
        }

        beginTest("unplug while in use is Lost and stays Lost; replug recovers and repeats the setup");
        {
            Rig r(true);
            r.ports->unplug();
            pump();
            expect(r.status() == ControlStatus::Lost);
            pump(1100);   // a scan tick must not clear it
            expect(r.status() == ControlStatus::Lost);

            r.ports->replug();
            pump();
            expect(r.status() == ControlStatus::InUse);
            expectEquals(count(r.ports->sent, kProgrammerOn), 2);
        }

        beginTest("leaving the mode while Lost clears it, sends no Live (the pad is gone) and reports NotFound");
        {
            Rig r(true);
            r.ports->unplug();
            pump();
            r.ports->outputs.clear();
            r.ports->inputs.clear();
            r.setMode(false);
            expect(r.status() == ControlStatus::NotFound);
            expectEquals(count(r.ports->sent, kProgrammerOff), 0);
            expectEquals(r.ports->closeCalls, 1);
        }

        beginTest("a press on the MIDI thread reaches the sink on the message thread, not before");
        {
            Rig r(true);
            const auto press = launchpadx::encodeInput({ ControlKind::Top, 1, 0 }, true);
            std::thread midi([&] { r.ports->inject(press); });
            midi.join();
            expect(r.sink.performed.empty());   // queued, not yet delivered
            pump();
            expectEquals((int) r.sink.performed.size(), 1);
            if (! r.sink.performed.empty())
            {
                expect(r.sink.performed[0].type == mu_core::ControlActionType::PerformanceItem);
                expectEquals(r.sink.performed[0].index, 1);
            }
            expect(r.sink.performedOn == std::this_thread::get_id());
        }

        beginTest("a press still queued when the mode ends is dropped, never delivered");
        {
            Rig r(true);
            const auto press = launchpadx::encodeInput({ ControlKind::Top, 0, 0 }, true);
            std::thread midi([&] { r.ports->inject(press); });
            midi.join();
            r.setMode(false);
            pump();
            expect(r.sink.performed.empty());
        }

        beginTest("nothing is delivered after the manager is destroyed");
        {
            Rig r(true);
            auto* ports = r.ports;
            const auto press = launchpadx::encodeInput({ ControlKind::Top, 0, 0 }, true);
            std::thread midi([&] { ports->inject(press); });
            midi.join();
            r.destroy();
            pump();
            expect(r.sink.performed.empty());
        }

        beginTest("the blink timer runs only while a pad pulses");
        {
            Rig r(true);
            r.view.current = 0;
            r.view.notify();
            expect(! r.manager->isBlinkTimerRunning());

            r.view.queued = 1;
            r.view.notify();
            expect(r.manager->isBlinkTimerRunning());

            r.view.queued = -1;
            r.view.notify();
            expect(! r.manager->isBlinkTimerRunning());

            r.view.queued = 1;
            r.view.notify();
            expect(r.manager->isBlinkTimerRunning());
            r.setMode(false);   // closing stops it too
            expect(! r.manager->isBlinkTimerRunning());
        }

        beginTest("lights follow the LiveView: a state change sends only the changed pads");
        {
            Rig r(true);
            const auto before = r.ports->sent.size();
            r.view.selected = 1;
            r.view.notify();
            expect(r.ports->sent.size() > before);
            const auto changed = r.ports->sent.size();
            r.view.notify();   // nothing differs
            expectEquals((int) r.ports->sent.size(), (int) changed);
        }

        beginTest("the quit hook gives the device back once and keeps it closed");
        {
            Rig r(true);
            r.manager->closeNow();
            expect(r.status() == ControlStatus::Released);
            expect(r.ports->sent.back() == kProgrammerOff);
            expectEquals(count(r.ports->sent, kProgrammerOff), 1);

            r.manager->closeNow();   // repeating it (the shell calls it on more than one path) changes nothing
            expectEquals(count(r.ports->sent, kProgrammerOff), 1);
            expect(r.status() == ControlStatus::Released);
            mu_core::ControlAttachment& handle = *r.manager;   // and it is on the seam the shell sees
            handle.closeNow();
            expectEquals(count(r.ports->sent, kProgrammerOff), 1);

            r.setMode(false);
            r.setMode(true);
            expectEquals(r.ports->openCalls, 1);
            r.manager->connect();   // an explicit Connect undoes the quit (a cancelled Save dialog), so the pad is taken again
            expectEquals(r.ports->openCalls, 2);

            r.destroy();
            expectEquals(count(r.finalLog, kProgrammerOff), 2);   // and is given back once more on destruction
        }

        beginTest("an unplug and replug followed by destruction still sends Live exactly once");
        {
            Rig r(true);
            r.ports->unplug();
            pump();
            r.ports->replug();
            pump();
            expect(r.status() == ControlStatus::InUse);
            r.destroy();
            expectEquals(count(r.finalLog, kProgrammerOn), 2);    // opened, then repeated on replug
            expectEquals(count(r.finalLog, kProgrammerOff), 1);   // given back once, at the end
            expect(r.finalLog.back() == kProgrammerOff);
        }

        beginTest("destroying the manager while the device is Lost sends no Live (the pad is gone)");
        {
            Rig r(true);
            r.ports->unplug();
            pump();
            expect(r.status() == ControlStatus::Lost);
            r.destroy();
            expectEquals(count(r.finalLog, kProgrammerOff), 0);
        }

        beginTest("connect on a Lost link starts over: Found while the pad is gone, InUse once it is back");
        {
            Rig r(true);
            r.ports->unplug();
            pump();
            r.manager->connect();
            expect(r.status() == ControlStatus::Found);   // the open fails while unplugged
            r.ports->replug();
            r.manager->rescan();
            expect(r.status() == ControlStatus::InUse);
        }

        beginTest("release while Lost frees the ports without sending Live, and reports Released");
        {
            Rig r(true);
            r.ports->unplug();
            pump();
            r.manager->release();
            expect(r.status() == ControlStatus::Released);
            expectEquals(count(r.ports->sent, kProgrammerOff), 0);
        }

        beginTest("the quit hook with the mode off sends nothing and still marks the device Released");
        {
            Rig r(false);
            r.manager->closeNow();
            expect(r.status() == ControlStatus::Released);
            expect(r.ports->sent.empty());
            expect(! r.manager->isScanTimerRunning());
        }

        beginTest("end to end: pad presses drive a real LiveState and the lights follow (top row = items, side = layers)");
        {
            // A real LiveState behind the manager, staging against a pretend transport (as LiveStateTests does).
            struct Adapter : mu_core::ControlSink
            {
                mu_core::LiveState* live = nullptr;
                bool perform(const mu_core::ControlAction& a) override { return live->perform(a); }
            };
            mu_core::LiveState live;
            int staged = -1, cancels = 0;
            bool pending = false;
            uint32_t serial = 0;
            mu_core::LiveState::Callbacks cb;
            cb.supportsPerformanceMode = [] { return true; };
            cb.canLoad    = [](int i) { return i >= 0 && i < 4; };
            cb.stageItem  = [&](int i) { staged = i; ++serial; pending = true; return mu_core::LiveState::Stage::Queued; };
            cb.cancelItem = [&] { ++cancels; pending = false; };
            cb.layerCount = [] { return 4; };
            cb.fullPending = [&] { return pending; };
            cb.stageSerial = [&] { return serial; };
            live.setCallbacks(cb);

            mu_pp::PerformanceFile list;
            const juce::File folder("C:/presets");
            for (int i = 0; i < 4; ++i)
                list.setItem(i, "TestState", folder.getChildFile("p" + juce::String(i) + ".preset"), folder, "P" + juce::String(i));
            live.loadPerformance(list, juce::File());
            live.perform({ mu_core::ControlActionType::PerformanceMode, 0, 1 });

            Adapter adapter; adapter.live = &live;
            mu_core::SurfaceLook look;
            look.accentRgb = 0x40A0FF;   // real colours: an all-black look would make every light identical
            look.faultRgb  = 0xFF3030;
            for (size_t i = 0; i < look.layerRgb.size(); ++i) look.layerRgb[i] = 0x202020u * (uint32_t) (i + 1);
            LoggedPorts* ports = nullptr;
            ControllerManager manager(adapter, live, look, juce::File(), [&]
            {
                auto p = std::make_unique<LoggedPorts>();
                ports = p.get();
                return ControllerBackend { std::move(p), std::make_unique<LaunchpadXDriver>(), LaunchpadXDriver::defaultPorts() };
            });
            expect(manager.getStatus() == ControlStatus::InUse);

            auto press = [&](ControlKind k, int x, int y)
            {
                ports->inject(launchpadx::encodeInput({ k, x, y }, true));
                ports->inject(launchpadx::encodeInput({ k, x, y }, false));
                pump();
            };

            press(ControlKind::Top, 2, 0);   // the third top button = item 2
            expectEquals(live.getQueuedItem(), 2);
            expectEquals(staged, 2);

            const auto sentBefore = ports->sent.size();
            press(ControlKind::Top, 2, 0);   // pressing the queued item again is already on its way
            expectEquals(live.getQueuedItem(), 2);
            expectEquals(cancels, 0);

            pending = false; live.fullPresetCommitted();
            expectEquals(live.getCurrentItem(), 2);
            press(ControlKind::Top, 0, 0);   // queue item 0 ...
            expectEquals(live.getQueuedItem(), 0);
            press(ControlKind::Top, 2, 0);   // ... the playing item cancels the switch
            expectEquals(live.getQueuedItem(), -1);
            expectEquals(cancels, 1);

            const auto beforeLayer = ports->sent.size();
            press(ControlKind::Side, 0, 6);  // y counts from the top, so row 6 of 8 is layer 2 (bottom = layer 1)
            expectEquals(live.getSelectedLayer(), 1);
            expect(ports->sent.size() > beforeLayer, "the lights were rewritten to show the new layer");
            expect(ports->sent.size() >= sentBefore);
        }

        beginTest("listeners hear each status change once");
        {
            Rig r(false);
            expectEquals(r.counter.calls, 0);
            r.setMode(true);
            expectEquals(r.counter.calls, 1);   // Found -> InUse
            r.view.notify();
            expectEquals(r.counter.calls, 1);   // a light change is not a status change
            r.manager->release();
            expectEquals(r.counter.calls, 2);   // -> Released
            r.manager->connect();
            expectEquals(r.counter.calls, 3);   // -> InUse
        }
    }
};

static ControllerManagerTests controllerManagerTests;
