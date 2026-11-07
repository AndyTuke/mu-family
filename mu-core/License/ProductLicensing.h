#pragma once

// Definition of ProcessorBase::initLicensing — include this ONLY in a licensed product's
// processor (it uses the offline verifier + online activation, which only licensed products
// compile and link). Unlicensed products never call initLicensing, so they never need it.
#include "Plugin/ProcessorBase.h"
#include "License/LicenseManager.h"
#include "License/OnlineActivation.h"

inline void ProcessorBase::initLicensing(const LicensingConfig& config)
{
    // Callable again to point the licence at another product (mu-Host, as an engine of another product opens): everything
    // below is re-derived, so nothing of the previous product stays.
    licensingEnabled = true;
    licensingProductKey = config.productId != nullptr ? config.productId : "";
    checkoutUrl = config.checkoutUrl != nullptr ? config.checkoutUrl : "";
    suggestedPrice = config.suggestedPrice;
    const juce::File dir = config.contentDir != juce::File() ? config.contentDir : getContentDir();

    // Offline signed .lic in the content folder — checked once; the result is fixed thereafter.
    auto licensedIn = [&](const juce::File& folder)
    {
        return folder != juce::File()
            && mu_core::LicenseManager::check(folder, config.productId, config.licenceFile, config.publicKey).status
                   == mu_core::LicenseStatus::Licensed;
    };
    offlineLicensed = licensedIn(dir) || licensedIn(config.alsoCheckDir);

    // Online activation (Lemon Squeezy). Startup uses a LOCAL-only check so plugin load never
    // blocks on the network; the activation overlay's activateOnlineFn does the real network call.
    const juce::String activationFile = config.activationFile;
    onlineActivated.store(mu_core::OnlineActivation::hasLocalActivation(dir, activationFile)
                              || (config.alsoCheckDir != juce::File() && mu_core::OnlineActivation::hasLocalActivation(config.alsoCheckDir, activationFile)),
                          std::memory_order_relaxed);
    activateOnlineFn = [this, activationFile, dir](const juce::String& key)
    {
        auto outcome = mu_core::OnlineActivation::activate(dir, activationFile, key);
        if (outcome.ok)
        {
            onlineActivated.store(true, std::memory_order_relaxed);
            onActivated();
        }
        return outcome;
    };
}
