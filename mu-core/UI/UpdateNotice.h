#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "BuildNumber.h"               // BUILD_NUMBER, MU_VERSION_STRING: the local version the check compares
#include "Plugin/ShellBackend.h"
#include "Plugin/UpdateChecker.h"
#include "UI/Components/StatusBar.h"

namespace mu_ui
{

// The "Update available" link in a status bar, and everything behind it (docs/design-plugin-family.md,
// "Update check"): the link shows when this product's entry in the site's latest.json is a newer build
// than ours. The data is fetched at most once a day (stored in the app settings) by the shared
// UpdateChecker thread; between fetches the stored answer decides, so the link also goes away as soon
// as the user updates. A click opens the download page and counts as seen; a right click offers "Hide
// this notice"; the link never shows while suppressed (Performance mode) or when the user turned the
// check off. Used by ShellChrome and by editors that have their own status bar (mu-Clid MIDI).
class UpdateNotice
{
public:
    UpdateNotice(mu_core::ShellBackend& shellBackend, StatusBar& bar)
        : backend(shellBackend), statusBar(bar)
    {
        statusBar.onUpdateClicked = [this]
        {
            juce::URL(mu_core::update_check::safeUrl(entry.download)).launchInDefaultBrowser();
            dismiss();
        };
        statusBar.onUpdateHideRequested = [this]
        {
            juce::PopupMenu menu;
            menu.addItem(1, "Hide this notice");
            juce::WeakReference<UpdateNotice> safe(this);
            menu.showMenuAsync(juce::PopupMenu::Options(), [safe](int r)
            {
                if (r == 1 && safe != nullptr) safe->dismiss();
            });
        };
        backend.onUpdateCheckToggled = [this] { start(); };
    }

    ~UpdateNotice()
    {
        // The backend and the status bar can outlive this object's owner: no callback into it may survive.
        backend.onUpdateCheckToggled    = nullptr;
        statusBar.onUpdateClicked       = nullptr;
        statusBar.onUpdateHideRequested = nullptr;
    }

    // Decides whether the link shows, fetching if a day has passed. Call when the editor opens and when
    // the setting changes.
    void start()
    {
        namespace uc = mu_core::update_check;
        auto* settings = backend.getAppSettingsFile();
        const juce::String slug = backend.getProductSlug();
        if (settings == nullptr || slug.isEmpty() || ! uc::isEnabled(settings)) { entry = {}; refresh(); return; }

        entry = uc::storedLatest(settings);
        refresh();   // the stored answer shows while a fetch runs
        if (! uc::isDue(juce::Time::currentTimeMillis(), uc::lastCheck(settings)) || fetchInFlight) return;

        fetchInFlight = true;
        juce::WeakReference<UpdateNotice> safeSelf(this);
        checker->request([safe = safeSelf, slug](const juce::String& body)
        {
            if (safe == nullptr) return;   // the owner closed mid-check
            safe->fetchInFlight = false;
            uc::Entry fetched;
            auto* s = safe->backend.getAppSettingsFile();
            if (s == nullptr || ! uc::parse(body, slug, fetched)) return;   // unreadable data: stay silent
            uc::storeLatest(s, fetched, juce::Time::currentTimeMillis());
            safe->entry = fetched;
            safe->refresh();
        });
    }

    // Performance mode hides the link; it returns afterwards.
    void setSuppressed(bool suppressed) { isSuppressed = suppressed; refresh(); }

private:
    JUCE_DECLARE_WEAK_REFERENCEABLE(UpdateNotice)

    void refresh()
    {
        namespace uc = mu_core::update_check;
        auto* settings = backend.getAppSettingsFile();
        const bool wanted = settings != nullptr
                            && uc::shouldShow(uc::isEnabled(settings), entry.build, uc::dismissedBuild(settings), BUILD_NUMBER);
        const juce::String tip = juce::String(entry.version.isNotEmpty() ? entry.version : "v1.1." + juce::String(entry.build))
                                 + " is out (you have v" + juce::String(MU_VERSION_STRING)
                                 + "). Click to open the download page.";
        statusBar.setUpdateLinkVisible(wanted && ! isSuppressed, tip);
    }

    // A click or "Hide this notice": remember the build, so the link only returns for a newer one.
    void dismiss()
    {
        mu_core::update_check::setDismissedBuild(backend.getAppSettingsFile(), entry.build);
        refresh();
    }

    mu_core::ShellBackend& backend;
    StatusBar&             statusBar;
    mu_core::update_check::Entry entry;   // this product's latest build (stored or just fetched)
    bool isSuppressed  = false;
    bool fetchInFlight = false;
    juce::SharedResourcePointer<mu_core::update_check::UpdateChecker> checker;   // held for the owner's life so the fetch is not cancelled
};

} // namespace mu_ui
