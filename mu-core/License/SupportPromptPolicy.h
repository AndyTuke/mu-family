#pragma once

#include <juce_data_structures/juce_data_structures.h>

#include <atomic>

// When the "support mu" prompt is shown. The apps are free and fully functional; an unlicensed
// copy asks politely, once per session, for a pay-what-you-want contribution. Header-only and
// free of any product or UI types so it can be unit-tested on its own (mu-core/Tests).
namespace mu_core::support_prompt
{

// Settings-file key: set once the user ticks "Don't show this again". It lives in the app
// settings file (not APVTS) so it survives across sessions and never travels in a preset.
inline constexpr const char* kHiddenKey = "supportPromptHidden";

// The tip page a supporter's About card links to ("Want to give more?"): one shared pay-what-you-want
// product with no licence key, so a tip never uses up a key or an activation.
inline constexpr const char* kTipUrl = "https://transwarp.lemonsqueezy.com/checkout/buy/a6bc15b0-9e2d-4aad-a4d2-7327a8e56e7b";

// A supporter holds a valid licence key: the offline signed .lic or an online activation.
// A product with no licensing model (never called initLicensing) has no supporters.
inline bool isSupporter(bool licensingEnabled, bool offlineLicensed, bool onlineActivated) noexcept
{
    return licensingEnabled && (offlineLicensed || onlineActivated);
}

// "Once per session" means once per process: every editor opened in this process shares it.
inline std::atomic<bool>& shownThisProcess() noexcept
{
    static std::atomic<bool> shown { false };
    return shown;
}

// Test-only: forget that the prompt was shown, so a test can exercise the first-show path again.
inline void resetShownThisProcessForTesting() noexcept { shownThisProcess().store(false); }

// Should the prompt appear now? Only a licensing product's unlicensed copy sees it, never after
// "Don't show again", and never twice in one process.
inline bool shouldShow(bool hasLicensing, bool isSupporterNow, bool hiddenForever, bool alreadyShown) noexcept
{
    return hasLicensing && ! isSupporterNow && ! hiddenForever && ! alreadyShown;
}

inline bool shouldShowNow(bool hasLicensing, bool isSupporterNow, bool hiddenForever) noexcept
{
    return shouldShow(hasLicensing, isSupporterNow, hiddenForever, shownThisProcess().load());
}

// Record that the prompt is on screen for this process.
inline void markShown() noexcept { shownThisProcess().store(true); }

// "Don't show again" persistence in the app settings file. A null file reads as not hidden.
inline bool isHidden(const juce::PropertiesFile* settings)
{
    return settings != nullptr && settings->getBoolValue(kHiddenKey, false);
}

inline void setHidden(juce::PropertiesFile* settings, bool hidden)
{
    if (settings == nullptr) return;
    settings->setValue(kHiddenKey, hidden);
    settings->saveIfNeeded();
}

} // namespace mu_core::support_prompt
