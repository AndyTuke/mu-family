// Performance mode's screen half (docs/design-ui-family.md section 12): the StepIndicator's pure helpers, the
// indication worked out from the live state, the Perf toggle and the exit hold on the transport bar, and the
// ShellChrome input shield. All driven with a real LiveState and fake callbacks over the test backend, so no
// display, device or processor is needed.

#include "TestShellBackend.h"
#include "Control/LiveState.h"
#include "UI/ModalDialog.h"
#include "UI/ConfirmDialog.h"
#include "UI/PerformanceChooser.h"
#include "UI/PerformanceIndication.h"
#include "UI/ShellChrome.h"

namespace
{
using namespace mu_core;
using Lamp = StepIndicator::Lamp;

struct ScaleGuard
{
    float saved = mu_ui::scale;
    ScaleGuard() { mu_ui::scale = 1.0f; }
    ~ScaleGuard() { mu_ui::scale = saved; }
};

struct Host : juce::Component
{
    mu_ui::ShellChrome* chrome = nullptr;
    void resized() override { if (chrome != nullptr) chrome->resized(); }
};
struct Panel : juce::Component {};

juce::TextButton* findButton(juce::Component& parent, const juce::String& text)
{
    for (auto* c : parent.getChildren())
        if (auto* b = dynamic_cast<juce::TextButton*>(c))
            if (b->getButtonText() == text) return b;
    return nullptr;
}

ControlAction act(ControlActionType t, int layer = 0, int index = -1)
{
    ControlAction a;
    a.type = t; a.layer = layer; a.index = index;
    return a;
}

// A live state wired to fake callbacks: items 0 to 2 load, item 3 is in the list but its file is gone,
// a stopped transport applies a staged item at once.
struct Rig
{
    LiveState live;
    bool stopped = true;
    bool pending = false;
    uint32_t serial = 0;
    int layers = 4;

    Rig()
    {
        LiveState::Callbacks cb;
        cb.supportsPerformanceMode = [] { return true; };
        cb.canLoad     = [](int i) { return i >= 0 && i <= 2; };
        cb.stageItem   = [this](int)
        {
            ++serial;
            if (stopped) return LiveState::Stage::Applied;
            pending = true;
            return LiveState::Stage::Queued;
        };
        cb.cancelItem  = [this] { pending = false; };
        cb.layerCount  = [this] { return layers; };
        cb.fullPending = [this] { return pending; };
        cb.stageSerial = [this] { return serial; };
        live.setCallbacks(cb);
    }

    void loadPerformance()
    {
        mu_pp::PerformanceFile p;
        p.name = "Gig";
        const juce::File folder("C:/presets");
        for (int i = 0; i < 4; ++i)
            p.setItem(i, "MuTantState", folder.getChildFile("p" + juce::String(i) + ".muTest"), folder, "Item " + juce::String(i));
        live.loadPerformance(p, juce::File("C:/presets/gig.muPerformance"));
    }
};

// Performs through the live state, remembering what it was asked.
struct Sink : ControlSink
{
    explicit Sink(LiveState& s) : state(s) {}
    LiveState& state;
    std::vector<ControlAction> seen;
    bool perform(const ControlAction& a) override { seen.push_back(a); return LiveState::handles(a.type) && state.perform(a); }
};

void pump(int ms) { juce::MessageManager::getInstance()->runDispatchLoopUntil(ms); }
}

struct PerformanceModeShellTests : juce::UnitTest
{
    PerformanceModeShellTests() : juce::UnitTest("Performance mode shell (indicator, toggle, shield)", "mu-core") {}

