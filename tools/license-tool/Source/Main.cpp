#include <juce_gui_extra/juce_gui_extra.h>
#include "LicenseToolComponent.h"
#include "Crypto.h"

// MuLicenseTool — owner-side JUCE GUI to generate product keypairs and issue machine-locked,
// Ed25519-signed `.lic` files. Replaces the OpenSSL/PowerShell keygen+gen_license scripts;
// signs with the SAME Monocypher the plugin verifies with (mu_core::LicenseManager). Never
// shipped to testers/users — a dev tool only.
//
// Headless convenience: `MuLicenseTool --keygen <Product> <outKeyFile>` generates a keypair,
// writes the 64-byte secret to <outKeyFile>, writes the C++ public-key array to
// <outKeyFile>.pub.txt, and exits — so initial keys can be made without the GUI.

namespace
{
    // Emit the public key as the uint8_t[32] body to paste into a product's LicenseKey.h.
    juce::String formatPublicArray (const uint8_t pub[32])
    {
        juce::String arr = "    inline constexpr uint8_t kLicensePublicKey[32] = {\n        ";
        for (int i = 0; i < 32; ++i)
        {
            arr << "0x" << juce::String::toHexString ((int) pub[i]).paddedLeft ('0', 2);
            if (i != 31) arr << ", ";
            if (i == 15) arr << "\n        ";
        }
        arr << "\n    };\n";
        return arr;
    }

    // Returns an exit code; -1 means "not a headless run, show the GUI".
    int runHeadlessKeygen (const juce::String& commandLine)
    {
        juce::StringArray args = juce::StringArray::fromTokens (commandLine, true);
        const int idx = args.indexOf ("--keygen");
        if (idx < 0)
            return -1;

        if (idx + 2 >= args.size())
        {
            std::cout << "usage: MuLicenseTool --keygen <Product> <outKeyFile>" << std::endl;
            return 1;
        }

        const juce::String product = args[idx + 1].unquoted();
        juce::File outKey (args[idx + 2].unquoted());

        uint8_t pub[32], secret[64];
        mulic::generateKeypair (pub, secret);

        outKey.getParentDirectory().createDirectory();
        outKey.replaceWithData (secret, 64);

        const juce::String pubArray = formatPublicArray (pub);
        outKey.withFileExtension ("key.pub.txt").replaceWithText (pubArray);

        std::cout << "Keypair generated for " << product << std::endl;
        std::cout << "  secret key -> " << outKey.getFullPathName() << std::endl;
        std::cout << pubArray << std::endl;
        return 0;
    }
}

class LicenseToolApplication : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override       { return "MuLicenseTool"; }
    const juce::String getApplicationVersion() override    { return "1.0"; }
    bool moreThanOneInstanceAllowed() override             { return true; }

    void initialise (const juce::String& commandLine) override
    {
        const int headless = runHeadlessKeygen (commandLine);
        if (headless >= 0)
        {
            setApplicationReturnValue (headless);
            quit();
            return;
        }
        mainWindow = std::make_unique<MainWindow> (getApplicationName());
    }

    void shutdown() override { mainWindow = nullptr; }

private:
    class MainWindow : public juce::DocumentWindow
    {
    public:
        explicit MainWindow (juce::String name)
            : DocumentWindow (name,
                              juce::Colour (0xff1e1e1e),
                              DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar (true);
            setContentOwned (new LicenseToolComponent(), true);
            setResizable (true, false);
            centreWithSize (getWidth(), getHeight());
            setVisible (true);
        }

        void closeButtonPressed() override
        {
            juce::JUCEApplication::getInstance()->systemRequestedQuit();
        }

    private:
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainWindow)
    };

    std::unique_ptr<MainWindow> mainWindow;
};

START_JUCE_APPLICATION (LicenseToolApplication)
