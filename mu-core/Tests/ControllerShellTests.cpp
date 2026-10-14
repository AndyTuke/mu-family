// The standalone shell's side of the controller seam, over a fake attachment (no mu-control, no MIDI port):
// the host that builds and gives the attachment back, the quit answers, the pad colours read from the theme,
// the settings page's Controller group and the transport bar's status-line messages.

#include "TestShellBackend.h"
#include "TestProcessorBase.h"
#include "Control/ControlHost.h"
#include "Plugin/QuitFlow.h"
#include "UI/StandardSettingsOverlay.h"
#include "UI/SurfaceLookFromTheme.h"
#include "UI/TransportBar.h"

#include <memory>

namespace
{
using namespace mu_core;
using Overlay = mu_ui::StandardSettingsOverlay;

// A controller handle whose every call is recorded in a shared log, so order can be asserted.
struct FakeController : ControlAttachment
{
    explicit FakeController(juce::StringArray* sharedLog = nullptr) : log(sharedLog) {}
    ~FakeController() override { if (log != nullptr) log->add("destroy"); }

    ControlStatus status = ControlStatus::Found;
    juce::String  label  = "LPX MIDI";
    int releases = 0, connects = 0, closes = 0;
    juce::StringArray* log;

    ControlStatus getStatus() const override { return status; }
    juce::String  getDeviceLabel() const override { return label; }
    void release() override { ++releases; set(ControlStatus::Released); }
    void connect() override { ++connects; set(ControlStatus::InUse); }
    void closeNow() override { ++closes; if (log != nullptr) log->add("closeNow"); status = ControlStatus::Released; }

    void set(ControlStatus s) { status = s; notifyStatusChanged(); }
};

struct NullSink : ControlSink { bool perform(const ControlAction&) override { return false; } };

// Leaves the process-wide factory slot empty whatever a test does.
struct SlotGuard { ~SlotGuard() { controlAttachmentFactory() = nullptr; } };

struct ScaleGuard
{
    float saved = mu_ui::scale;
    ScaleGuard() { mu_ui::scale = 1.0f; }
    ~ScaleGuard() { mu_ui::scale = saved; }
};

juce::TextButton* findButton(juce::Component& parent, const juce::String& text)
{
    for (auto* c : parent.getChildren())
        if (auto* b = dynamic_cast<juce::TextButton*>(c))
            if (b->getButtonText() == text) return b;
    return nullptr;
}

juce::Label* findLabel(juce::Component& parent, const juce::String& text)
{
    for (auto* c : parent.getChildren())
        if (auto* l = dynamic_cast<juce::Label*>(c))
            if (l->getText() == text) return l;
    return nullptr;
}

StatusLamp* findLamp(juce::Component& parent)
{
    for (auto* c : parent.getChildren())
        if (auto* l = dynamic_cast<StatusLamp*>(c)) return l;
    return nullptr;
}

bool shown(juce::Component* c) { return c != nullptr && c->isVisible(); }
}

struct ControllerShellTests : juce::UnitTest
{
    ControllerShellTests() : juce::UnitTest("Controller shell hook, settings group and status line", "mu-core") {}

