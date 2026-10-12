#include "TestShellBackend.h"
#include "TestProcessorBase.h"
#include "UI/ShellChrome.h"
#include "UI/Components/KnobWithLabel.h"
#include "UI/ModalDialog.h"

#include <memory>

// ShellChrome over a hosted Component and the fake backend: no processor, no editor. Covers the
// parts every window built on the chrome depends on: the children and their z-order, the layout at
// the standard window size, the overlay state machine, the extra-screen registration and the
// backend callbacks.
namespace
{
struct ScaleGuard
{
    float saved = mu_ui::scale;
    ~ScaleGuard() { mu_ui::scale = saved; }
};

// A host window that forwards its layout to the chrome, as a real editor does.
struct Host : juce::Component
{
    mu_ui::ShellChrome* chrome = nullptr;
    void resized() override { if (chrome != nullptr) chrome->resized(); }
};

// A stand-in for a panel the product supplies.
struct Panel : juce::Component {};

int indexOf(juce::Component& host, juce::Component& child) { return host.getIndexOfChildComponent(&child); }
} // namespace

struct ShellChromeTests : juce::UnitTest
{
    ShellChromeTests() : juce::UnitTest("ShellChrome over a hosted component", "mu-core") {}

    // Presses the dialog button labelled `label` in the newest ModalDialog child of `host`.
    bool clickDialogButton(juce::Component& host, const juce::String& label)
    {
        for (int i = host.getNumChildComponents(); --i >= 0;)
            if (auto* dlg = dynamic_cast<mu_ui::ModalDialog*>(host.getChildComponent(i)))
                for (auto* c : dlg->getChildren())
                    if (auto* b = dynamic_cast<juce::TextButton*>(c))
                        if (b->getButtonText() == label)
                        {
                            b->onClick();
                            // The dialog removed itself from the host and queued its own delete
                            // (guarded by a SafePointer); there is no message loop here, so delete it now.
                            delete dlg;
                            return true;
                        }
        return false;
    }

    // Open dialogs a test did not answer would leak; remove and delete them.
    void discardDialogs(juce::Component& host)
    {
        for (int i = host.getNumChildComponents(); --i >= 0;)
            if (auto* dlg = dynamic_cast<mu_ui::ModalDialog*>(host.getChildComponent(i)))
            {
                host.removeChildComponent(dlg);
                delete dlg;
            }
    }

