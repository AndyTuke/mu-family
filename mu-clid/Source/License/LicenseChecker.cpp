#include "LicenseChecker.h"
#include "../../ThirdParty/monocypher/monocypher.h"

// Build the canonical string that was signed by gen_license.ps1.
// Fields are sorted alphabetically — must match the PowerShell tool exactly.
static juce::String buildCanonical(const juce::String& email,
                                   const juce::String& expires,
                                   const juce::String& issued,
                                   const juce::String& name,
                                   const juce::String& order)
{
    return "email="   + email   + "\n"
         + "expires=" + expires + "\n"
         + "issued="  + issued  + "\n"
         + "name="    + name    + "\n"
         + "order="   + order   + "\n"
         + "product=mu-Clid";
}

static bool isExpired(const juce::String& expires)
{
    if (expires.equalsIgnoreCase("lifetime"))
        return false;

    // Expect "YYYY-MM-DD". Compare lexicographically against today — valid because
    // ISO 8601 date strings sort the same way as calendar dates.
    const juce::String today = juce::Time::getCurrentTime().formatted("%Y-%m-%d");
    return expires < today;
}

LicenseChecker::Info LicenseChecker::check(const juce::File& contentDir)
{
    Info result; // default: Demo

    auto licFile = contentDir.getChildFile("muclid.lic");
    if (!licFile.existsAsFile())
        return result;

    auto parsed = juce::JSON::parse(licFile.loadFileAsString());
    auto* obj   = parsed.getDynamicObject();
    if (obj == nullptr)
        return result;

    auto get = [&](const char* key) -> juce::String {
        return obj->getProperty(key).toString();
    };

    const auto email     = get("email");
    const auto expires   = get("expires");
    const auto issued    = get("issued");
    const auto name      = get("name");
    const auto order     = get("order");
    const auto product   = get("product");
    const auto sigBase64 = get("signature");

    // Reject obviously incomplete files
    if (email.isEmpty() || name.isEmpty() || order.isEmpty() || sigBase64.isEmpty())
        return result;

    // Product check — future-proof against cross-product license reuse
    if (product != "mu-Clid")
        return result;

    // Decode base64 signature
    juce::MemoryOutputStream sigStream;
    if (!juce::Base64::convertFromBase64(sigStream, sigBase64))
        return result;
    if (sigStream.getDataSize() != 64)
        return result;

    // Rebuild the canonical payload and verify the Ed25519 signature
    const auto canonical = buildCanonical(email, expires, issued, name, order);
    const auto* msg      = reinterpret_cast<const uint8_t*>(canonical.toRawUTF8());
    const auto  msgSize  = static_cast<size_t>(canonical.getNumBytesAsUTF8());
    const auto* sig      = static_cast<const uint8_t*>(sigStream.getData());

    if (crypto_eddsa_check(sig, kPublicKey, msg, msgSize) != 0)
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
