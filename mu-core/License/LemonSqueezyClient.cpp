#include "License/LemonSqueezyClient.h"

namespace mu_core
{

// Parse the shared shape of LS license responses (activate + validate return the same
// nested objects). `okField` is the per-endpoint success flag ("activated" / "valid").
static LemonSqueezyResult parseResponse (const juce::String& body, const char* okField)
{
    LemonSqueezyResult r;

    auto json = juce::JSON::parse (body);
    auto* obj = json.getDynamicObject();
    if (obj == nullptr)
    {
        r.message = "Unexpected response from Lemon Squeezy.";
        return r;
    }

    r.ok = (bool) obj->getProperty (okField);

    if (auto* lk = obj->getProperty ("license_key").getDynamicObject())
    {
        r.status    = lk->getProperty ("status").toString();
        r.expiresAt = lk->getProperty ("expires_at").toString();
    }
    if (auto* inst = obj->getProperty ("instance").getDynamicObject())
        r.instanceId = inst->getProperty ("id").toString();
    if (auto* meta = obj->getProperty ("meta").getDynamicObject())
    {
        r.customerName  = meta->getProperty ("customer_name").toString();
        r.customerEmail = meta->getProperty ("customer_email").toString();
    }

    // LS puts a reason string in "error" when the flag is false.
    const auto err = obj->getProperty ("error").toString();
    if (err.isNotEmpty())
        r.message = err;

    return r;
}

LemonSqueezyResult LemonSqueezyClient::post (const juce::String& endpoint,
                                             const juce::StringPairArray& params)
{
    LemonSqueezyResult r;

    juce::URL url (endpoint);
    url = url.withParameters (params); // POST: present as form fields when withPOSTData unset

    int statusCode = 0;
    juce::StringPairArray responseHeaders;

    auto options = juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inPostData)
                       .withExtraHeaders ("Accept: application/json")
                       .withConnectionTimeoutMs (8000)
                       .withResponseHeaders (&responseHeaders)
                       .withStatusCode (&statusCode);

    std::unique_ptr<juce::InputStream> stream (url.createInputStream (options));
    if (stream == nullptr)
    {
        r.networkError = true;
        r.message = "Could not reach the activation server (offline?).";
        return r;
    }

    const juce::String body = stream->readEntireStreamAsString();

    // LS returns 400 with a JSON {error,...} for rejections — still parse it for the message,
    // but a 5xx / empty body is a server/network problem → fall back offline.
    if (statusCode >= 500 || body.isEmpty())
    {
        r.networkError = true;
        r.message = "Activation server error (" + juce::String (statusCode) + ").";
        return r;
    }

    return parseResponse (body, endpoint.contains ("activate") ? "activated" : "valid");
}

LemonSqueezyResult LemonSqueezyClient::activate (const juce::String& licenseKey,
                                                 const juce::String& instanceName)
{
    juce::StringPairArray p;
    p.set ("license_key",   licenseKey);
    p.set ("instance_name", instanceName);
    return post ("https://api.lemonsqueezy.com/v1/licenses/activate", p);
}

LemonSqueezyResult LemonSqueezyClient::validate (const juce::String& licenseKey,
                                                 const juce::String& instanceId)
{
    juce::StringPairArray p;
    p.set ("license_key", licenseKey);
    if (instanceId.isNotEmpty())
        p.set ("instance_id", instanceId);
    return post ("https://api.lemonsqueezy.com/v1/licenses/validate", p);
}

} // namespace mu_core
