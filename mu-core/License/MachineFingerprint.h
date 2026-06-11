#pragma once
#include <juce_core/juce_core.h>

// A stable, privacy-preserving per-machine identifier. Used two ways:
//   (1) online  — sent to Lemon Squeezy as the activation `instance_name`;
//   (2) offline — shown to the user as a "challenge" they send the owner, who issues a
//                 `.lic` bound to it; the verifier then checks the running machine matches.
// Header-only/inline so it folds into the per-product verifier TU (no extra CMake source).
namespace mu_core
{

struct MachineFingerprint
{
    // The raw OS-level device id. Not displayed — it can be long/PII-ish; we hash it.
    static juce::String getRaw()
    {
        // getUniqueDeviceID() is stable across reboots/reinstalls on every supported OS;
        // fall back to the (less stable) machine-identifier list if it's ever empty.
        auto id = juce::SystemStats::getUniqueDeviceID();
        if (id.isEmpty())
            id = juce::SystemStats::getMachineIdentifiers(juce::SystemStats::MachineIdFlags::macAddresses)
                     .joinIntoString("-");
        return id;
    }

    // The short, typeable challenge code (e.g. "A1B2-C3D4-E5F6"). A 64-bit non-crypto hash
    // of the raw id, hex, grouped in 4s — deterministic, so issue-time == verify-time. A
    // crypto hash isn't needed (the Ed25519 signature over the fingerprint is the tamper
    // boundary); juce::String::hashCode64 lives in juce_core, which every TU links (MD5/SHA256
    // are in juce_cryptography, which the per-product verifier TU does not).
    static juce::String getShortCode()
    {
        const juce::int64 h = getRaw().hashCode64();
        const auto hex = juce::String::toHexString (h).paddedLeft ('0', 16).toUpperCase();

        juce::String code;
        for (int i = 0; i < 12; ++i)
        {
            if (i > 0 && (i % 4) == 0)
                code << '-';
            code << hex[i];
        }
        return code;
    }
};

} // namespace mu_core
