#pragma once

// When the licence reminder is shown. The apps are free and fully functional; an unlicensed copy shows the
// polite reminder every time an instance loads (a fixed-price licence removes it). "Maybe later" closes it for
// that instance only. Header-only and free of any product or UI types so it can be unit-tested on its own.
namespace mu_core::support_prompt
{

// A supporter holds a valid licence key: the offline signed .lic or an online activation.
// A product with no licensing model (never called initLicensing) has no supporters.
inline bool isSupporter(bool licensingEnabled, bool offlineLicensed, bool onlineActivated) noexcept
{
    return licensingEnabled && (offlineLicensed || onlineActivated);
}

// Should the reminder appear now? Only a licensing product's unlicensed copy sees it, and not again once the user
// has dismissed it ("Maybe later") in this instance.
inline bool shouldShow(bool hasLicensing, bool isSupporterNow, bool dismissedThisInstance) noexcept
{
    return hasLicensing && ! isSupporterNow && ! dismissedThisInstance;
}

} // namespace mu_core::support_prompt
