#pragma once
#include <juce_core/juce_core.h>

// Persists the online activation so a machine that activated once doesn't re-enter its key
// every launch — we store the license key + the LS instance id and re-validate on startup.
// One small JSON file in the content dir (e.g. "muclid.activation"), next to the offline
// `.lic`. Header-only; no crypto (the LS server is the authority for the online path).
namespace mu_core
{

struct ActivationRecord
{
    juce::String licenseKey;
    juce::String instanceId;
    juce::String lastValidated; // ISO date of the last successful online validate (grace window)

    bool isValid() const { return licenseKey.isNotEmpty() && instanceId.isNotEmpty(); }
};

struct ActivationStore
{
    static ActivationRecord load (const juce::File& contentDir, const juce::String& filename)
    {
        ActivationRecord rec;
        auto f = contentDir.getChildFile (filename);
        if (! f.existsAsFile())
            return rec;

        // Hold the parsed var in a named local: getDynamicObject() returns a raw pointer
        // owned by the var's reference count, so the var must outlive the property reads.
        // (A temporary in the `if` condition is destroyed before the body → use-after-free.)
        const auto parsed = juce::JSON::parse (f.loadFileAsString());
        if (auto* obj = parsed.getDynamicObject())
        {
            rec.licenseKey    = obj->getProperty ("license_key").toString();
            rec.instanceId    = obj->getProperty ("instance_id").toString();
            rec.lastValidated = obj->getProperty ("last_validated").toString();
        }
        return rec;
    }

    static void save (const juce::File& contentDir, const juce::String& filename,
                      const ActivationRecord& rec)
    {
        contentDir.createDirectory();
        auto* obj = new juce::DynamicObject();
        obj->setProperty ("license_key",    rec.licenseKey);
        obj->setProperty ("instance_id",    rec.instanceId);
        obj->setProperty ("last_validated", rec.lastValidated);
        contentDir.getChildFile (filename).replaceWithText (juce::JSON::toString (juce::var (obj)));
    }

    static void clear (const juce::File& contentDir, const juce::String& filename)
    {
        contentDir.getChildFile (filename).deleteFile();
    }
};

} // namespace mu_core