    void hostTests()
    {
        beginTest("ControlHost: an empty factory slot builds nothing (a build without mu-control)");
        {
            SlotGuard slot;
            controlAttachmentFactory() = nullptr;
            LiveState view; NullSink sink;
            TestShellBackend b(true);
            b.live = &view; b.sink = &sink; b.performanceSupported = true;
            ControlHost host;
            int published = 0;
            expect(! host.start(b, SurfaceLook{}, juce::File(), [&](ControlAttachment*) { ++published; }));
            expect(host.get() == nullptr);
            expectEquals(published, 0);
            host.closeNow();    // nothing to do, and no crash
            host.shutdown();
        }

        beginTest("ControlHost: the factory gets the backend's sink and view, the look and the folder; the processor is handed the pointer");
        {
            SlotGuard slot;
            juce::StringArray log;
            LiveState view; NullSink sink;
            const ControlSink* gotSink = nullptr; const LiveView* gotView = nullptr;
            float gotDim = 0; juce::File gotFolder;
            controlAttachmentFactory() = [&](ControlSink& s, const LiveView& v, const SurfaceLook& look, const juce::File& f)
            {
                gotSink = &s; gotView = &v; gotDim = look.dimScale; gotFolder = f;
                return std::unique_ptr<ControlAttachment>(new FakeController(&log));
            };
            TestShellBackend b(true);
            b.live = &view; b.sink = &sink; b.performanceSupported = true;
            SurfaceLook look; look.dimScale = 0.45f;
            const auto folder = juce::File::getSpecialLocation(juce::File::tempDirectory);

            ControlHost host;
            ControlAttachment* published = nullptr;
            expect(host.start(b, look, folder, [&](ControlAttachment* a) { published = a; log.add(a != nullptr ? "publish" : "publish:null"); }));
            expect(gotSink == &sink && gotView == &view);
            expectWithinAbsoluteError(gotDim, 0.45f, 0.0001f);
            expect(gotFolder == folder);
            expect(published != nullptr && published == host.get());
        }

        beginTest("ControlHost: shutdown gives the device back first, then the processor forgets it, then it is destroyed");
        {
            SlotGuard slot;
            juce::StringArray log;
            LiveState view; NullSink sink;
            controlAttachmentFactory() = [&](ControlSink&, const LiveView&, const SurfaceLook&, const juce::File&)
            { return std::unique_ptr<ControlAttachment>(new FakeController(&log)); };
            TestShellBackend b(true);
            b.live = &view; b.sink = &sink; b.performanceSupported = true;

            ControlHost host;
            host.start(b, SurfaceLook{}, juce::File(), [&](ControlAttachment* a) { log.add(a != nullptr ? "publish" : "publish:null"); });
            host.shutdown();
            expectEquals(log.joinIntoString(","), juce::String("publish,closeNow,publish:null,destroy"));
            expect(host.get() == nullptr);
            host.shutdown();    // harmless the second time
            host.closeNow();
            expectEquals(log.size(), 4);
        }

        beginTest("ControlHost: nothing is built without Performance mode support, a sink or a view");
        {
            SlotGuard slot;
            int built = 0;
            controlAttachmentFactory() = [&](ControlSink&, const LiveView&, const SurfaceLook&, const juce::File&)
            { ++built; return std::unique_ptr<ControlAttachment>(new FakeController()); };
            LiveState view; NullSink sink;
            auto none = [](ControlAttachment*) {};

            TestShellBackend unsupported(true);
            unsupported.live = &view; unsupported.sink = &sink; unsupported.performanceSupported = false;
            TestShellBackend noSink(true);
            noSink.live = &view; noSink.performanceSupported = true;
            TestShellBackend noView(true);
            noView.sink = &sink; noView.performanceSupported = true;
            for (auto* backend : { &unsupported, &noSink, &noView })
            {
                ControlHost host;
                expect(! host.start(*backend, SurfaceLook{}, juce::File(), none));
            }
            expectEquals(built, 0);
        }
    }

    void quitTests()
    {
        beginTest("QuitFlow: Close and Save give the devices back before quitting or saving; Cancel does nothing");
        {
            juce::StringArray log;
            mu_standalone::QuitFlow flow;
            flow.giveBackDevices = [&] { log.add("giveBack"); };
            flow.quit            = [&] { log.add("quit"); };
            flow.saveState       = [&] { log.add("saveState"); };

            flow.close();
            expectEquals(log.joinIntoString(","), juce::String("giveBack,quit"));

            log.clear();
            flow.save();   // no processor save-and-quit: save the state itself, then quit
            expectEquals(log.joinIntoString(","), juce::String("giveBack,saveState,quit"));

            log.clear();
            flow.saveAndQuit = [&](std::function<void()> thenQuit) { log.add("saveAndQuit"); thenQuit(); };
            flow.save();   // the processor's route: devices back first, even though the save may be slow
            expectEquals(log.joinIntoString(","), juce::String("giveBack,saveAndQuit,quit"));

            log.clear();
            flow.cancel();
            expectEquals(log.size(), 0);
        }
    }

