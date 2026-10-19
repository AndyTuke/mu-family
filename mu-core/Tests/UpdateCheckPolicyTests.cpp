#include "Plugin/UpdateCheckPolicy.h"

namespace uc = mu_core::update_check;

// The update-check rules: how latest.json is read, when a fetch is due, when the link shows, which
// page it opens, and what is kept in the app settings.
struct UpdateCheckPolicyTests : juce::UnitTest
{
    UpdateCheckPolicyTests() : juce::UnitTest("Update check policy", "mu-core") {}

    void runTest() override
    {
        const juce::String json = R"({"schema":1,"build":1500,"released":"2026-11-01","products":{
            "mu-tant":{"version":"v1.1.1500","build":1500,"notes":"https://transwarp.me/n","download":"https://transwarp.me/download/mu-tant"},
            "mu-on":{"version":"v1.1.1490","build":1490},"bad":{"build":0}}})";

        beginTest("parse reads one product's entry and ignores unknown fields");
        {
            uc::Entry e;
            expect(uc::parse(json, "mu-tant", e));
            expectEquals(e.build, 1500);
            expectEquals(e.version, juce::String("v1.1.1500"));
            expectEquals(e.download, juce::String("https://transwarp.me/download/mu-tant"));
            expect(uc::parse(json, "mu-on", e));
            expectEquals(e.build, 1490);
            expect(e.download.isEmpty());
        }

        beginTest("parse stays silent for a missing product, a zero build, an unknown schema and junk");
        {
            uc::Entry e;
            expect(! uc::parse(json, "mu-toni", e));
            expect(! uc::parse(json, "bad", e));
            expect(! uc::parse(json.replace("\"schema\":1", "\"schema\":2"), "mu-tant", e));
            expect(! uc::parse("not json", "mu-tant", e));
            expect(! uc::parse({}, "mu-tant", e));
        }

        beginTest("a fetch is due after 24 h, or when the timestamp is missing or in the future");
        {
            const juce::int64 now = 10'000'000'000LL;
            expect(uc::isDue(now, 0));
            expect(uc::isDue(now, now + 1000));
            expect(! uc::isDue(now, now - uc::kIntervalMs + 1));
            expect(uc::isDue(now, now - uc::kIntervalMs));
        }

        beginTest("the link shows only for a newer build the user has not hidden, and only when enabled");
        {
            expect(uc::shouldShow(true, 1500, 0, 1415));
            expect(! uc::shouldShow(true, 1415, 0, 1415));    // same build
            expect(! uc::shouldShow(true, 1400, 0, 1415));    // a Debug build above the entry
            expect(! uc::shouldShow(false, 1500, 0, 1415));   // switched off
            expect(! uc::shouldShow(true, 1500, 1500, 1415)); // hidden for that build
            expect(uc::shouldShow(true, 1501, 1500, 1415));   // a still newer build returns
        }

        beginTest("only a page on our own site is opened from the data file");
        {
            expectEquals(uc::safeUrl("https://transwarp.me/download/mu-on"), juce::String("https://transwarp.me/download/mu-on"));
            expectEquals(uc::safeUrl("https://evil.example/x"), juce::String(uc::kDownloadPage));
            expectEquals(uc::safeUrl("https://transwarp.me.evil.example/"), juce::String(uc::kDownloadPage));
            expectEquals(uc::safeUrl({}), juce::String(uc::kDownloadPage));
        }

        beginTest("settings: on by default, a null file reads as defaults, values round-trip");
        {
            expect(uc::isEnabled(nullptr));
            expectEquals(uc::dismissedBuild(nullptr), 0);
            uc::setEnabled(nullptr, false);   // no crash

            const auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("mu-update-check-test");
            dir.createDirectory();
            const auto file = dir.getChildFile("settings.xml");
            file.deleteFile();
            {
                juce::PropertiesFile::Options o;
                juce::PropertiesFile pf(file, o);
                expect(uc::isEnabled(&pf));
                uc::setEnabled(&pf, false);
                expect(! uc::isEnabled(&pf));
                uc::setDismissedBuild(&pf, 1500);
                expectEquals(uc::dismissedBuild(&pf), 1500);
                uc::Entry e; e.build = 1500; e.version = "v1.1.1500"; e.download = "https://transwarp.me/d";
                uc::storeLatest(&pf, e, 123456789LL);
                const auto back = uc::storedLatest(&pf);
                expectEquals(back.build, 1500);
                expectEquals(back.version, e.version);
                expectEquals(back.download, e.download);
                expectEquals((int) (uc::lastCheck(&pf) / 1000), 123456);
            }
            dir.deleteRecursively();
        }
    }
};

static UpdateCheckPolicyTests updateCheckPolicyTests;
