#include "License/OnlineActivation.h"
#include "License/LemonSqueezyClient.h"
#include "License/ActivationStore.h"
#include "License/MachineFingerprint.h"

namespace mu_core
{

// Offline grace: a machine that validated online may keep running for this long without a
// successful re-validate (covers a user who's temporarily offline). After it lapses, a launch
// with no network drops to Demo until the next successful validate.
static constexpr juce::int64 kGraceMs = (juce::int64) 30 * 24 * 60 * 60 * 1000;

static juce::String nowMs() { return juce::String (juce::Time::getCurrentTime().toMilliseconds()); }

OnlineActivationOutcome OnlineActivation::activate (const juce::File&   contentDir,
                                                    const juce::String& activationFilename,
                                                    const juce::String& licenseKey)
{
    OnlineActivationOutcome o;

    const auto fingerprint = MachineFingerprint::getShortCode();
    const auto res = LemonSqueezyClient::activate (licenseKey.trim(), fingerprint);

    o.networkError = res.networkError;
    o.message      = res.message;
    o.customerName = res.customerName;
    o.customerEmail= res.customerEmail;

    const bool usable = res.ok
                     && res.instanceId.isNotEmpty()
                     && res.status != "expired"
                     && res.status != "disabled";
    if (usable)
    {
        ActivationRecord rec;
        rec.licenseKey    = licenseKey.trim();
        rec.instanceId    = res.instanceId;
        rec.lastValidated = nowMs();
        ActivationStore::save (contentDir, activationFilename, rec);
        o.ok = true;
        if (o.message.isEmpty())
            o.message = "Activated.";
    }
    else if (! res.networkError && o.message.isEmpty())
    {
        o.message = "That license key could not be activated (limit reached, expired, or invalid).";
    }
    return o;
}

OnlineActivationOutcome OnlineActivation::validateStored (const juce::File&   contentDir,
                                                         const juce::String& activationFilename)
{
    OnlineActivationOutcome o;

    auto rec = ActivationStore::load (contentDir, activationFilename);
    if (! rec.isValid())
        return o; // never activated online — caller falls back to the offline `.lic`

    const auto res = LemonSqueezyClient::validate (rec.licenseKey, rec.instanceId);

    if (res.networkError)
    {
        // Offline: trust the stored activation within the grace window.
        o.networkError = true;
        const auto last = rec.lastValidated.getLargeIntValue();
        o.ok = (last > 0) && (juce::Time::getCurrentTime().toMilliseconds() - last < kGraceMs);
        if (! o.ok)
            o.message = "Offline and the activation grace period has lapsed.";
        return o;
    }

    if (res.ok && res.status == "active")
    {
        rec.lastValidated = nowMs();
        ActivationStore::save (contentDir, activationFilename, rec);
        o.ok = true;
    }
    else
    {
        // Revoked / expired / refunded — stop trusting it.
        ActivationStore::clear (contentDir, activationFilename);
        o.message = res.message.isNotEmpty() ? res.message : "License is no longer valid.";
    }
    return o;
}

void OnlineActivation::deactivate (const juce::File& contentDir, const juce::String& activationFilename)
{
    ActivationStore::clear (contentDir, activationFilename);
}

bool OnlineActivation::hasLocalActivation (const juce::File& contentDir, const juce::String& activationFilename)
{
    return ActivationStore::load (contentDir, activationFilename).isValid();
}

} // namespace mu_core
