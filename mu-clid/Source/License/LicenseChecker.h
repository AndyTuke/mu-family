#pragma once
#include <juce_core/juce_core.h>
#include <cstdint>

enum class LicenseStatus { Demo, Licensed };

// Set to false before shipping v1.0. While true, isLicensed() always returns
// true regardless of whether a muclid.lic file exists — beta testers get full
// functionality without needing a license.
static constexpr bool kBetaBuild = true;

class LicenseChecker
{
public:
    struct Info
    {
        LicenseStatus status  = LicenseStatus::Demo;
        juce::String  name;
        juce::String  email;
        juce::String  order;
        juce::String  expires;
    };

    // Reads muclid.lic from contentDir, verifies the Ed25519 signature,
    // and returns license info. Returns Demo status on any failure.
    // Call once at startup from the message thread; result is read-only thereafter.
    static Info check(const juce::File& contentDir);

private:
    // 32-byte Ed25519 public key embedded at build time.
    // !! REPLACE with output of tools/keygen.ps1 before distributing !!
    // The private key never lives in the repo — only this public key.
    static constexpr uint8_t kPublicKey[32] = {
        0xcc, 0xa8, 0xc7, 0x6d, 0x70, 0x88, 0x8b, 0x51, 0x59, 0x7c, 0xea, 0xe5, 0x33, 0x7f, 0xcb, 0xe1,
        0xb4, 0xa8, 0x01, 0x4f, 0x68, 0xfb, 0x9b, 0xf0, 0x57, 0xcf, 0xb1, 0xd9, 0x85, 0x37, 0x3e, 0x74,
    };
};
