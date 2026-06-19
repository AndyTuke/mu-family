#pragma once

// Shared standalone-app shell for the μ-family. Every product's standalone is the same:
// a StandaloneFilterWindow whose close button raises the shared "Close <product>?" prompt,
// plus a JUCEApplication that owns the window + the standalone-only mu-link bridge. Only the
// display name + mu-link client name differ, so this lifts the whole thing into one place.
//
// STANDALONE-ONLY: like mu-core/Link/MuLinkStandalone.h, this header is #included only by
// each product's StandaloneApp.cpp (the JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP target). It is
// NOT part of mu-core's INTERFACE sources, so VST3/CLAP never compile it. Plugin-agnostic —
// it talks only to ProcessorBase + the shared bridge + mu_ui::confirmQuitAsync, never a
// product symbol. JucePlugin_Name / JucePlugin_VersionString resolve in the product TU.

#include "Plugin/ProcessorBase.h"        // mu-core: onSaveAndQuit hook
#include "Link/MuLinkStandalone.h"        // mu-core: makeStandaloneBridge (header-only)
#include "UI/ConfirmDialog.h"             // mu-core: mu_ui::confirmQuitAsync (themed dialog)

#include <juce_audio_utils/juce_audio_utils.h>   // AudioDeviceSelectorComponent — StandaloneFilterWindow needs it in scope
#include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>

#include <memory>

namespace mu_standalone {

// The standalone window: intercepts the OS close and shows the shared themed quit prompt
// (Cancel / Save / Close) instead of closing immediately.
class CloseConfirmWindow : public juce::StandaloneFilterWindow
{
public:
    CloseConfirmWindow (const juce::String& title,
                        juce::Colour background,
                        std::unique_ptr<juce::StandalonePluginHolder> holder,
                        juce::String productDisplayName)
        : juce::StandaloneFilterWindow (title, background, std::move (holder)),
          displayName (std::move (productDisplayName))
    {}

    void closeButtonPressed() override
    {
        if (dialogOpen) return;
        dialogOpen = true;

        juce::Component::SafePointer<CloseConfirmWindow> self (this);
        mu_ui::confirmQuitAsync (this, displayName,
            [self]   // Close — quit without an explicit save
            {
                if (self != nullptr) self->dialogOpen = false;
                juce::JUCEApplication::getInstance()->quit();
            },
            [self]   // Save — defer to the processor's save+quit, else save state then quit
            {
                if (self == nullptr) return;
                self->dialogOpen = false;
                auto* proc = self->pluginHolder
                    ? dynamic_cast<ProcessorBase*> (self->pluginHolder->processor.get())
                    : nullptr;
                if (proc != nullptr && proc->onSaveAndQuit)
                    proc->onSaveAndQuit ([] { juce::JUCEApplication::getInstance()->quit(); });
                else
                {
                    if (self->pluginHolder != nullptr) self->pluginHolder->savePluginState();
                    juce::JUCEApplication::getInstance()->quit();
                }
            },
            [self]   // Cancel — let the window be closeable again
            {
                if (self != nullptr) self->dialogOpen = false;
            });
    }

private:
    juce::String displayName;
    bool dialogOpen = false;
};

// The standalone application. Products construct it with their display name + mu-link client
// name; everything else (settings file, window, bridge wiring, shutdown order) is shared.
class App : public juce::JUCEApplication
{
public:
    // muLinkName empty → no mu-link bridge (the product isn't wired to mu-link yet, e.g.
    // mu-on / mu-toni); non-empty → attach the standalone bridge under that client name.
    struct Config { juce::String displayName; juce::String muLinkName; };

    explicit App (Config c) : config (std::move (c))
    {
        juce::PropertiesFile::Options options;
        options.applicationName     = JucePlugin_Name;
        options.filenameSuffix      = ".settings";
        options.osxLibrarySubFolder = "Application Support";
       #if JUCE_LINUX || JUCE_BSD
        options.folderName          = "~/.config";
       #else
        options.folderName          = "";
       #endif
        appProperties.setStorageParameters (options);
    }

    const juce::String getApplicationName()    override { return config.displayName; }
    const juce::String getApplicationVersion() override { return JucePlugin_VersionString; }
    bool moreThanOneInstanceAllowed()          override { return true; }
    void anotherInstanceStarted (const juce::String&) override {}

    void initialise (const juce::String&) override
    {
        auto holder = std::make_unique<juce::StandalonePluginHolder> (
            appProperties.getUserSettings(),
            false, juce::String{}, nullptr,
            juce::Array<juce::StandalonePluginHolder::PluginInOuts>{},
            false);

        mainWindow = std::make_unique<CloseConfirmWindow> (
            getApplicationName(),
            juce::LookAndFeel::getDefaultLookAndFeel().findColour (
                juce::ResizableWindow::backgroundColourId),
            std::move (holder),
            config.displayName);

        mainWindow->setVisible (true);

        // Standalone-only mu-link bridge: route audio to mu-link's bus + slave the transport
        // when it's running; absent → the product runs normally on its own device. Skipped
        // when the product hasn't opted into mu-link (empty muLinkName).
        if (config.muLinkName.isNotEmpty())
            if (auto* holderPtr = mainWindow->pluginHolder.get())
                if (holderPtr->processor != nullptr)
                {
                    muLinkBridge = mu_link::makeStandaloneBridge (
                        *mainWindow, *holderPtr->processor, holderPtr->player, config.muLinkName);

                    // Let the product publish its current preset name to mu-link for display.
                    if (auto* pb = dynamic_cast<ProcessorBase*> (holderPtr->processor.get()))
                        pb->onPresetNameChanged = [b = muLinkBridge.get()] (const juce::String& n)
                        { if (b != nullptr) b->setPresetName (n); };
                }
    }

    void shutdown() override
    {
        // Drop the preset-name hook before the bridge so a late preset load can't call a
        // dangling bridge pointer during teardown.
        if (mainWindow != nullptr && mainWindow->pluginHolder != nullptr)
            if (auto* pb = dynamic_cast<ProcessorBase*> (mainWindow->pluginHolder->processor.get()))
                pb->onPresetNameChanged = nullptr;
        muLinkBridge = nullptr;   // references the holder's processor + player — tear down first
        if (mainWindow != nullptr && mainWindow->pluginHolder != nullptr)
            mainWindow->pluginHolder->saveAudioDeviceState();
        mainWindow = nullptr;
        appProperties.saveIfNeeded();
    }

    void systemRequestedQuit() override
    {
        if (mainWindow != nullptr)
            mainWindow->closeButtonPressed();
        else
            quit();
    }

private:
    Config config;
    juce::ApplicationProperties             appProperties;
    std::unique_ptr<CloseConfirmWindow>     mainWindow;
    std::unique_ptr<mu_link::MuLinkBridge>  muLinkBridge;
};

} // namespace mu_standalone
