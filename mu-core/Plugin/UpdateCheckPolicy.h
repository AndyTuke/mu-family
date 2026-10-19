#pragma once

#include <juce_data_structures/juce_data_structures.h>

// When the "Update available" link shows. Each product compares its own entry in the site's
// latest.json against its compiled build number; the logic here is header-only and free of any
// UI or network type so it can be unit-tested on its own (mu-core/Tests).
namespace mu_core::update_check
{

inline constexpr const char* kUrl          = "https://transwarp.me/latest.json";
inline constexpr const char* kDownloadPage = "https://transwarp.me/download";
inline constexpr const char* kTrustedPrefix = "https://transwarp.me/";
inline constexpr int         kSchema       = 1;
inline constexpr juce::int64 kIntervalMs   = 24LL * 60 * 60 * 1000;   // at most one fetch a day

// Settings-file keys: the machine-wide app settings, never APVTS, so a project reload cannot turn the check back on.
inline constexpr const char* kEnabledKey   = "updateCheckEnabled";
inline constexpr const char* kLastCheckKey = "updateLastCheck";      // ms since epoch of the last good fetch
inline constexpr const char* kLatestKey    = "updateLatestBuild";
inline constexpr const char* kVersionKey   = "updateLatestVersion";
inline constexpr const char* kUrlKey       = "updateLatestUrl";
inline constexpr const char* kDismissedKey = "updateDismissedBuild";

// One product's entry from latest.json.
struct Entry
{
    int          build = 0;
    juce::String version;
    juce::String download;   // the per-product page; empty means the shared download page
};

// Reads one product's entry. An unknown schema, a missing product or a bad build give false, so
// the caller stays silent.
inline bool parse(const juce::String& json, const juce::String& slug, Entry& out)
{
    const juce::var root = juce::JSON::parse(json);
    if (! root.isObject() || (int) root.getProperty("schema", 0) != kSchema) return false;
    const juce::var entry = root.getProperty("products", {}).getProperty(slug, {});
    if (! entry.isObject()) return false;
    const int build = (int) entry.getProperty("build", 0);
    if (build <= 0) return false;
    out.build    = build;
    out.version  = entry.getProperty("version", {}).toString();
    out.download = entry.getProperty("download", {}).toString();
    return true;
}

inline bool isNewer(int latestBuild, int currentBuild) noexcept { return latestBuild > currentBuild; }

// A missing (0) or future timestamp counts as due.
inline bool isDue(juce::int64 nowMs, juce::int64 lastCheckMs) noexcept
{
    return lastCheckMs <= 0 || lastCheckMs > nowMs || nowMs - lastCheckMs >= kIntervalMs;
}

// The link shows for a build newer than ours and newer than any the user has hidden.
inline bool shouldShow(bool enabled, int latestBuild, int dismissedBuild, int currentBuild) noexcept
{
    return enabled && isNewer(latestBuild, currentBuild) && latestBuild > dismissedBuild;
}

// Only a page on our own site is opened from the data file; anything else falls back to the shared page.
inline juce::String safeUrl(const juce::String& fromFile)
{
    return fromFile.startsWith(kTrustedPrefix) ? fromFile : juce::String(kDownloadPage);
}

// ─── Persistence (null file = defaults) ──────────────────────────────────────
inline bool isEnabled(const juce::PropertiesFile* s) { return s == nullptr || s->getBoolValue(kEnabledKey, true); }
inline void setEnabled(juce::PropertiesFile* s, bool on)
{
    if (s == nullptr) return;
    s->setValue(kEnabledKey, on);
    s->saveIfNeeded();
}
inline int  dismissedBuild(const juce::PropertiesFile* s) { return s != nullptr ? s->getIntValue(kDismissedKey, 0) : 0; }
inline void setDismissedBuild(juce::PropertiesFile* s, int build)
{
    if (s == nullptr) return;
    s->setValue(kDismissedKey, build);
    s->saveIfNeeded();
}

inline Entry storedLatest(const juce::PropertiesFile* s)
{
    Entry e;
    if (s == nullptr) return e;
    e.build    = s->getIntValue(kLatestKey, 0);
    e.version  = s->getValue(kVersionKey);
    e.download = s->getValue(kUrlKey);
    return e;
}
inline juce::int64 lastCheck(const juce::PropertiesFile* s) { return s != nullptr ? s->getValue(kLastCheckKey).getLargeIntValue() : 0; }

inline void storeLatest(juce::PropertiesFile* s, const Entry& e, juce::int64 nowMs)
{
    if (s == nullptr) return;
    s->setValue(kLatestKey, e.build);
    s->setValue(kVersionKey, e.version);
    s->setValue(kUrlKey, e.download);
    s->setValue(kLastCheckKey, juce::String(nowMs));
    s->saveIfNeeded();
}

} // namespace mu_core::update_check
