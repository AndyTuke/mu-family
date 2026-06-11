#pragma once
#include <juce_core/juce_core.h>

// Orchestrates the online (Lemon Squeezy) activation path on top of LemonSqueezyClient +
// ActivationStore + MachineFingerprint: activate a key, persist the instance, and re-validate
// stored activations at startup (with an offline grace window). Compiled per LICENSED product
// (added to its target_sources alongside the verifier) — it's the only piece that reaches the
// network, so unlicensed siblings never pull it in. mu-core stays plugin-agnostic: the caller
// passes its content dir + activation filename, no product identity is baked in.
namespace mu_core
{

struct OnlineActivationOutcome
{
    bool         ok = false;            // machine is activated/valid
    bool         networkError = false;  // couldn't reach LS (caller may keep offline state)
    juce::String message;
    juce::String customerName;
    juce::String customerEmail;
};

struct OnlineActivation
{
    // Activate a freshly-entered license key against this machine's fingerprint; persists the
    // returned instance id on success. Blocks on the network — call off the message thread.
    static OnlineActivationOutcome activate (const juce::File&   contentDir,
                                             const juce::String& activationFilename,
                                             const juce::String& licenseKey);

    // Re-validate the stored activation at startup. Valid → refreshes the grace timestamp;
    // revoked/expired → clears the stored record; offline → accepts within a 30-day grace.
    static OnlineActivationOutcome validateStored (const juce::File&   contentDir,
                                                   const juce::String& activationFilename);

    static void deactivate (const juce::File& contentDir, const juce::String& activationFilename);

    // Local-only (NO network) check: is a prior activation stored? Used at startup so plugin
    // load never blocks on the network — a stored activation is trusted optimistically; the
    // online re-validate/revocation pass is a documented follow-up.
    static bool hasLocalActivation (const juce::File& contentDir, const juce::String& activationFilename);
};

} // namespace mu_core