    void indicatorHelpers()
    {
        beginTest("StepIndicator: the LCD text split keeps the lit part at 60 percent when both do not fit");
        {
            float lit = 0, ghost = 0;
            StepIndicator::splitText(100.0f, 30.0f, 20.0f, 4.0f, lit, ghost);
            expectWithinAbsoluteError(lit, 30.0f, 0.01f);   expectWithinAbsoluteError(ghost, 20.0f, 0.01f);
            StepIndicator::splitText(100.0f, 90.0f, 50.0f, 4.0f, lit, ghost);
            expectWithinAbsoluteError(lit, 60.0f, 0.01f);   expectWithinAbsoluteError(ghost, 36.0f, 0.01f);
            StepIndicator::splitText(100.0f, 30.0f, 200.0f, 4.0f, lit, ghost);
            expectWithinAbsoluteError(lit, 30.0f, 0.01f);   expectWithinAbsoluteError(ghost, 66.0f, 0.01f);
            StepIndicator::splitText(100.0f, 140.0f, 0.0f, 4.0f, lit, ghost);
            expectWithinAbsoluteError(lit, 100.0f, 0.01f);  expectEquals(ghost, 0.0f);
            // Too little room left for a readable ghost (36 here, minimum 40): it is dropped, not drawn as a stub.
            StepIndicator::splitText(100.0f, 90.0f, 50.0f, 4.0f, lit, ghost, 40.0f);
            expectWithinAbsoluteError(lit, 90.0f, 0.01f);   expectEquals(ghost, 0.0f);
            StepIndicator::splitText(100.0f, 90.0f, 50.0f, 4.0f, lit, ghost, 30.0f);
            expectWithinAbsoluteError(ghost, 36.0f, 0.01f);   // enough room: kept
        }

        beginTest("StepIndicator: layout is LCD 240 + gap 4 + 8 lamps of 11 in the 332 px group");
        {
            ScaleGuard guard;
            const auto l = StepIndicator::computeLayout({ 0, 0, 332, 24 }, 8);
            expectEquals(l.lcd.getWidth(), 240);
            expectEquals((int) l.lamps.size(), 8);
            expectEquals(l.lamps.front().getX(), 244);
            expectEquals(l.lamps.back().getRight(), 332);
            expectEquals(StepIndicator::computeLayout({ 0, 0, 120, 24 }, 0).lcd.getWidth(), 120);
        }

        beginTest("StepIndicator: lamp brightness follows the state, queued pulses between dim and bright");
        {
            const auto& L = MuLookAndFeel::lighting();
            const float bright = juce::jmin(1.0f, L.lampOn + L.lampPlayhead);
            expectEquals(StepIndicator::litFor(Lamp::Empty, 0, false), L.lampOff);
            expectEquals(StepIndicator::litFor(Lamp::Available, 0, false), L.lampDim);
            expectEquals(StepIndicator::litFor(Lamp::Fault, 0, false), L.lampDim);
            expectEquals(StepIndicator::litFor(Lamp::Current, 0, false), L.lampOn);
            expectEquals(StepIndicator::litFor(Lamp::Current, 0, true), bright);
            expectWithinAbsoluteError(StepIndicator::litFor(Lamp::Queued, 0.0, false), L.lampDim, 0.001f);
            expectWithinAbsoluteError(StepIndicator::litFor(Lamp::Queued, 1000.0 / (2.0 * L.queuedPulseHz), false), bright, 0.001f);
            expectWithinAbsoluteError(StepIndicator::pulse(500.0, 2.0f), 0.0f, 0.001f);   // a full period at 2 Hz
        }

        beginTest("HoldTracker: progress over the duration, finished at the end, cancel resets");
        {
            mu_ui::perf::HoldTracker h(1000.0);
            expect(! h.isHolding() && h.progress(500.0) == 0.0f && ! h.finished(5000.0), "idle");
            h.press(100.0);
            expectWithinAbsoluteError(h.progress(600.0), 0.5f, 0.001f);
            expect(! h.finished(1099.0) && h.finished(1100.0), "finished exactly at the duration");
            expectEquals(h.progress(9000.0), 1.0f);
            h.cancel();
            expect(! h.isHolding() && h.progress(600.0) == 0.0f && ! h.finished(1100.0), "cancelled");
        }

        beginTest("layer indicator text and the followed layer are clamped to the layer count");
        {
            expectEquals(mu_ui::perf::layerIndicatorText(2, 5), juce::String("Layer 3/5"));
            expectEquals(mu_ui::perf::layerIndicatorText(9, 5), juce::String("Layer 5/5"));
            expectEquals(mu_ui::perf::layerIndicatorText(0, 0), juce::String("Layer 1/1"));
            expectEquals(mu_ui::perf::followedLayer(-3, 4), 0);
            expectEquals(mu_ui::perf::followedLayer(7, 4), 3);
            expectEquals(mu_ui::perf::followedLayer(2, 4), 2);
            expectEquals(mu_ui::perf::followedLayer(2, 0), 0);
        }
    }

