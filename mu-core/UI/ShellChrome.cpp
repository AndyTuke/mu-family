#include "ShellChrome.h"
#include "UI/Components/KnobWithLabel.h"
#include "UI/SettingsOverlayBase.h"
#include "BuildNumber.h"        // BUILD_NUMBER, MU_VERSION_STRING: local version for the upgrade check

namespace mu_ui
{

ShellChrome::ScaleInit::ScaleInit(mu_core::ShellBackend& b)
{
    mu_ui::scale = juce::jlimit(mu_core::ShellBackend::kUiScaleMedium,
                                mu_core::ShellBackend::kUiScaleLarge,
                                b.getUiScale());
}

ShellChrome::ShellChrome(juce::Component& hostComponent, mu_core::ShellBackend& shellBackend)
    : host(hostComponent),
      backend(shellBackend),
      scaleInit(shellBackend),
      transportBar(shellBackend),
      tooltipWindow(&hostComponent, 700)
{
    wireBuiltIns();
}

ShellChrome::~ShellChrome()
{
    // The backend can outlive the host (a DAW closes the window and keeps the plugin), so no
    // callback into this object may survive it.
    backend.onLoadError      = nullptr;
    backend.onUiScaleChanged = nullptr;
    host.setLookAndFeel(nullptr);
}

// Parents the built-in pieces to the host in their fixed z-order and wires their callbacks.
void ShellChrome::wireBuiltIns()
{
    host.setLookAndFeel(&lookAndFeel);

    host.addAndMakeVisible(transportBar);
    host.addChildComponent(aboutOverlay);
    host.addChildComponent(saveDialog);
    host.addChildComponent(presetBrowser);
    host.addAndMakeVisible(statusBar);

    // Transport bar: mixer / settings / about toggles and the status line.
    transportBar.onMixerToggle    = [this] { showMixer(! mixerVisible); };
    transportBar.onLogoClicked    = [this] { showAbout(true); };
    transportBar.onSettingsToggle = [this] { showSettings(! settingsVisible); };
    transportBar.onStatusUpdate   = [this](const juce::String& name, const juce::String& val)
    {
        statusBar.showParam(name, val);
    };

    aboutOverlay.onDismiss = [this] { showAbout(false); };

    presetBrowser.setFileExtension(backend.getFullPresetExtension());
    presetBrowser.onClose = [this] { showPresetBrowser(false); };

    // Surface preset / state-load failures to the user.
    backend.onLoadError = [this](const juce::String& msg)
    {
        statusBar.showParam("Load failed", msg, MuLookAndFeel::colour(MuLookAndFeel::knobLevel));
    };

    // Upgrade banner: hidden until the async version check finds a newer release online.
    {
        using Id = MuLookAndFeel::ColourIds;
        upgradeBanner.setJustificationType(juce::Justification::centred);
        upgradeBanner.setFont(juce::Font(juce::FontOptions{}.withHeight(11.0f)));
        upgradeBanner.setColour(juce::Label::backgroundColourId, MuLookAndFeel::colour(Id::segmentActiveBg));
        upgradeBanner.setColour(juce::Label::textColourId, MuLookAndFeel::colour(Id::headingText));
        host.addChildComponent(upgradeBanner);
        upgradeBanner.setVisible(false);
        // Whole banner is a click target: it opens the download page.
        upgradeBanner.setInterceptsMouseClicks(true, false);
        upgradeBanner.addMouseListener(this, false);
        upgradeBanner.setMouseCursor(juce::MouseCursor::PointingHandCursor);
    }
}

// Fixed window size from the UI scale, re-applied when the scale changes in settings.
void ShellChrome::applyWindowSize()
{
    host.setSize(mu_ui::s(MuLookAndFeel::kWindowWidth), mu_ui::s(MuLookAndFeel::kWindowHeight));

    // React to runtime scale changes from the settings overlay.
    juce::Component::SafePointer<juce::Component> safeHost(&host);
    backend.onUiScaleChanged = [safeHost](float newScale)
    {
        if (auto* h = safeHost.getComponent())
        {
            mu_ui::scale = newScale;
            h->setSize(mu_ui::s(MuLookAndFeel::kWindowWidth), mu_ui::s(MuLookAndFeel::kWindowHeight));
            h->sendLookAndFeelChange();
            h->resized();
            h->repaint();
        }
    };
}

void ShellChrome::showUpgradeAvailable(const juce::String& latestTag)
{
    const juce::String current   = "v" + juce::String(MU_VERSION_STRING);
    const juce::String available = latestTag;   // the release's own tag, e.g. v1.1.0.972
    upgradeBanner.setText(
        juce::String::fromUTF8("Update available  \xe2\x80\x94  you have ") + current
            + juce::String::fromUTF8("  \xe2\x80\x94  ") + available
            + juce::String::fromUTF8(" is out  \xe2\x80\x94  click here to download"),
        juce::dontSendNotification);
    upgradeAvailable.store(true);
    upgradeBanner.setVisible(true);
    host.resized();
    host.repaint();
}

void ShellChrome::startVersionCheck()
{
    // Ask GitHub for the "latest release" (the same endpoint the website's download page uses),
    // parse the build number out of its tag (vX.Y.0.NNN), and raise the upgrade banner if it beats
    // our local BUILD_NUMBER. Only the build number is compared: it never resets across major /
    // minor bumps, so it alone orders releases.
    //
    // The result is cached PROCESS-WIDE so we fetch at most once per process: reopening the
    // editor, or running many plugin instances in one DAW session, all share the single result
    // instead of each hammering GitHub's unauthenticated API (60 req/hr/IP, after which the call
    // would just fail). The fetch runs on a throwaway thread (it blocks on the network) and fails
    // silently offline.
    //   -1 = not yet checked   0 = checked, nothing newer   >BUILD_NUMBER = newer build
    static std::atomic<int>  s_latestBuild  { -1 };
    static std::atomic<bool> s_fetchStarted { false };
    static juce::String      s_latestTag;   // written before s_latestBuild's release-store

    const int cached = s_latestBuild.load(std::memory_order_acquire);
    if (cached > BUILD_NUMBER) { showUpgradeAvailable(s_latestTag); return; }  // already known
    if (cached >= 0)           return;                                         // already checked, none newer
    if (s_fetchStarted.exchange(true)) return;                                 // a fetch is already in flight

    juce::WeakReference<ShellChrome> safe(this);
    juce::Thread::launch([safe]
    {
        const juce::URL url("https://api.github.com/repos/AndyTuke/mu-family/releases/latest");
        const auto opts = juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
                              .withConnectionTimeoutMs(5000);
        std::unique_ptr<juce::InputStream> stream(url.createInputStream(opts));

        int latestBuild = 0;   // 0 = nothing newer / fetch failed
        juce::String tag;
        if (stream != nullptr)
        {
            const juce::var json = juce::JSON::parse(stream->readEntireStreamAsString());
            tag = json.getProperty("tag_name", {}).toString();   // e.g. "v1.1.0.972"
            if (tag.isNotEmpty())   // build number = final dot-separated segment
                latestBuild = tag.fromLastOccurrenceOf(".", false, false).getIntValue();
        }

        // Cache the verdict for this process (clamp to 0 when nothing newer / failed).
        if (latestBuild > BUILD_NUMBER) s_latestTag = tag;
        s_latestBuild.store(latestBuild > BUILD_NUMBER ? latestBuild : 0, std::memory_order_release);
        if (latestBuild <= BUILD_NUMBER) return;

        juce::MessageManager::callAsync([safe, tag]
        {
            if (safe == nullptr) return;   // host closed mid-check
            safe->showUpgradeAvailable(tag);
        });
    });
}

void ShellChrome::mouseDown(const juce::MouseEvent& e)
{
    // Clicking the upgrade banner opens the download page.
    if (e.eventComponent == &upgradeBanner)
        juce::URL(kDownloadUrl).launchInDefaultBrowser();
}

void ShellChrome::setMainArea(juce::Component* newSidebar, juce::Component* newMainPanel)
{
    if (sidebar != nullptr)   host.removeChildComponent(sidebar);
    if (mainPanel != nullptr) host.removeChildComponent(mainPanel);
    sidebar   = newSidebar;
    mainPanel = newMainPanel;
    if (sidebar != nullptr)   host.addAndMakeVisible(*sidebar);
    if (mainPanel != nullptr) host.addAndMakeVisible(*mainPanel);
    // The main page's knobs take the app colour (the mixer and other overlays keep theirs).
    if (mainPanel != nullptr) MuLookAndFeel::useAppColourKnobs(*mainPanel);
    host.resized();
}

void ShellChrome::setMixerOverlay(juce::Component* overlay)
{
    if (mixerOverlay != nullptr) host.removeChildComponent(mixerOverlay);
    mixerOverlay = overlay;
    if (mixerOverlay != nullptr)
    {
        host.addChildComponent(*mixerOverlay);
        transportBar.setShowMixerToggle(true);
    }
    else
    {
        transportBar.setShowMixerToggle(false);
    }
    host.resized();
}

void ShellChrome::setSettingsOverlay(juce::Component* overlay)
{
    if (settingsOverlay != nullptr) host.removeChildComponent(settingsOverlay);
    settingsOverlay = overlay;
    if (settingsOverlay != nullptr)
        host.addChildComponent(*settingsOverlay);

    // Standard overlay chrome: close returns to the main view.
    if (auto* base = dynamic_cast<SettingsOverlayBase*>(overlay))
        base->onClose = [this] { showSettings(false); };
    host.resized();
}

ShellChrome::Extra* ShellChrome::findExtra(const juce::Component& screen)
{
    for (auto& e : extras)
        if (e.screen == &screen) return &e;
    return nullptr;
}

const ShellChrome::Extra* ShellChrome::findExtra(const juce::Component& screen) const
{
    for (auto& e : extras)
        if (e.screen == &screen) return &e;
    return nullptr;
}

void ShellChrome::addExtraScreen(juce::Component& screen, ExtraKind kind, juce::Component* insertBefore)
{
    jassert(findExtra(screen) == nullptr);
    extras.push_back({ &screen, kind, false });
    const int index = insertBefore != nullptr ? host.getIndexOfChildComponent(insertBefore) : -1;
    host.addChildComponent(screen, index);
}

bool ShellChrome::isExtraScreenVisible(const juce::Component& screen) const
{
    const auto* e = findExtra(screen);
    return e != nullptr && e->visible;
}

// A Modal toggles in place (and takes focus); a Page replaces the main panel.
void ShellChrome::showExtraScreen(juce::Component& screen, bool show)
{
    auto* e = findExtra(screen);
    if (e == nullptr) return;

    if (e->kind == ExtraKind::Modal)
    {
        e->visible = show;
        screen.setVisible(show);
        screen.toFront(true);
        return;
    }

    if (show)
    {
        hideAllOverlays();
        e->visible = true;
        if (mainPanel) mainPanel->setVisible(false);
        screen.setVisible(true);
    }
    else
    {
        e->visible = false;
        screen.setVisible(false);
        if (mainPanel) mainPanel->setVisible(true);   // opening any overlay cleared the mixer (hideAllOverlays) so always return to main
    }
}

bool ShellChrome::anyOverlayVisible() const noexcept
{
    if (mixerVisible || aboutVisible || saveVisible || browserVisible || settingsVisible)
        return true;
    for (auto& e : extras)
        if (e.visible) return true;
    return false;
}

void ShellChrome::hideAllOverlays()
{
    // Cancel any in-progress fade animations before forcing visibility states.
    if (mainPanel)         animator.cancelAnimation(mainPanel, true);
    if (mixerOverlay)      animator.cancelAnimation(mixerOverlay, true);
    if (settingsOverlay)   animator.cancelAnimation(settingsOverlay, true);
    animator.cancelAnimation(&presetBrowser, true);
    for (auto& e : extras)
        if (e.kind == ExtraKind::Page) animator.cancelAnimation(e.screen, true);

    auto setOne = [](juce::Component* c) { if (c) { c->setAlpha(1.0f); c->setVisible(false); } };
    setOne(mixerOverlay);
    setOne(settingsOverlay);
    aboutOverlay.setVisible(false);
    saveDialog.setVisible(false);
    presetBrowser.setVisible(false);
    for (auto& e : extras)
    {
        e.screen->setVisible(false);
        e.visible = false;
    }

    mixerVisible = false;
    transportBar.setMixerActive(false);
    aboutVisible    = false;
    saveVisible     = false;
    browserVisible  = false;
    settingsVisible = false;

    if (mainPanel)
    {
        mainPanel->setAlpha(1.0f);
        mainPanel->setVisible(true);
    }
}

void ShellChrome::fadeSwitch(juce::Component* outgoing, juce::Component* incoming, int durationMs)
{
    if (incoming)
    {
        incoming->setAlpha(0.0f);
        incoming->setVisible(true);
    }
    if (outgoing)
        animator.animateComponent(outgoing, outgoing->getBounds(), 0.0f, durationMs, false, 1.0, 1.0);
    if (incoming)
        animator.animateComponent(incoming, incoming->getBounds(), 1.0f, durationMs, false, 1.0, 1.0);

    if (outgoing)
    {
        juce::Component::SafePointer<juce::Component> safeOut(outgoing);
        juce::Timer::callAfterDelay(durationMs + 10, [safeOut]
        {
            if (auto* out = safeOut.getComponent())
            {
                out->setVisible(false);
                out->setAlpha(1.0f);
            }
        });
    }
}

void ShellChrome::showMixer(bool show)
{
    if (mixerOverlay == nullptr) return;
    if (show) hideAllOverlays();
    mixerVisible = show;
    transportBar.setMixerActive(show);
    if (show)
    {
        if (mainPanel) mixerOverlay->setBounds(mainPanel->getBounds());
        fadeSwitch(mainPanel, mixerOverlay);
    }
    else
    {
        fadeSwitch(mixerOverlay, mainPanel);
    }
}

void ShellChrome::showAbout(bool show)
{
    aboutVisible = show;
    if (show && onAboutOpening)
        onAboutOpening();
    aboutOverlay.setVisible(show);
    aboutOverlay.toFront(false);
}

void ShellChrome::showSaveDialog(bool show)
{
    saveVisible = show;
    if (show && onSaveDialogOpening)
        onSaveDialogOpening();
    saveDialog.setVisible(show);
    saveDialog.toFront(false);
}

void ShellChrome::showPresetBrowser(bool show)
{
    if (show)
    {
        hideAllOverlays();
        presetBrowser.refresh(backend.getPresetsDir());
        browserVisible = true;
        if (mainPanel) mainPanel->setVisible(false);
        presetBrowser.setVisible(true);
    }
    else
    {
        browserVisible = false;
        presetBrowser.setVisible(false);
        if (mainPanel) mainPanel->setVisible(true);   // opening any overlay cleared the mixer (hideAllOverlays) so always return to main
    }
}

void ShellChrome::showSettings(bool show)
{
    if (settingsOverlay == nullptr) return;
    if (show)
    {
        hideAllOverlays();
        settingsVisible = true;
        if (mainPanel) mainPanel->setVisible(false);
        settingsOverlay->setVisible(true);
    }
    else
    {
        settingsVisible = false;
        settingsOverlay->setVisible(false);
        if (mainPanel) mainPanel->setVisible(true);   // opening any overlay cleared the mixer (hideAllOverlays) so always return to main
    }
}

void ShellChrome::setMetalStyle(bool metal, juce::Colour appAccent)
{
    lookAndFeel.setMetalStyle(metal);
    if (! appAccent.isTransparent())
    {
        lookAndFeel.setAppAccent(appAccent);
        upgradeBanner.setColour(juce::Label::backgroundColourId,   // set before the app colour existed
                                MuLookAndFeel::colour(MuLookAndFeel::segmentActiveBg));
    }
    host.sendLookAndFeelChange();   // shared components re-read the style in lookAndFeelChanged()
    host.repaint();
}

void ShellChrome::setScrews(bool on)
{
    lookAndFeel.setScrews(on);
    host.sendLookAndFeelChange();
    host.repaint();
}

void ShellChrome::forwardKnobStatus(juce::Component& root, const StatusVisitor& visitor)
{
    const StatusFn show = [this](const juce::String& name, const juce::String& val) { statusBar.showParam(name, val); };
    // Walk the panel's tree; a component that reports its own status needn't be searched inside.
    for (auto* c : root.getChildren())
    {
        if (auto* k = dynamic_cast<KnobWithLabel*>(c))
        {
            if (! k->onStatusUpdate) k->onStatusUpdate = show;
        }
        else if (visitor && visitor(*c, show))
        {
            // handled by the host
        }
        else
            forwardKnobStatus(*c, visitor);
    }
}

void ShellChrome::paintBackground(juce::Graphics& g) const
{
    g.fillAll(MuLookAndFeel::colour(MuLookAndFeel::windowBackground));
}

void ShellChrome::resized()
{
    using mu_ui::s;
    const int w          = host.getWidth();
    const int h          = host.getHeight();
    const int statusH    = s(MuLookAndFeel::kStatusBarH);
    const int transportH = s(MuLookAndFeel::kTransportBarH);
    const int sidebarW   = (sidebar != nullptr) ? s(MuLookAndFeel::kSidebarW) : 0;
    const int upgradeH   = upgradeAvailable.load()        ? s(kUpgradeBannerH) : 0;
    const int bannerH    = upgradeH;
    const int contentH   = h - transportH - statusH - bannerH;

    const juce::Rectangle<int> mainArea { sidebarW, transportH,
                                          w - sidebarW, contentH };

    transportBar.setBounds(0, 0, w, transportH);
    if (sidebar)   sidebar  ->setBounds(0, transportH, sidebarW, contentH);
    if (mainPanel) mainPanel->setBounds(mainArea);
    if (mixerOverlay)    mixerOverlay   ->setBounds(mainArea);
    if (settingsOverlay) settingsOverlay->setBounds(mainArea);
    presetBrowser.setBounds(mainArea);
    for (auto& e : extras)
        if (e.kind == ExtraKind::Page) e.screen->setBounds(mainArea);

    // Modal overlays span the full host area.
    aboutOverlay.setBounds(host.getLocalBounds());
    for (auto& e : extras)
        if (e.kind == ExtraKind::Modal) e.screen->setBounds(host.getLocalBounds());
    saveDialog.setBounds(host.getLocalBounds());

    // The upgrade banner sits just above the status bar.
    const int by = h - statusH - bannerH;
    if (upgradeH > 0) upgradeBanner.setBounds(0, by, w, upgradeH);

    statusBar.setBounds(0, h - statusH, w, statusH);
}

} // namespace mu_ui
