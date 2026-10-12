#include "TestShellBackend.h"
#include "UI/ShellChrome.h"
#include "UI/Components/KnobWithLabel.h"

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

        beginTest("About and Save notify the host before they appear");
        {
            bool aboutVisibleAtHook = true, saveVisibleAtHook = true;
            int aboutCalls = 0, saveCalls = 0;
            chrome->onAboutOpening = [&] { ++aboutCalls; aboutVisibleAtHook = chrome->getAboutOverlay().isVisible(); };
            chrome->onSaveDialogOpening = [&] { ++saveCalls; saveVisibleAtHook = chrome->getSaveDialog().isVisible(); };
            chrome->showAbout(true);
            chrome->showSaveDialog(true);
            expectEquals(aboutCalls, 1);
            expectEquals(saveCalls, 1);
            expect(! aboutVisibleAtHook && ! saveVisibleAtHook, "hooks run before the overlay shows");
            expect(chrome->getAboutOverlay().isVisible() && chrome->getSaveDialog().isVisible(), "both visible after");
            expect(chrome->anyOverlayVisible(), "counted as overlays");
            chrome->showAbout(false);
            chrome->showSaveDialog(false);
            expectEquals(aboutCalls, 1);
            expectEquals(saveCalls, 1);
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