    void indicationFromLiveState()
    {
        beginTest("indication: no performance reads 'No performance' with every lamp off");
        {
            Rig rig;
            const auto ind = mu_ui::perf::buildIndication(rig.live, "Old");
            expectEquals(ind.ghost, juce::String("No performance"));
            expect(ind.lit.isEmpty() && ! ind.ghostPulses, "nothing lit");
            expectEquals((int) ind.lamps.size(), 8);
            for (auto l : ind.lamps) expect(l == Lamp::Empty, "lamp empty");
        }

        beginTest("indication: a performance with nothing playing shows -/8 and the loaded preset");
        {
            Rig rig; rig.loadPerformance();
            const auto ind = mu_ui::perf::buildIndication(rig.live, "Old");
            expectEquals(ind.lit, juce::String("-/8  Old"));
            expect(ind.ghost.isEmpty(), "no next item without a current one");
            const std::vector<Lamp> expected { Lamp::Available, Lamp::Available, Lamp::Available, Lamp::Fault,
                                               Lamp::Empty, Lamp::Empty, Lamp::Empty, Lamp::Empty };
            expect(ind.lamps == expected, "available, available, available, missing, then empty");
            expectEquals(mu_ui::perf::buildIndication(rig.live, {}).lit, juce::String("-/8"));
        }

        beginTest("indication: the current item, its next playable item as a ghost, and the hover texts");
        {
            Rig rig; rig.loadPerformance();
            rig.live.perform(act(ControlActionType::PerformanceMode, 0, 1));
            rig.live.perform(act(ControlActionType::PerformanceItem, 0, 0));
            auto ind = mu_ui::perf::buildIndication(rig.live, {});
            expectEquals(ind.lit, juce::String("1/8 Item 0"));
            expectEquals(ind.ghost, juce::String("> Item 1"));
            expect(ind.lamps[0] == Lamp::Current && ind.lamps[1] == Lamp::Available, "lamps");
            expectEquals(ind.hints[0].first, juce::String("Performance \"Gig\""));
            expectEquals(ind.hints[0].second, juce::String("preset 1 of 8: Item 0"));
            expectEquals(ind.hints[3].first, juce::String("Preset 4 missing"));
            expectEquals(ind.hints[3].second, juce::String("Item 3"));

            rig.live.perform(act(ControlActionType::PerformanceItem, 0, 2));
            ind = mu_ui::perf::buildIndication(rig.live, {});
            expectEquals(ind.lit, juce::String("3/8 Item 2"));
            expect(ind.ghost.isEmpty(), "the missing item after it is skipped: no next, omitted at the last playable item");
        }

        beginTest("indication: a queued switch pulses its lamp and ghost and reports its status line");
        {
            Rig rig; rig.loadPerformance();
            rig.live.perform(act(ControlActionType::PerformanceMode, 0, 1));
            rig.live.perform(act(ControlActionType::PerformanceItem, 0, 0));
            rig.stopped = false;
            rig.live.perform(act(ControlActionType::PerformanceItem, 0, 2));
            const auto ind = mu_ui::perf::buildIndication(rig.live, {});
            expectEquals(ind.lit, juce::String("1/8 Item 0"));      // the playing preset stays on the LCD
            expectEquals(ind.ghost, juce::String("-> 3 Item 2"));
            expect(ind.ghostPulses, "the ghost pulses");
            expect(ind.lamps[0] == Lamp::Current && ind.lamps[2] == Lamp::Queued, "current and queued lamps");
            expect(ind.queuedValue.startsWith("3 Item 2"), "the status line names the queued item");
        }
    }

