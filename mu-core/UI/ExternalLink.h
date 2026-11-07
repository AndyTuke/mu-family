#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

// Every link that leaves the app (browser, mail client) goes through here, so an automated UI test can swap the launcher
// for a recorder instead of opening Edge or Outlook on the tester's machine.
namespace mu_core
{
    inline std::function<void(const juce::URL&)>& externalLauncher()
    {
        static std::function<void(const juce::URL&)> launcher;
        return launcher;
    }

    inline void openExternally(const juce::URL& url)
    {
        if (auto& l = externalLauncher()) l(url);
        else url.launchInDefaultBrowser();
    }

    // A hyperlink button whose click opens its URL through openExternally.
    struct ExternalLinkButton : juce::HyperlinkButton
    {
        using juce::HyperlinkButton::HyperlinkButton;
        void clicked() override { openExternally(getURL()); }
    };
}