    void lookTests()
    {
        beginTest("makeSurfaceLook reads the screen's tokens");
        {
            const auto look = mu_ui::makeSurfaceLook();
            const auto& light = MuLookAndFeel::lighting();
            expectEquals(look.layerRgb[0], (uint32_t) 0xE5484D);   // the first layer colour of the palette
            expectEquals(look.layerRgb[7], (uint32_t) 0xB87445);
            expectEquals(look.faultRgb, (uint32_t) (MuLookAndFeel::colour(MuLookAndFeel::indicatorFault).getARGB() & 0xffffffu));
            expectEquals(look.accentRgb, (uint32_t) (MuTheme::current().global.accent.getARGB() & 0xffffffu));
            expectWithinAbsoluteError(look.dimScale, light.padDimScale, 0.0001f);
            expectWithinAbsoluteError(look.pulseHz, light.queuedPulseHz, 0.0001f);
            expectEquals(look.stepFlashMs, (int) light.stepFlashMs);
        }
    }

    void settingsTests()
    {
        ScaleGuard guard;

        beginTest("Controller group: absent without an attachment");
        {
            TestShellBackend b(true);
            Overlay page(b);
            page.setBounds(0, 0, 1088, 814);
            page.setVisible(true);
            expect(findButton(page, "Release") == nullptr || ! shown(findButton(page, "Release")));
            expect(! shown(findButton(page, "Connect")));
            expect(! shown(findLamp(page)));
            expect(! shown(findLabel(page, "Launchpad")));
        }

        beginTest("Controller group: appears when the shell attaches after the page was built, and goes when it is destroyed");
        {
            TestShellBackend b(true);
            Overlay page(b);
            page.setBounds(0, 0, 1088, 814);
            page.setVisible(true);
            expect(! shown(findLamp(page)));

            auto fake = std::make_unique<FakeController>();
            b.controller = fake.get();
            page.setVisible(false);
            page.setVisible(true);   // the page re-reads the attachment each time it is shown
            expect(shown(findLamp(page)) && shown(findLabel(page, "Launchpad")) && shown(findLabel(page, "Controller")));
            expect(shown(findButton(page, "Release")), "Found: the action is Release");

            b.controller = nullptr;   // the shell clears the processor's pointer first, then destroys the attachment
            fake.reset();
            expect(! shown(findLamp(page)) && ! shown(findButton(page, "Release")), "gone with the attachment, no dangling listener");
        }

        beginTest("Controller group: the button label and the lamp text follow the status");
        {
            TestShellBackend b(true);
            FakeController fake;
            b.controller = &fake;
            Overlay page(b);
            page.setBounds(0, 0, 1088, 814);
            page.setVisible(true);
            auto* lamp = findLamp(page);
            expect(lamp != nullptr);
            if (lamp == nullptr) return;

            struct Row { ControlStatus status; const char* button; };
            const Row rows[] = { { ControlStatus::Found, "Release" }, { ControlStatus::InUse, "Release" },
                                 { ControlStatus::Released, "Connect" }, { ControlStatus::NotFound, "Connect" },
                                 { ControlStatus::Lost, "Connect" } };
            for (const auto& row : rows)
            {
                fake.set(row.status);
                const auto statusName = controlStatusText(row.status);
                expect(shown(findButton(page, row.button)), statusName + ": button reads " + row.button);
                expect(! shown(findButton(page, juce::String(row.button) == "Release" ? "Connect" : "Release")), statusName + ": only one button");
            }
            expectEquals(controlStatusText(ControlStatus::NotFound), juce::String("Not found"));
            expectEquals(controlStatusText(ControlStatus::InUse), juce::String("In use"));
        }

        beginTest("Controller group: the button releases while the device is held and connects otherwise");
        {
            TestShellBackend b(true);
            FakeController fake;
            b.controller = &fake;
            Overlay page(b);
            page.setBounds(0, 0, 1088, 814);
            page.setVisible(true);

            findButton(page, "Release")->onClick();
            expectEquals(fake.releases, 1);
            expect(fake.getStatus() == ControlStatus::Released);
            expect(shown(findButton(page, "Connect")), "the label changed with the status");
            findButton(page, "Connect")->onClick();
            expectEquals(fake.connects, 1);
            expectEquals(fake.releases, 1);
        }

        beginTest("Controller group: lamp colours per status (design-ui-family 14)");
        {
            using Id = MuLookAndFeel::ColourIds;
            const auto& L = MuLookAndFeel::lighting();
            const auto notFound = Overlay::controllerLook(ControlStatus::NotFound);
            const auto found    = Overlay::controllerLook(ControlStatus::Found);
            const auto inUse    = Overlay::controllerLook(ControlStatus::InUse);
            const auto released = Overlay::controllerLook(ControlStatus::Released);
            const auto lost     = Overlay::controllerLook(ControlStatus::Lost);

            expect(notFound.colour == MuLookAndFeel::colour(Id::segmentWarningBorder) && notFound.lit == L.lampDim, "Not found: amber, dim");
            expect(found.colour == MuLookAndFeel::colour(Id::segmentPositiveBorder) && found.lit == L.lampOn, "Found: green");
            expect(inUse.colour == found.colour && inUse.lit > found.lit, "In use: green, brighter");
            expect(released.lit == L.lampOff, "Released: unlit, the bare lamp base");
            expect(lost.colour == MuLookAndFeel::colour(Id::indicatorFault) && lost.lit == inUse.lit, "Lost: fault red, bright");
            expectEquals(found.text, juce::String("Found"));
            expectEquals(released.button, juce::String("Connect"));
            expectEquals(found.button, juce::String("Release"));
        }

        beginTest("Controller group: the standalone page still fits without scrolling at both sizes");
        {
            for (float sc : { 1.0f, 1.25f })
                for (int h : { 814, 774 })
                {
                    ScaleGuard inner;
                    mu_ui::scale = sc;
                    TestProcessorBase proc(juce::AudioProcessor::wrapperType_Standalone);
                    proc.setMidiSyncEnabled(true);   // the tallest standalone page: Messages and Sync offset rows
                    FakeController fake;
                    proc.setControlAttachment(&fake);
                    Overlay page(proc);
                    const int W = mu_ui::s(1088), H = mu_ui::s(h);
                    page.setBounds(0, 0, W, H);
                    page.setVisible(true);   // lays the group out; the page's own fit assertion guards the height

                    const int footerTop = H - mu_ui::s(20) - mu_ui::s(20);
                    const juce::String label = "scale " + juce::String(sc) + " h" + juce::String(h);
                    for (auto* c : { (juce::Component*) findLamp(page), (juce::Component*) findButton(page, "Release") })
                    {
                        expect(shown(c), label + ": Controller controls present");
                        if (c == nullptr) continue;
                        expect(page.getLocalBounds().contains(c->getBounds()), label + ": inside the page");
                        expect(c->getBottom() <= footerTop, label + ": above the footer");
                    }
                    expect(! findLamp(page)->getBounds().intersects(findButton(page, "Release")->getBounds()), label + ": rows do not overlap");
                    proc.setControlAttachment(nullptr);
                }
        }
    }

