#include "License/LicenseManager.h"
#include "License/LicenseCanonical.h"
#include "License/MachineFingerprint.h"
#include <monocypher.h>

namespace mu_core
{

static bool isExpired(const juce::String& expires)
{
    if (expires.equalsIgnoreCase("lifetime"))
        return false;

    // Expect "YYYY-MM-DD". Compare lexicographically against today — valid because
    // ISO 8601 date strings sort the same way as calendar dates.
    const juce::String today = juce::Time::getCurrentTime().formatted("%Y-%m-%d");
    return expires < today;
}

LicenseManager::Info LicenseManager::check(const juce::File&   contentDir,
                                           const juce::String& productId,
                                           const juce::String& licenseFilename,
                                           const uint8_t       publicKey[32])
{
    Info result; // default: Demo

    auto licFile = contentDir.getChildFile(licenseFilename);
    if (!licFile.existsAsFile())
        return result;

    auto parsed = juce::JSON::parse(licFile.loadFileAsString());
    auto* obj   = parsed.getDynamicObject();
    if (obj == nullptr)
        return result;

    auto get = [&](const char* key) -> juce::String {
        return obj->getProperty(key).toString();
    };

    const auto email       = get("email");
    const auto expires     = get("expires");
    const auto fingerprint = get("fingerprint");
    const auto issued      = get("issued");
    const auto name        = get("name");
    const auto order       = get("order");
    const auto product     = get("product");
    const auto sigBase64   = get("signature");

    // Reject obviously incomplete files
    if (email.isEmpty() || name.isEmpty() || order.isEmpty() || sigBase64.isEmpty() || fingerprint.isEmpty())
        return result;

    // Product check — a key for one product must not unlock another.
    if (product != productId)
        return result;

    // Machine lock — the license is bound to one machine's fingerprint, so a `.lic` can't
    // be copied to another machine. Checked before the (cheaper-to-skip) crypto verify.
    if (fingerprint != MachineFingerprint::getShortCode())
        return result;

    // Decode base64 signature
    juce::MemoryOutputStream sigStream;
    if (!juce::Base64::convertFromBase64(sigStream, sigBase64))
        return result;
    if (sigStream.getDataSize() != 64)
        return result;

    // Rebuild the canonical payload and verify the Ed25519 signature
    const auto canonical = buildLicenseCanonical(email, expires, fingerprint, issued, name, order, productId);
    const auto* msg      = reinterpret_cast<const uint8_t*>(canonical.toRawUTF8());
    const auto  msgSize  = static_cast<size_t>(canonical.getNumBytesAsUTF8());
    const auto* sig      = static_cast<const uint8_t*>(sigStream.getData());

    if (crypto_eddsa_check(sig, publicKey, msg, msgSize) != 0)
        return result;

    // Signature is valid — check expiry
    if (isExpired(expires))
        return result;

    result.status  = LicenseStatus::Licensed;
    result.name    = name;
    result.email   = email;
    result.order   = order;
    result.expires = expires;
    return result;
}

} // namespace mu_core