    void transportToggle()
    {
        beginTest("Perf toggle: shown only where the backend supports the mode and has a live view and a sink");
        {
            ScaleGuard guard;
            Rig rig; Sink sink(rig.live);
            TestShellBackend plain(true);
            TransportBar a(plain);
            a.setBounds(0, 0, mu_ui::s(MuLookAndFeel::kWindowWidth), mu_ui::s(MuLookAndFeel::kTransportBarH));
            expect(! findButton(a, "Perf")->isVisible() && ! a.isPerformanceAvailable(), "no live view: hidden");

            TestShellBackend noSupport(true);
            noSupport.live = &rig.live; noSupport.sink = &sink;
            TransportBar b(noSupport);
            expect(! findButton(b, "Perf")->isVisible(), "unsupported: hidden");

            TestShellBackend noSink(true);
            noSink.live = &rig.live; noSink.performanceSupported = true;
            TransportBar c(noSink);
            expect(! findButton(c, "Perf")->isVisible(), "no sink: hidden");

            TestShellBackend ok(true);
            ok.live = &rig.live; ok.sink = &sink; ok.performanceSupported = true;
            TransportBar d(ok);
            d.setBounds(a.getBounds());
            auto* perf = findButton(d, "Perf");
            auto* mixer = findButton(d, "Mixer");
            expect(perf->isVisible() && d.isPerformanceAvailable(), "supported: shown");
            expectEquals(perf->getWidth(), mu_ui::s(MuLookAndFeel::kPerfBtnW));
            expect(perf->getRight() <= mixer->getX(), "left of Mixer");
            expect(perf->getX() >= 0 && mixer->getRight() <= d.getWidth(), "inside the bar");
        }

        beginTest("Perf toggle: with no performance it says so for a moment and stays off");
        {
            ScaleGuard guard;
            Rig rig; Sink sink(rig.live);
            TestShellBackend b(true);
            b.live = &rig.live; b.sink = &sink; b.performanceSupported = true;
            TransportBar bar(b);
            bar.setBounds(0, 0, mu_ui::s(MuLookAndFeel::kWindowWidth), mu_ui::s(MuLookAndFeel::kTransportBarH));
            juce::StringArray lines;
            bar.onStatusUpdate = [&](const juce::String& n, const juce::String& v) { lines.add(n + "|" + v); };
            findButton(bar, "Perf")->onClick();
            expectEquals(lines.joinIntoString(";"), juce::String("Performance|Choose a performance first"));
            expect(! bar.isPerformanceMode() && sink.seen.empty(), "off, and nothing was performed");
        }

        beginTest("Perf toggle: a click enters with the editor's layer, swaps the preset group for the indicator");
        {
            ScaleGuard guard;
            Rig rig; rig.loadPerformance(); Sink sink(rig.live);
            TestShellBackend b(true);
            b.live = &rig.live; b.sink = &sink; b.performanceSupported = true;
            TransportBar bar(b);
            bar.setBounds(0, 0, mu_ui::s(MuLookAndFeel::kWindowWidth), mu_ui::s(MuLookAndFeel::kTransportBarH));
            bar.getEditorLayer = [] { return 2; };
            auto* perf = findButton(bar, "Perf");
            auto* newBtn = findButton(bar, "New");
            auto* saveBtn = findButton(bar, "Save");
            expect(newBtn->isVisible() && saveBtn->isVisible() && ! bar.getStepIndicator().isVisible(), "preset group shown");

            perf->onClick();
            expect(bar.isPerformanceMode() && rig.live.isPerformanceMode(), "on");
            expectEquals((int) sink.seen.size(), 1);
            expect(sink.seen[0].type == ControlActionType::PerformanceMode && sink.seen[0].index == 1, "asked to enter");
            expectEquals(sink.seen[0].layer, 2);
            expectEquals(rig.live.getSelectedLayer(), 2);
            expect(perf->getToggleState(), "the toggle is lit");
            expect(! newBtn->isVisible() && ! saveBtn->isVisible() && bar.getStepIndicator().isVisible(), "indicator replaces the group");
            expect(bar.getStepIndicator().getWidth() > 0 && bar.getStepIndicator().getRight() <= perf->getX(), "indicator sits left of Perf");
            expectEquals(bar.getStepIndicator().getNumLamps(), 8);

            perf->onClick();   // a plain click on the lit button does nothing
            expectEquals((int) sink.seen.size(), 1);
            expect(bar.isPerformanceMode(), "still on");
        }

        beginTest("Perf toggle: the exit hold fills the face, release cancels it, completion leaves without a confirm");
        {
            ScaleGuard guard;
            Rig rig; rig.loadPerformance(); Sink sink(rig.live);
            TestShellBackend b(true);
            b.live = &rig.live; b.sink = &sink; b.performanceSupported = true;
            TransportBar bar(b);
            bar.setBounds(0, 0, mu_ui::s(MuLookAndFeel::kWindowWidth), mu_ui::s(MuLookAndFeel::kTransportBarH));
            auto* perf = findButton(bar, "Perf");
            juce::StringArray lines;
            bar.onStatusUpdate = [&](const juce::String& n, const juce::String& v) { lines.add(n + "|" + v); };
            auto fill = [&] { return (float) perf->getProperties().getWithDefault("muFill", 0.0f); };

            bar.beginExitHold(1000.0);
            expect(! bar.isExitHolding(), "not held while the mode is off");
            perf->onClick();
            expect(bar.isPerformanceMode(), "entered");

            lines.clear();
            bar.beginExitHold(1000.0);
            expect(bar.isExitHolding(), "holding");
            expect(lines.contains("Performance|Hold to leave Performance mode"), "status text while held");
            bar.tickExitHold(1500.0);
            expectWithinAbsoluteError(fill(), 0.5f, 0.001f);
            lines.clear();
            bar.cancelExitHold();
            expect(! bar.isExitHolding() && fill() == 0.0f && bar.isPerformanceMode(), "release cancels and resets the fill");
            expect(lines.contains("|"), "and the 'Hold to leave' status line is taken down");

            bar.beginExitHold(2000.0);
            bar.tickExitHold(2999.0);
            expect(bar.isPerformanceMode() && fill() > 0.99f, "not done a millisecond early");
            lines.clear();
            bar.tickExitHold(3000.0);
            expect(lines.contains("|"), "completion also takes the status line down");
            expect(! bar.isPerformanceMode() && ! rig.live.isPerformanceMode(), "completion leaves the mode");
            expect(sink.seen.back().type == ControlActionType::PerformanceMode && sink.seen.back().index == 0, "asked to leave");
            expect(fill() == 0.0f && ! bar.isExitHolding(), "fill reset");
            expect(! perf->getToggleState() && findButton(bar, "New")->isVisible() && ! bar.getStepIndicator().isVisible(), "preset group is back");

            perf->onClick();   // the release that ends the hold must not enter again
            expect(! bar.isPerformanceMode(), "the click after a completed hold is ignored");
        }
    }