    void statusLineTests()
    {
        ScaleGuard guard;

        beginTest("Status line: Connected and Released are announced, adoption and light changes are not");
        {
            TestShellBackend b(true);
            FakeController fake;
            fake.status = ControlStatus::InUse;
            b.controller = &fake;
            TransportBar bar(b);
            juce::StringArray lines;
            bar.onStatusUpdate = [&](const juce::String& n, const juce::String& v) { lines.add(n + "|" + v); };

            bar.syncController();   // adopts the attachment: its current state is the baseline
            expectEquals(lines.size(), 0);

            fake.set(ControlStatus::Released);
            expectEquals(lines.joinIntoString(";"), juce::String("Launchpad|Released"));
            lines.clear();
            fake.set(ControlStatus::Found);      // a device appears or connects
            expectEquals(lines.joinIntoString(";"), juce::String("Launchpad|Connected"));
            lines.clear();
            fake.set(ControlStatus::InUse);      // Performance mode started: not news
            expectEquals(lines.size(), 0);
        }

        beginTest("Status line: Lost is sticky until the device returns, and then clears");
        {
            TestShellBackend b(true);
            FakeController fake;
            b.controller = &fake;
            TransportBar bar(b);
            juce::StringArray lines;
            bar.onStatusUpdate = [&](const juce::String& n, const juce::String& v) { lines.add(n + "|" + v); };
            bar.syncController();

            fake.set(ControlStatus::NotFound);
            fake.set(ControlStatus::Found);      // a timed 'Connected' is pending...
            lines.clear();
            fake.set(ControlStatus::InUse);
            fake.set(ControlStatus::Lost);       // ...and the sticky warning supersedes its clear
            expectEquals(lines.joinIntoString(";"), juce::String("Launchpad|Lost: unplugged or in use elsewhere"));
            lines.clear();
            juce::MessageManager::getInstance()->runDispatchLoopUntil(MuLookAndFeel::kPerfStatusMs + 300);
            expectEquals(lines.size(), 0, "nothing clears the warning on a timer");

            fake.set(ControlStatus::Found);      // the pad came back
            expectEquals(lines.joinIntoString(";"), juce::String("|;Launchpad|Connected"));
        }

        beginTest("Status line: the warning goes when the attachment is destroyed");
        {
            TestShellBackend b(true);
            auto fake = std::make_unique<FakeController>();
            b.controller = fake.get();
            TransportBar bar(b);
            juce::StringArray lines;
            bar.onStatusUpdate = [&](const juce::String& n, const juce::String& v) { lines.add(n + "|" + v); };
            bar.syncController();
            fake->set(ControlStatus::Lost);
            lines.clear();

            b.controller = nullptr;
            fake.reset();
            expectEquals(lines.joinIntoString(";"), juce::String("|"));
            bar.syncController();   // and the bar holds nothing stale
        }

        beginTest("Status line: the Lost warning goes when Performance mode ends");
        {
            LiveState live;
            LiveState::Callbacks cb;
            cb.supportsPerformanceMode = [] { return true; };
            cb.canLoad = [](int) { return true; };
            live.setCallbacks(cb);
            mu_pp::PerformanceFile list;
            const juce::File folder("C:/presets");
            list.setItem(0, "MuTantState", folder.getChildFile("p0.muTant"), folder, "P0");
            live.loadPerformance(list, juce::File());

            TestShellBackend b(true);
            NullSink sink;
            b.live = &live; b.sink = &sink; b.performanceSupported = true;
            FakeController fake;
            b.controller = &fake;
            TransportBar bar(b);
            juce::StringArray lines;
            bar.onStatusUpdate = [&](const juce::String& n, const juce::String& v) { lines.add(n + "|" + v); };

            live.perform({ ControlActionType::PerformanceMode, 0, 1 });
            bar.syncPerformance();
            bar.syncController();
            fake.set(ControlStatus::Lost);
            expectEquals(lines.joinIntoString(";"), juce::String("Launchpad|Lost: unplugged or in use elsewhere"));
            lines.clear();

            live.perform({ ControlActionType::PerformanceMode, 0, 0 });
            bar.syncPerformance();
            expectEquals(lines.joinIntoString(";"), juce::String("|"));
        }
    }

    void runTest() override
    {
        juce::ScopedJuceInitialiser_GUI gui;
        hostTests();
        quitTests();
        lookTests();
        settingsTests();
        statusLineTests();
    }
};

static ControllerShellTests controllerShellTests;
