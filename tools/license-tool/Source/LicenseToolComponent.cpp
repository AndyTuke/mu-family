#include "LicenseToolComponent.h"
#include "Crypto.h"
#include "License/LicenseCanonical.h"   // mu_core::buildLicenseCanonical — shared with the verifier

namespace
{
    // Products the tool can issue for + the public-key array variable name to paste into
    // each one's LicenseKey.h. Keep in sync with the product LicenseKey.h files.
    struct ProductInfo { const char* id; const char* keyVar; };
    const ProductInfo kProducts[] = {
        { "mu-Clid", "mu_clid::kLicensePublicKey" },
        { "mu-Tant", "mu_tant::kLicensePublicKey" },
    };
}

LicenseToolComponent::LicenseToolComponent()
{
    titleLabel.setText ("mu License Tool", juce::dontSendNotification);
    titleLabel.setFont (juce::Font (juce::FontOptions{}.withHeight (20.0f).withStyle ("Bold")));
    addAndMakeVisible (titleLabel);

    productLabel.setText ("Product", juce::dontSendNotification);
    addAndMakeVisible (productLabel);
    for (int i = 0; i < (int) std::size (kProducts); ++i)
        productBox.addItem (kProducts[(size_t) i].id, i + 1);
    productBox.setSelectedId (1);
    addAndMakeVisible (productBox);

    keyFileLabel.setText ("Secret key file", juce::dontSendNotification);
    addAndMakeVisible (keyFileLabel);
    keyFileEditor.setMultiLine (false);
    addAndMakeVisible (keyFileEditor);
    addAndMakeVisible (browseKeyButton);
    browseKeyButton.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser> ("Select the product's secret key file");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
            [this] (const juce::FileChooser& fc)
            {
                if (fc.getResult() != juce::File())
                    keyFileEditor.setText (fc.getResult().getFullPathName());
            });
    };

    auto initField = [this] (juce::Label& l, const juce::String& text, juce::TextEditor& e)
    {
        l.setText (text, juce::dontSendNotification);
        addAndMakeVisible (l);
        e.setMultiLine (false);
        addAndMakeVisible (e);
    };
    initField (nameLabel,        "Name",        nameEditor);
    initField (emailLabel,       "Email",       emailEditor);
    initField (orderLabel,       "Order",       orderEditor);
    initField (expiresLabel,     "Expires",     expiresEditor);
    initField (fingerprintLabel, "Fingerprint", fingerprintEditor);
    expiresEditor.setText ("lifetime", juce::dontSendNotification);

    keygenButton.onClick = [this] { generateKeypairPressed(); };
    issueButton.onClick  = [this] { issueLicensePressed(); };
    addAndMakeVisible (keygenButton);
    addAndMakeVisible (issueButton);

    outputBox.setMultiLine (true);
    outputBox.setReadOnly (true);
    outputBox.setFont (juce::Font (juce::FontOptions{}
                                       .withName (juce::Font::getDefaultMonospacedFontName())
                                       .withHeight (13.0f)));
    addAndMakeVisible (outputBox);

    setSize (560, 620);
}

void LicenseToolComponent::log (const juce::String& line)
{
    outputBox.moveCaretToEnd();
    outputBox.insertTextAtCaret (line + "\n");
}

void LicenseToolComponent::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff1e1e1e));
}

void LicenseToolComponent::resized()
{
    auto r = getLocalBounds().reduced (16);
    titleLabel.setBounds (r.removeFromTop (32));
    r.removeFromTop (8);

    auto row = [&r] (juce::Label& l, juce::Component& c, int extra = 0)
    {
        auto line = r.removeFromTop (26);
        l.setBounds (line.removeFromLeft (110));
        c.setBounds (line.reduced (0).withTrimmedRight (extra));
        r.removeFromTop (6);
    };

    row (productLabel, productBox);

    {
        auto line = r.removeFromTop (26);
        keyFileLabel.setBounds (line.removeFromLeft (110));
        browseKeyButton.setBounds (line.removeFromRight (90));
        keyFileEditor.setBounds (line.withTrimmedRight (6));
        r.removeFromTop (6);
    }

    row (nameLabel,        nameEditor);
    row (emailLabel,       emailEditor);
    row (orderLabel,       orderEditor);
    row (expiresLabel,     expiresEditor);
    row (fingerprintLabel, fingerprintEditor);

    r.removeFromTop (8);
    {
        auto line = r.removeFromTop (32);
        keygenButton.setBounds (line.removeFromLeft (180));
        line.removeFromLeft (12);
        issueButton.setBounds (line.removeFromLeft (160));
    }
    r.removeFromTop (10);
    outputBox.setBounds (r);
}

