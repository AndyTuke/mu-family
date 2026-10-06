#pragma once

// Definition of ProcessorBase::initLicensing — include this ONLY in a licensed product's
// processor (it uses the offline verifier + online activation, which only licensed products
// compile and link). Unlicensed products never call initLicensing, so they never need it.
#include "Plugin/ProcessorBase.h"
#include "License/LicenseManager.h"
#include "License/OnlineActivation.h"

inline void ProcessorBase::initLicensing(const LicensingConfig& config)
{
    licensingEnabled = true;

    // Offline signed .lic in the content folder — checked once; the result is fixed thereafter.
    offlineLicensed = mu_core::LicenseManager::check(getContentDir(), config.productId,
                                                     config.licenceFile, config.publicKey).status
                      == mu_core::LicenseStatus::Licensed;

    // Online activation (Lemon Squeezy). Startup uses a LOCAL-only check so plugin load never
    // blocks on the network; the activation overlay's activateOnlineFn does the real network call.
    const juce::String activationFile = config.activationFile;
    if (mu_core::OnlineActivation::hasLocalActivation(getContentDir(), activationFile))
        onlineActivated.store(true, std::memory_order_relaxed);
    activateOnlineFn = [this, activationFile](const juce::String& key)
    {
        auto outcome = mu_core::OnlineActivation::activate(getContentDir(), activationFile, key);
        if (outcome.ok)
        {
            onlineActivated.store(true, std::memory_order_relaxed);
            onActivated();
        }
        return outcome;
    };
}
