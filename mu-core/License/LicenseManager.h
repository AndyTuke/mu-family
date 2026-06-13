#pragma once
#include <juce_core/juce_core.h>
#include <cstdint>

// Release builds require a valid license (no license → Demo); Debug builds are the
// tester/beta builds that run fully unlocked. The flag is set by the build config in
// the root CMakeLists ($<CONFIG:Release> → 1), never hand-edited — so a build can't
// ship with the wrong gate. This header defines the OFF default so a TU that never
// saw the define still compiles (and defaults to "no license required").
#ifndef MUFAMILY_REQUIRE_LICENSE
 #define MUFAMILY_REQUIRE_LICENSE 0
#endif

namespace mu_core
{

enum class LicenseStatus { Demo, Licensed };

// Family-shared offline license-file verifier (Ed25519 via Monocypher). Each product
// supplies its own product id, license filename, and embedded public key, so a mu-Clid
// license can never unlock mu-Tant — the signed canonical payload carries `product=`,
// which is checked against the caller's productId.
//
// This is the OFFLINE path (a signed `.lic` file dropped next to the plugin). The online
// Lemon Squeezy activation + offline challenge-response fallback land on top of this and
// produce the same Info; see the backlog. mu-core stays plugin-agnostic — the product
// id / filename / key are passed in, never hard-coded here.
class LicenseManager
{
public:
    struct Info
    {
        LicenseStatus status = LicenseStatus::Demo;
        juce::String  name;
        juce::String  email;
        juce::String  order;
        juce::String  expires;
    };

    // Reads <licenseFilename> from contentDir, verifies the Ed25519 signature against
    // publicKey, checks the product id + expiry, and returns the result. Returns Demo on
    // any failure (missing/malformed file, wrong product, bad signature, expired). Call
    // once at startup on the message thread; the result is immutable thereafter.
    static Info check(const juce::File&   contentDir,
                      const juce::String& productId,        // e.g. "mu-Clid"
                      const juce::String& licenseFilename,  // e.g. "muclid.lic"
                      const uint8_t       publicKey[32]);
};

} // namespace mu_core