    void runPresetFlowTests()
    {
        const auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("mu-shellchrome-test");
        dir.deleteRecursively();
        dir.createDirectory();

        mu_ui::scale = 1.0f;
        TestShellBackend b;
        b.presetsDir = dir;
        b.contentDir = dir;
        b.defaultPreset = dir.getChildFile("_default.muTest");
        Host host;
        auto chrome = std::make_unique<mu_ui::ShellChrome>(host, b);
        host.chrome = chrome.get();
        Panel main;
        chrome->setMainArea(nullptr, &main);
        chrome->applyWindowSize();

        juce::StringArray events;
        chrome->onPresetLoaded = [&](const juce::File& f)
        { events.add("loaded:" + f.getFileName() + (chrome->isDirty() ? ":dirty" : ":clean")); };
        chrome->onPresetSaved = [&](const juce::File& f)
        { events.add("saved:" + f.getFileName() + (chrome->isDirty() ? ":dirty" : ":clean")); };
        chrome->onPresetNew = [&] { events.add("new"); };
        chrome->onCategoriesRefreshed = [&](const juce::StringArray& c) { events.add("categories:" + c.joinIntoString(",")); };
        chrome->getProductCategories = [] { return juce::StringArray { "Lead", "Bass" }; };

        beginTest("dirty flag: set, read, cleared");
        chrome->markDirty();
        expect(chrome->isDirty(), "marked");
        chrome->clearDirty();
        expect(! chrome->isDirty(), "cleared");

        beginTest("choosing a preset loads it, clears the flag, then tells the host");
        {
            const auto a = dir.getChildFile("A.muTest");
            a.replaceWithText("<Preset/>");
            chrome->getTransportBar().refreshPresets();   // the bar finds a loaded file through its list
            chrome->markDirty();
            chrome->getTransportBar().onPresetSelected(a);
            expectEquals(b.log.joinIntoString("|"), juce::String("load:A.muTest"));
            expectEquals(events.joinIntoString("|"), juce::String("loaded:A.muTest:clean"));
            expect(chrome->getTransportBar().getLoadedPresetFile() == a, "the bar shows it");
            events.clear();
            b.log.clear();

            chrome->getPresetBrowser().onLoadPreset(a);   // the browser takes the same path
            expectEquals(events.joinIntoString("|"), juce::String("loaded:A.muTest:clean"));
            events.clear();
            b.log.clear();
        }

        beginTest("Save opens the card with the merged categories, then saves in order");
        {
            b.categories = { "Pad", "Bass" };
            chrome->markDirty();
            chrome->getTransportBar().onSavePreset();
            expect(chrome->getSaveDialog().isVisible(), "card is up");
            expectEquals(events.joinIntoString("|"), juce::String("categories:Bass,Lead,Pad"));
            events.clear();

            chrome->getSaveDialog().onSave("B", "desc", "Pad", false);
            expectEquals(b.log.joinIntoString("|"), juce::String("save:B|ensure:Pad"));
            expectEquals(events.joinIntoString("|"), juce::String("categories:Pad,Bass|saved:B.muTest:clean"));
            expect(! chrome->getSaveDialog().isVisible(), "card closed");
            expect(chrome->getTransportBar().getLoadedPresetFile() == dir.getChildFile("B.muTest"), "bar shows the saved file");
            events.clear();
            b.log.clear();
        }

        beginTest("saving over an existing preset asks first");
        {
            chrome->markDirty();
            chrome->getSaveDialog().onSave("B", "", "Pad", false);
            expect(b.log.isEmpty() && events.isEmpty(), "nothing saved yet");
            expect(clickDialogButton(host, "Overwrite"), "the confirm dialog is up");
            expectEquals(b.log.joinIntoString("|"), juce::String("save:B|ensure:Pad"));
            expect(! chrome->isDirty(), "saved");
            discardDialogs(host);
            events.clear();
            b.log.clear();
        }

        beginTest("Save and quit runs the quit after the save, and not after a cancel");
        {
            int quits = 0;
            b.onSaveAndQuit([&] { ++quits; });
            expect(chrome->getSaveDialog().isVisible(), "card is up");
            chrome->getSaveDialog().onCancel();
            expect(! chrome->getSaveDialog().isVisible() && quits == 0, "cancel closes it, no quit");
            chrome->getSaveDialog().onSave("C", "", "Pad", false);
            expectEquals(quits, 0);   // the cancelled request is gone

            b.onSaveAndQuit([&] { ++quits; });
            chrome->getSaveDialog().onSave("D", "", "Pad", false);
            expectEquals(quits, 1);
            events.clear();
            b.log.clear();
        }

        beginTest("New without a saved default only reports");
        {
            chrome->markDirty();
            chrome->getTransportBar().onNewPreset();   // dirty: asks to discard first
            expect(b.log.isEmpty(), "waiting for the answer");
            expect(clickDialogButton(host, "Discard"), "the discard dialog is up");
            discardDialogs(host);
            expectEquals(events.joinIntoString("|"), juce::String("new"));
            expect(b.log.isEmpty(), "no load without a default file");
            events.clear();
        }

        beginTest("New with a default loads it, clears the flag, then tells the host");
        {
            b.defaultPreset.replaceWithText("<Preset/>");
            chrome->clearDirty();
            chrome->getTransportBar().onNewPreset();   // clean: no question
            expectEquals(b.log.joinIntoString("|"), juce::String("load:_default.muTest"));
            expectEquals(events.joinIntoString("|"), juce::String("loaded:_default.muTest:clean|new"));
            expect(! chrome->getTransportBar().getLoadedPresetFile().existsAsFile(), "no preset is shown as loaded");
            events.clear();
        }

        beginTest("a staged swap landing tells the host which preset is loaded");
        {
            const auto a = dir.getChildFile("A.muTest");
            chrome->getTransportBar().setLoadedPreset(a);
            b.onPresetSwapCommitted();
            expectEquals(events.joinIntoString("|"), juce::String("loaded:A.muTest:clean"));
            events.clear();
        }

        chrome.reset();
        expect(! b.onSaveAndQuit && ! b.onPresetSwapCommitted, "preset callbacks cleared with the chrome");
        dir.deleteRecursively();
    }

