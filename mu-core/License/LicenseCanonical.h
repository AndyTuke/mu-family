#pragma once
#include <juce_core/juce_core.h>

// The single source of truth for the signed license payload. Both the OFFLINE verifier
// (mu_core::LicenseManager, plugin-side) and the signer (tools/license-tool, MuLicenseTool)
// include this header, so the byte-exact string they each Ed25519-sign / verify can never
// drift — the previous PowerShell+OpenSSL signer hand-rebuilt this string separately, which
// was the most fragile part of the system. Change this format and EVERY existing license
// invalidates (acceptable pre-launch: no licenses are in the wild yet).
namespace mu_core
{

// Build the canonical string that is Ed25519-signed. Fields are sorted alphabetically and
// joined by '\n' (no trailing newline). `fingerprint` machine-locks the license: every
// license carries it, and the verifier checks it matches the running machine, so a `.lic`
// can never be shared between machines (owner decision: machine-lock everything).
inline juce::String buildLicenseCanonical(const juce::String& email,
                                          const juce::String& expires,
                                          const juce::String& fingerprint,
                                          const juce::String& issued,
                                          const juce::String& name,
                                          const juce::String& order,
                                          const juce::String& product)
{
    return "email="       + email       + "\n"
         + "expires="     + expires     + "\n"
         + "fingerprint=" + fingerprint + "\n"
         + "issued="      + issued      + "\n"
         + "name="        + name        + "\n"
         + "order="       + order       + "\n"
         + "product="     + product;
}

} // namespace mu_core