void LicenseToolComponent::generateKeypairPressed()
{
    const auto pi = kProducts[(size_t) (productBox.getSelectedId() - 1)];

    uint8_t pub[32], secret[64];
    mulic::generateKeypair (pub, secret);

    chooser = std::make_unique<juce::FileChooser> ("Save the secret key (keep private, git-ignored)",
                                                   juce::File(),
                                                   "*.key");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
        [this, pi, pub, secret] (const juce::FileChooser& fc)
        {
            auto out = fc.getResult();
            if (out == juce::File())
                return;

            out.replaceWithData (secret, 64);

            // Format the public key as a C++ array to paste into LicenseKey.h.
            juce::String arr;
            for (int i = 0; i < 32; ++i)
            {
                arr << "0x" << juce::String::toHexString ((int) pub[i]).paddedLeft ('0', 2);
                if (i != 31) arr << ", ";
                if (i == 15) arr << "\n        ";
            }

            log ("Keypair generated for " + juce::String (pi.id));
            log ("  secret key -> " + out.getFullPathName() + "  (KEEP PRIVATE)");
            log ("  paste this into LicenseKey.h (" + juce::String (pi.keyVar) + "):");
            log ("    inline constexpr uint8_t kLicensePublicKey[32] = {");
            log ("        " + arr);
            log ("    };");
            log ("");
        });
}

void LicenseToolComponent::issueLicensePressed()
{
    const auto pi = kProducts[(size_t) (productBox.getSelectedId() - 1)];

    const juce::String name        = nameEditor.getText().trim();
    const juce::String email       = emailEditor.getText().trim();
    const juce::String order       = orderEditor.getText().trim();
    const juce::String expires     = expiresEditor.getText().trim();
    const juce::String fingerprint = fingerprintEditor.getText().trim();

    if (name.isEmpty() || email.isEmpty() || order.isEmpty() || fingerprint.isEmpty())
    {
        log ("ERROR: Name, Email, Order and Fingerprint are all required.");
        return;
    }

    juce::File keyFile (keyFileEditor.getText().trim());
    if (! keyFile.existsAsFile() || keyFile.getSize() != 64)
    {
        log ("ERROR: secret key file missing or not 64 bytes: " + keyFile.getFullPathName());
        return;
    }

    juce::MemoryBlock keyData;
    keyFile.loadFileAsData (keyData);
    const auto* secret = static_cast<const uint8_t*> (keyData.getData());

    const juce::String issued = juce::Time::getCurrentTime().formatted ("%Y-%m-%d");

    // Build the EXACT canonical string the verifier rebuilds, then Ed25519-sign it.
    const auto canonical = mu_core::buildLicenseCanonical (email, expires, fingerprint, issued,
                                                           name, order, pi.id);

    uint8_t sig[64];
    mulic::sign (secret, canonical.toRawUTF8(), (size_t) canonical.getNumBytesAsUTF8(), sig);
    const juce::String sigBase64 = juce::Base64::toBase64 (sig, 64);

    // Emit the .lic JSON (alphabetical fields + signature) the plugin reads.
    auto* obj = new juce::DynamicObject();
    obj->setProperty ("email",       email);
    obj->setProperty ("expires",     expires);
    obj->setProperty ("fingerprint", fingerprint);
    obj->setProperty ("issued",      issued);
    obj->setProperty ("name",        name);
    obj->setProperty ("order",       order);
    obj->setProperty ("product",     juce::String (pi.id));
    obj->setProperty ("signature",   sigBase64);
    const juce::String json = juce::JSON::toString (juce::var (obj));

    const juce::String suggested = juce::String (pi.id).toLowerCase().replace ("-", "") + ".lic";
    chooser = std::make_unique<juce::FileChooser> ("Save the license file",
                                                   juce::File::getSpecialLocation (juce::File::userDesktopDirectory)
                                                       .getChildFile (suggested),
                                                   "*.lic");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
        [this, json, pi, fingerprint] (const juce::FileChooser& fc)
        {
            auto out = fc.getResult();
            if (out == juce::File())
                return;
            out.replaceWithText (json);
            log ("Issued " + juce::String (pi.id) + " license -> " + out.getFullPathName());
            log ("  machine-locked to fingerprint " + fingerprint);
            log ("");
        });
}