    void runKeyTests()
    {
        const auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("mu-shellchrome-keys");
        dir.deleteRecursively();
        dir.createDirectory();
        const juce::KeyPress space(juce::KeyPress::spaceKey), p('p');

        beginTest("keys: standalone seeds keybindings.json and space plays");
        {
            TestShellBackend b(true);
            b.contentDir = dir;
            Host host;
            mu_ui::ShellChrome chrome(host, b);
            expect(dir.getChildFile("keybindings.json").existsAsFile(), "defaults written");
            expect(host.getWantsKeyboardFocus(), "the host takes keyboard focus");
            expect(! chrome.keyPressed(p, nullptr) && ! b.playing, "other keys pass");
            expect(chrome.keyPressed(space, nullptr) && b.playing, "space toggles play");
        }

        beginTest("keys: keybindings.json can move the key");
        {
            dir.getChildFile("keybindings.json").replaceWithText("{ \"play_stop\": \"p\" }");
            TestShellBackend b(true);
            b.contentDir = dir;
            Host host;
            mu_ui::ShellChrome chrome(host, b);
            expect(! chrome.keyPressed(space, nullptr) && ! b.playing, "space no longer plays");
            expect(chrome.keyPressed(p, nullptr) && b.playing, "p plays");
        }

        beginTest("keys: a plugin host owns the transport, so the host is left alone");
        {
            TestShellBackend b(false);
            b.contentDir = dir;
            Host host;
            mu_ui::ShellChrome chrome(host, b);
            expect(! host.getWantsKeyboardFocus(), "no keyboard focus claimed");
        }
        dir.deleteRecursively();
    }

