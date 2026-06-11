#pragma once
#include <juce_core/juce_core.h>

// Online activation against the Lemon Squeezy License API (the PRIMARY path; the offline
// signed `.lic` is the fallback). Store #377845 (transwarp.lemonsqueezy.com).
//
//   activate() — POST /v1/licenses/activate  (license_key + instance_name=fingerprint)
//                → an instance id we persist and re-validate on later launches.
//   validate() — POST /v1/licenses/validate  (license_key + instance_id).
//
// THREADING: these block on the network — call ONLY from a background thread (the activation
// overlay does), NEVER the audio or message thread. mu-core stays plugin-agnostic: the caller
// supplies the license key + fingerprint; no product identity is hard-coded here.
namespace mu_core
{

struct LemonSqueezyResult
{
    bool         ok = false;     // activated / valid
    bool         networkError = false; // true = couldn't reach LS (→ offline fallback), vs. a rejection
    juce::String instanceId;     // returned by activate; echoed by validate
    juce::String status;         // license_key.status: "active" / "expired" / "disabled"
    juce::String customerName;
    juce::String customerEmail;
    juce::String expiresAt;      // license_key.expires_at (ISO or empty = lifetime)
    juce::String message;        // human-readable error/note from LS or the client
};

class LemonSqueezyClient
{
public:
    static LemonSqueezyResult activate (const juce::String& licenseKey,
                                        const juce::String& instanceName);

    static LemonSqueezyResult validate (const juce::String& licenseKey,
                                        const juce::String& instanceId);

private:
    static LemonSqueezyResult post (const juce::String& endpoint,
                                    const juce::StringPairArray& params);
};

} // namespace mu_core
