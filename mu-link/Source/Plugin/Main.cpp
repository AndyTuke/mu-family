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

#include "Server/AudioServer.h"
#include "UI/MuLinkComponent.h"

class MuLinkApplication : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override    { return "mu-link"; }
    const juce::String getApplicationVersion() override { return "1.0"; }
    bool moreThanOneInstanceAllowed() override          { return false; }

    void initialise(const juce::String&) override
    {
        server = std::make_unique<mu_link::AudioServer>();
        server->start();                       // creates the bus + opens the default device
        mainWindow = std::make_unique<MainWindow>(getApplicationName(), *server);
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
                             juce::Colour(0xff1c1c1b),
                             DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar(true);
            setContentOwned(new MuLinkComponent(server), true);
            setResizable(true, true);
            setResizeLimits(760, 460, 1600, 1000);
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