    void runTest() override
    {
        ScaleGuard guard;

        beginTest("the stored UI scale is applied before the children are built");
        {
            mu_ui::scale = 1.0f;
            TestShellBackend backend;
            backend.uiScale = 1.25f;
            Host host;
            mu_ui::ShellChrome chrome(host, backend);
            host.chrome = &chrome;
            expect(mu_ui::scale == 1.25f, "scale taken from the backend");
            chrome.applyWindowSize();
            expectEquals(host.getWidth(),  mu_ui::s(MuLookAndFeel::kWindowWidth));
            expectEquals(host.getHeight(), mu_ui::s(MuLookAndFeel::kWindowHeight));
        }

        mu_ui::scale = 1.0f;
        TestShellBackend backend;
        Host host;
        auto chrome = std::make_unique<mu_ui::ShellChrome>(host, backend);
        host.chrome = chrome.get();
        Panel sidebar, mainPanel, mixer, settings, activation, pageA, pageB;
        chrome->setMainArea(&sidebar, &mainPanel);
        chrome->setMixerOverlay(&mixer);
        chrome->setSettingsOverlay(&settings);
        chrome->addExtraScreen(activation, mu_ui::ShellChrome::ExtraKind::Modal, &chrome->getSaveDialog());
        chrome->addExtraScreen(pageA, mu_ui::ShellChrome::ExtraKind::Page, &chrome->getStatusBar());
        chrome->addExtraScreen(pageB, mu_ui::ShellChrome::ExtraKind::Page, &chrome->getStatusBar());
        chrome->applyWindowSize();

        beginTest("children are parented to the host in the shell's z-order");
        {
            auto& t = chrome->getTransportBar();
            auto& a = chrome->getAboutOverlay();
            auto& sv = chrome->getSaveDialog();
            auto& b = chrome->getPresetBrowser();
            auto& st = chrome->getStatusBar();
            for (auto* c : std::initializer_list<juce::Component*> { &t, &a, &sv, &b, &st, &activation, &pageA, &pageB })
                expect(c->getParentComponent() == &host, "parented to the host");
            expect(indexOf(host, t) < indexOf(host, a), "bar below About");
            expect(indexOf(host, a) < indexOf(host, activation), "About below the activation panel");
            expect(indexOf(host, activation) < indexOf(host, sv), "activation panel below the save card");
            expect(indexOf(host, sv) < indexOf(host, b), "save card below the browser");
            expect(indexOf(host, b) < indexOf(host, pageA), "browser below the first page");
            expect(indexOf(host, pageA) < indexOf(host, pageB), "pages keep their registration order");
            expect(indexOf(host, pageB) < indexOf(host, st), "pages below the status bar");
            expect(&host.getLookAndFeel() == &chrome->getLookAndFeel(), "host uses the chrome's look and feel");
        }

        beginTest("layout at the standard window size");
        {
            using mu_ui::s;
            const int w = host.getWidth(), h = host.getHeight();
            const int th = s(MuLookAndFeel::kTransportBarH), sh = s(MuLookAndFeel::kStatusBarH);
            const int sw = s(MuLookAndFeel::kSidebarW);
            expect(chrome->getTransportBar().getBounds() == juce::Rectangle<int>(0, 0, w, th), "bar spans the top");
            expect(chrome->getStatusBar().getBounds() == juce::Rectangle<int>(0, h - sh, w, sh), "status bar spans the bottom");
            const juce::Rectangle<int> mainArea(sw, th, w - sw, h - th - sh);
            expect(sidebar.getBounds() == juce::Rectangle<int>(0, th, sw, h - th - sh), "sidebar fills its column");
            expect(mainPanel.getBounds() == mainArea, "main panel fills the main area");
            expect(mixer.getBounds() == mainArea && settings.getBounds() == mainArea, "overlay slots fill the main area");
            expect(chrome->getPresetBrowser().getBounds() == mainArea, "browser fills the main area");
            expect(pageA.getBounds() == mainArea && pageB.getBounds() == mainArea, "page screens fill the main area");
            expect(chrome->getAboutOverlay().getBounds() == host.getLocalBounds(), "About spans the host");
            expect(chrome->getSaveDialog().getBounds() == host.getLocalBounds(), "save card spans the host");
            expect(activation.getBounds() == host.getLocalBounds(), "modal screen spans the host");
        }

        beginTest("no sidebar: the main panel takes the full width");
        {
            Panel other;
            chrome->setMainArea(nullptr, &other);
            expectEquals(other.getX(), 0);
            expectEquals(other.getWidth(), host.getWidth());
            chrome->setMainArea(&sidebar, &mainPanel);
            expect(other.getParentComponent() == nullptr, "the old panel is released");
        }

        beginTest("overlay state machine");
        {
            expect(! chrome->anyOverlayVisible(), "nothing open at start");
            chrome->showSettings(true);
            expect(settings.isVisible() && ! mainPanel.isVisible(), "settings replaces the main panel");
            expect(chrome->isSettingsVisible() && chrome->anyOverlayVisible(), "state reflects it");
            chrome->showExtraScreen(pageA, true);
            expect(pageA.isVisible() && ! settings.isVisible(), "opening a page closes settings");
            expect(chrome->isExtraScreenVisible(pageA) && ! chrome->isSettingsVisible(), "flags follow");
            chrome->showExtraScreen(pageA, false);
            expect(! pageA.isVisible() && mainPanel.isVisible(), "closing a page returns to the main panel");
            expect(! chrome->anyOverlayVisible(), "all closed again");

            chrome->showExtraScreen(activation, true);
            expect(activation.isVisible() && mainPanel.isVisible(), "a modal sits over the page");
            expect(chrome->anyOverlayVisible(), "a modal counts as an overlay");
            chrome->showPresetBrowser(true);
            expect(! activation.isVisible() && chrome->getPresetBrowser().isVisible() && ! mainPanel.isVisible(),
                   "the browser closes the modal and replaces the main panel");
            chrome->showPresetBrowser(false);
            expect(mainPanel.isVisible() && ! chrome->getPresetBrowser().isVisible(), "browser closed");

            chrome->showMixer(true);
            expect(chrome->isMixerVisible() && mixer.isVisible(), "mixer opens");
            chrome->showSettings(true);
            expect(! chrome->isMixerVisible() && ! mixer.isVisible() && settings.isVisible(), "settings closes the mixer");
            chrome->hideAllOverlays();
            expect(! chrome->anyOverlayVisible() && mainPanel.isVisible() && mainPanel.getAlpha() == 1.0f
                       && ! settings.isVisible() && ! mixer.isVisible(), "hideAllOverlays restores the main view");
        }

        beginTest("About notifies the host before it appears; Save opens and closes");
        {
            bool aboutVisibleAtHook = true;
            int aboutCalls = 0;
            chrome->onAboutOpening = [&] { ++aboutCalls; aboutVisibleAtHook = chrome->getAboutOverlay().isVisible(); };
            chrome->showAbout(true);
            chrome->showSaveDialog(true);
            expectEquals(aboutCalls, 1);
            expect(! aboutVisibleAtHook, "the hook runs before the panel shows");
            expect(chrome->getAboutOverlay().isVisible() && chrome->getSaveDialog().isVisible(), "both visible after");
            expect(chrome->anyOverlayVisible(), "counted as overlays");
            chrome->showAbout(false);
            chrome->showSaveDialog(false);
            expectEquals(aboutCalls, 1);
            expect(! chrome->anyOverlayVisible(), "closed");
        }

        beginTest("transport bar toggles drive the state machine");
        {
            chrome->getTransportBar().onSettingsToggle();
            expect(chrome->isSettingsVisible(), "gear opens settings");
            chrome->getTransportBar().onSettingsToggle();
            expect(! chrome->isSettingsVisible(), "gear closes it");
            chrome->getTransportBar().onLogoClicked();
            expect(chrome->getAboutOverlay().isVisible(), "logo opens About");
            chrome->getAboutOverlay().onDismiss();
            expect(! chrome->getAboutOverlay().isVisible(), "dismiss closes it");
            chrome->getPresetBrowser().onClose();
            expect(mainPanel.isVisible(), "closing the browser shows the main panel");
        }

        beginTest("a missing slot makes the matching show call a no-op");
        {
            TestShellBackend b2;
            Host h2;
            mu_ui::ShellChrome c2(h2, b2);
            h2.chrome = &c2;
            c2.showMixer(true);
            c2.showSettings(true);
            expect(! c2.isMixerVisible() && ! c2.isSettingsVisible(), "nothing to show");
        }

        beginTest("forwardKnobStatus reaches knobs and the host's own component types");
        {
            struct Special : juce::Component { int hits = 0; };
            Panel root;
            KnobWithLabel knob("Cutoff");
            Special special;
            root.addAndMakeVisible(knob);
            root.addAndMakeVisible(special);
            chrome->forwardKnobStatus(root, [&](juce::Component& c, const mu_ui::ShellChrome::StatusFn& show)
            {
                if (auto* sp = dynamic_cast<Special*>(&c)) { ++sp->hits; show("x", "y"); return true; }
                return false;
            });
            expect(static_cast<bool>(knob.onStatusUpdate), "the knob got a status handler");
            expectEquals(special.hits, 1);
            // A handler the knob already has is left alone.
            int own = 0;
            knob.onStatusUpdate = [&](const juce::String&, const juce::String&) { ++own; };
            chrome->forwardKnobStatus(root);
            knob.onStatusUpdate("a", "b");
            expectEquals(own, 1);
        }

        runPresetFlowTests();
        runKeyTests();

        beginTest("backend callbacks: scale change resizes the host, teardown clears them");
        {
            expect(static_cast<bool>(backend.onLoadError) && static_cast<bool>(backend.onUiScaleChanged),
                   "chrome registered its callbacks");
            backend.setUiScale(1.25f);
            expectEquals(host.getWidth(), mu_ui::s(MuLookAndFeel::kWindowWidth));
            expect(mu_ui::scale == 1.25f, "global scale follows");
            expect(chrome->getTransportBar().getWidth() == host.getWidth(), "layout reran at the new size");
            mu_ui::scale = 1.0f;

            host.chrome = nullptr;
            chrome.reset();
            expect(! backend.onLoadError && ! backend.onUiScaleChanged, "callbacks cleared with the chrome");
            expectEquals(host.getNumChildComponents(), 7);   // only the seven test panels remain
        }
    }
};

