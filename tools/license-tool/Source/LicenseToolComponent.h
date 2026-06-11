#pragma once
#include <juce_gui_extra/juce_gui_extra.h>

// The MuLicenseTool form. Two jobs (owner-side, never shipped):
//   • Keygen  — generate a product keypair: save the 64-byte secret key, show the public
//               key as the uint8_t[32] to paste into that product's LicenseKey.h.
//   • Issue   — fill name/email/order/expiry/fingerprint, pick the product + its key file,
//               and emit a machine-locked, Ed25519-signed `.lic` (the format the plugin's
//               mu_core::LicenseManager verifies).
class LicenseToolComponent : public juce::Component
{
public:
    LicenseToolComponent();
    void resized() override;
    void paint (juce::Graphics&) override;

private:
    void generateKeypairPressed();
    void issueLicensePressed();
    void log (const juce::String& line);

    juce::Label    titleLabel;

    juce::Label    productLabel;
    juce::ComboBox productBox;

    juce::Label       keyFileLabel;
    juce::TextEditor  keyFileEditor;     // path to the 64-byte secret key file
    juce::TextButton  browseKeyButton    { "Browse..." };

    juce::Label      nameLabel,  emailLabel,  orderLabel,  expiresLabel,  fingerprintLabel;
    juce::TextEditor nameEditor, emailEditor, orderEditor, expiresEditor, fingerprintEditor;

    juce::TextButton keygenButton  { "Generate keypair..." };
    juce::TextButton issueButton   { "Generate .lic" };

    juce::TextEditor outputBox;          // multiline log / public-key dump

    std::unique_ptr<juce::FileChooser> chooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LicenseToolComponent)
};
