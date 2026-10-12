// mu-link — application entry (Stage L3).
//
// A standalone GUI app (not a plugin): it owns the AudioServer (hardware device + the
// shared-memory bus + master clock) and hosts MuLinkComponent. Launch it, then any mu
// standalone auto-attaches and is summed to the one output. Local-only — no plugin
// formats, no tester deploy.
//
//   Build:  cmake --build build --config Debug --target mu-link
//   Run:    build/mu-link/mu-link_artefacts/Debug/mu-link.exe

#include <juce_gui_extra/juce_gui_extra.h>

#include "BuildNumber.h"
#include "Server/AudioServer.h"
#include "UI/Components/MuLookAndFeel.h"
#include "UI/MuLinkComponent.h"

class MuLinkApplication : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override    { return "mu-link"; }
    const juce::String getApplicationVersion() override { return MU_VERSION_STRING; }
    bool moreThanOneInstanceAllowed() override          { return false; }

    void initialise(const juce::String&) override
    {
        server = std::make_unique<mu_link::AudioServer>();
        server->start();                       // creates the bus + opens the default device
       #if JUCE_DEBUG
        mainWindow = std::make_unique<MainWindow>(getApplicationName() + " - Debug", *server);   // marks a Debug build
       #else
        mainWindow = std::make_unique<MainWindow>(getApplicationName(), *server);
       #endif
    }

    void shutdown() override
    {
        mainWindow = nullptr;                  // destroy the UI (holds server&) first
        if (server != nullptr)
            server->stop();
        server = nullptr;
    }

    void systemRequestedQuit() override { quit(); }

private:
    // Top-level window hosting the mu-link content.
    class MainWindow : public juce::DocumentWindow
    {
    public:
        MainWindow(const juce::String& name, mu_link::AudioServer& server)
            : DocumentWindow(name,
                             MuLookAndFeel::colour(MuLookAndFeel::windowBackground),
                             DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar(true);
            // Fixed at the family size; the window follows the content when the display size changes.
            setContentOwned(new MuLinkComponent(server), true);
            setResizable(false, false);
            centreWithSize(getWidth(), getHeight());
            setVisible(true);
        }

        void closeButtonPressed() override
        {
            JUCEApplication::getInstance()->systemRequestedQuit();
        }

    private:
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainWindow)
    };

    std::unique_ptr<mu_link::AudioServer> server;
    std::unique_ptr<MainWindow>           mainWindow;
};

START_JUCE_APPLICATION (MuLinkApplication)