static ShellChromeTests shellChromeTests;

// An editor (the chrome inside it) is destroyed while the backend lives on, as a DAW does when it
// closes the plugin window. The transport bar's timer and the backend callbacks must go with it:
// nothing may reach a destroyed object afterwards.
struct ShellChromeTeardownTests : juce::UnitTest
{
    ShellChromeTeardownTests() : juce::UnitTest("ShellChrome teardown while the backend lives", "mu-core") {}

    // Lets the message thread run timers for `ms`.
    static void pump(int ms) { juce::MessageManager::getInstance()->runDispatchLoopUntil(ms); }

    struct ExposedProcessor : TestProcessorBase
    {
        explicit ExposedProcessor(WrapperType w) : TestProcessorBase(w) {}
        using ProcessorBase::drainDeferredWork;
    };

    void runTest() override
    {
        ScaleGuard guard;
        mu_ui::scale = 1.0f;

        beginTest("fake backend: the bar's timer polls it while the chrome lives and stops with the chrome");
        {
            TestShellBackend backend(true);
            Host host;
            auto chrome = std::make_unique<mu_ui::ShellChrome>(host, backend);
            host.chrome = chrome.get();

            pump(250);
            expect(backend.reads.load() > 0, "the transport bar's timer read the backend");

            host.chrome = nullptr;
            chrome.reset();
            expect(! backend.onLoadError && ! backend.onUiScaleChanged && ! backend.onSaveAndQuit
                       && ! backend.onPresetSwapCommitted, "every shell callback is cleared");

            const int atDestroy = backend.reads.load();
            pump(400);   // several timer periods
            expectEquals(backend.reads.load(), atDestroy);

            // The backend still works with nobody listening.
            backend.setUiScale(1.25f);
            backend.setUiScale(1.0f);
        }

        beginTest("processor backend: destroying the chrome, then running the swap poll and deferred work, touches nothing stale");
        {
            ExposedProcessor proc(juce::AudioProcessor::wrapperType_Standalone);
            int committed = 0;
            {
                Host host;
                auto chrome = std::make_unique<mu_ui::ShellChrome>(host, proc);
                host.chrome = chrome.get();
                proc.onPresetSwapCommitted = [&committed] { ++committed; };   // overwritten by the chrome's own
                pump(150);
                host.chrome = nullptr;
            }
            expect(! proc.onLoadError && ! proc.onUiScaleChanged && ! proc.onSaveAndQuit
                       && ! proc.onPresetSwapCommitted, "every shell callback is cleared on the processor");
            pump(300);                   // the bar's timer is gone; the processor's own poll keeps running
            proc.drainDeferredWork();    // nothing may call into the destroyed chrome
            expectEquals(committed, 0);
        }
    }
};

static ShellChromeTeardownTests shellChromeTeardownTests;