    void chromeShield()
    {
        const auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("mu-perf-shield");
        dir.deleteRecursively();
        dir.createDirectory();

        beginTest("shield: no live view or an unsupported backend never raises it, and Ctrl+P is left alone");
        {
            ScaleGuard guard;
            TestShellBackend plain(true);
            plain.contentDir = dir;
            Host host;
            mu_ui::ShellChrome chrome(host, plain);
            host.chrome = &chrome;
            chrome.applyWindowSize();
            expect(! chrome.isPerformanceLocked() && ! chrome.getInputShield().isVisible(), "no shield");
            expect(! chrome.keyPressed(juce::KeyPress('p', juce::ModifierKeys::ctrlModifier, 0), nullptr), "Ctrl+P passes");

            Rig rig; rig.loadPerformance(); Sink sink(rig.live);
            TestShellBackend off(true);
            off.live = &rig.live; off.sink = &sink; off.contentDir = dir;   // performanceSupported stays false
            Host host2;
            mu_ui::ShellChrome chrome2(host2, off);
            host2.chrome = &chrome2;
            rig.live.perform(act(ControlActionType::PerformanceMode, 0, 1));
            expect(rig.live.isPerformanceMode() && ! chrome2.isPerformanceLocked(), "the state is on but the backend says no");
        }

        beginTest("shield: the key that enters is Ctrl+P, either case, no other modifiers");
        {
            using mu_ui::ShellChrome;
            expect(ShellChrome::isPerformanceKey(juce::KeyPress('p', juce::ModifierKeys::ctrlModifier, 0)), "ctrl p");
            expect(ShellChrome::isPerformanceKey(juce::KeyPress('P', juce::ModifierKeys::ctrlModifier, 0)), "ctrl P");
            expect(! ShellChrome::isPerformanceKey(juce::KeyPress('p')), "plain p");
            expect(! ShellChrome::isPerformanceKey(juce::KeyPress('p', juce::ModifierKeys::ctrlModifier | juce::ModifierKeys::shiftModifier, 0)), "ctrl shift p");
            expect(! ShellChrome::isPerformanceKey(juce::KeyPress('o', juce::ModifierKeys::ctrlModifier, 0)), "ctrl o");
        }

        beginTest("shield: entering closes every overlay and dialog as a cancel, then the shield takes the top");
        {
            ScaleGuard guard;
            Rig rig; rig.loadPerformance(); Sink sink(rig.live);
            TestShellBackend b(true);
            b.live = &rig.live; b.sink = &sink; b.performanceSupported = true; b.contentDir = dir;
            Host host;
            mu_ui::ShellChrome chrome(host, b);
            host.chrome = &chrome;
            Panel sidebar, mainPanel, mixer, settings, layerIndicator;
            chrome.setMainArea(&sidebar, &mainPanel);
            chrome.setMixerOverlay(&mixer);
            chrome.setSettingsOverlay(&settings);
            sidebar.addAndMakeVisible(layerIndicator);
            chrome.applyWindowSize();
            host.setVisible(true);   // getComponentAt only resolves through visible components
            layerIndicator.setBounds(4, 300, 60, 30);
            chrome.addShieldExemption(layerIndicator);

            chrome.showSettings(true);
            bool confirmed = false;
            mu_ui::confirmAsync(&host, "Delete", "Sure?", "Delete", [&] { confirmed = true; });
            auto dialogs = [&]
            {
                int n = 0;
                for (auto* c : host.getChildren()) if (dynamic_cast<mu_ui::ModalDialog*>(c) != nullptr) ++n;
                return n;
            };
            expectEquals(dialogs(), 1);

            auto& perf = chrome.getTransportBar();
            expect(perf.enterPerformanceMode(), "entered");
            expect(chrome.isPerformanceLocked() && chrome.getInputShield().isVisible(), "locked");
            expect(! chrome.anyOverlayVisible() && mainPanel.isVisible(), "settings closed, main panel back");
            expectEquals(dialogs(), 0);
            expect(! confirmed, "the dialog was cancelled, not confirmed");
            // The tooltip window parents itself to the host as always-on-top; every other child is below the shield.
            auto shieldOnTop = [&]
            {
                const int at = host.getIndexOfChildComponent(&chrome.getInputShield());
                for (int i = at + 1; i < host.getNumChildComponents(); ++i)
                    if (! host.getChildComponent(i)->isAlwaysOnTop()) return false;
                return at >= 0;
            };
            expect(shieldOnTop(), "the shield is topmost");
            expect(chrome.getInputShield().getBounds() == host.getLocalBounds(), "and covers the host");
            pump(30);   // the closed dialog deletes itself on the next message

            // A panel added while locked still ends up under the shield.
            Panel otherMain;
            chrome.setMainArea(&sidebar, &otherMain);
            expect(shieldOnTop(), "still topmost");
            chrome.setMainArea(&sidebar, &mainPanel);

            beginTest("shield: hit testing lets only the exempt components through");
            auto centreOf = [&](juce::Component& c) { return host.getLocalPoint(&c, c.getLocalBounds().getCentre()); };
            const auto mainCentre = host.getLocalPoint(&mainPanel, mainPanel.getLocalBounds().getCentre());
            expect(host.getComponentAt(mainCentre) == &chrome.getInputShield(), "the main panel is shielded");
            expect(host.getComponentAt(host.getLocalPoint(&sidebar, juce::Point<int>(20, 20))) == &chrome.getInputShield(), "so is the sidebar");
            expect(host.getComponentAt(centreOf(chrome.getTransportBar())) != nullptr, "the bar resolves");
            expect(host.getComponentAt(centreOf(chrome.getTransportBar().getPerformToggle())) == &chrome.getTransportBar().getPerformToggle(),
                   "the Perf toggle is reachable");
            expect(host.getComponentAt(centreOf(chrome.getTransportBar().getStepIndicator())) == &chrome.getTransportBar().getStepIndicator(),
                   "the preset indicator is reachable for hover");
            expect(host.getComponentAt(centreOf(layerIndicator)) == &layerIndicator, "the sidebar's layer indicator is reachable");

            beginTest("shield: every key does nothing while on, the play key and Ctrl+P included");
            const juce::KeyPress space(juce::KeyPress::spaceKey), ctrlP('p', juce::ModifierKeys::ctrlModifier, 0);
            expect(chrome.keyPressed(space, nullptr) && ! b.playing, "space is swallowed");
            expect(chrome.keyPressed(ctrlP, nullptr) && chrome.isPerformanceLocked(), "Ctrl+P is swallowed and does not leave");
            expect(chrome.keyPressed(juce::KeyPress(juce::KeyPress::escapeKey), nullptr), "Escape is swallowed");
            expect(chrome.getInputShield().keyPressed(juce::KeyPress('x')), "the shield itself returns true");

            beginTest("shield: leaving lowers it and the keyboard works again");
            rig.live.perform(act(ControlActionType::PerformanceMode, 0, 0));
            expect(! chrome.isPerformanceLocked() && ! chrome.getInputShield().isVisible(), "unlocked");
            expect(host.getComponentAt(mainCentre) != &chrome.getInputShield(), "the panel gets the pointer again");
            expect(chrome.keyPressed(space, nullptr) && b.playing, "space plays");

            beginTest("shield: Ctrl+P enters; with no performance it only says so");
            expect(chrome.keyPressed(ctrlP, nullptr) && chrome.isPerformanceLocked(), "Ctrl+P entered");
            rig.live.perform(act(ControlActionType::PerformanceMode, 0, 0));
            mu_pp::PerformanceFile empty;
            rig.live.loadPerformance(empty, juce::File());
            juce::StringArray lines;
            chrome.getTransportBar().onStatusUpdate = [&](const juce::String& n, const juce::String& v) { lines.add(n + "|" + v); };
            expect(chrome.keyPressed(ctrlP, nullptr) && ! chrome.isPerformanceLocked(), "handled, still unlocked");
            expect(lines.contains("Performance|Choose a performance first"), "told to choose a performance first");

            host.chrome = nullptr;
        }
        dir.deleteRecursively();
    }

    void chooserLoad()
    {
        beginTest("interim chooser: a performance file loads into the live state; bad files report why");
        {
            const auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("mu-perf-chooser");
            dir.deleteRecursively();
            expectEquals(mu_ui::perf::performancesDir(dir).getFileName(), juce::String("Performances"));

            Rig rig;
            mu_pp::PerformanceFile p;
            p.name = "Friday";
            p.setItem(0, "MuTantState", dir.getChildFile("a.muTest"), dir, "A");
            const auto good = dir.getChildFile("Friday.muPerformance");
            dir.createDirectory();
            expect(p.save(good), "saved");
            expect(mu_ui::perf::loadPerformanceFile(rig.live, good).isEmpty(), "loads");
            expectEquals(rig.live.getPerformance().name, juce::String("Friday"));
            expect(rig.live.getPerformanceFile() == good, "remembers the file");

            expectEquals(mu_ui::perf::loadPerformanceFile(rig.live, dir.getChildFile("none.muPerformance")), juce::String("file not found"));
            const auto junk = dir.getChildFile("junk.muPerformance");
            junk.replaceWithText("not xml");
            expectEquals(mu_ui::perf::loadPerformanceFile(rig.live, junk), juce::String("not a performance file"));
            expectEquals(rig.live.getPerformance().name, juce::String("Friday"));   // a failed load changes nothing

            rig.live.perform(act(ControlActionType::PerformanceMode, 0, 1));
            expectEquals(mu_ui::perf::loadPerformanceFile(rig.live, good), juce::String("leave Performance mode first"));
            dir.deleteRecursively();
        }
    }

    void runTest() override
    {
        indicatorHelpers();
        indicationFromLiveState();
        transportToggle();
        chromeShield();
        chooserLoad();
    }
};

static PerformanceModeShellTests performanceModeShellTests;
