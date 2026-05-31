// Custom standalone app for mu-Toni: shows a confirmation dialog before closing,
// mirroring mu-clid / mu-tant. Activated by JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP=1
// on the Standalone target. The Close prompt is the shared mu-core dialog
// (mu_ui::confirmQuitAsync) so it looks + behaves identically across products.

#include "PluginProcessor.h"
#include "UI/ConfirmDialog.h"
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>

//==============================================================================
class MuToniWindow : public juce::StandaloneFilterWindow
{
public:
    MuToniWindow (const juce::String& title,
                  juce::Colour background,
                  std::unique_ptr<juce::StandalonePluginHolder> holder)
        : juce::StandaloneFilterWindow (title, background, std::move (holder))
    {}

    void closeButtonPressed() override
    {
        if (dialogOpen) return;
        dialogOpen = true;

        juce::Component::SafePointer<MuToniWindow> safeThis (this);
        const auto name = juce::String (juce::CharPointer_UTF8 ("\xce\xbc-Toni"));

        mu_ui::confirmQuitAsync (name,
            [safeThis]   // OK — close without explicit save
            {
                if (safeThis != nullptr) safeThis->dialogOpen = false;
                juce::JUCEApplication::getInstance()->quit();
            },
            [safeThis]   // Save — defer to the processor's save+quit, else save state
            {
                if (safeThis == nullptr) return;
                safeThis->dialogOpen = false;
                auto* proc = safeThis->pluginHolder
                    ? dynamic_cast<mu_toni::PluginProcessor*>(safeThis->pluginHolder->processor.get())
                    : nullptr;
                if (proc != nullptr && proc->onSaveAndQuit)
                    proc->onSaveAndQuit ([] { juce::JUCEApplication::getInstance()->quit(); });
                else
                {
                    if (safeThis->pluginHolder != nullptr) safeThis->pluginHolder->savePluginState();
                    juce::JUCEApplication::getInstance()->quit();
                }
            },
            [safeThis]   // Cancel — let the window be closeable again
            {
                if (safeThis != nullptr) safeThis->dialogOpen = false;
            });
    }

private:
    bool dialogOpen = false;
};

//==============================================================================
class MuToniApp : public juce::JUCEApplication
{
public:
    MuToniApp()
    {
        juce::PropertiesFile::Options options;
        options.applicationName     = juce::CharPointer_UTF8 (JucePlugin_Name);
        options.filenameSuffix      = ".settings";
        options.osxLibrarySubFolder = "Application Support";
       #if JUCE_LINUX || JUCE_BSD
        options.folderName          = "~/.config";
       #else
        options.folderName          = "";
       #endif
        appProperties.setStorageParameters (options);
    }

    const juce::String getApplicationName()    override
    {
        return juce::String (juce::CharPointer_UTF8 ("\xce\xbc-Toni"));
    }
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

        mainWindow = std::make_unique<MuToniWindow> (
            getApplicationName(),
            juce::LookAndFeel::getDefaultLookAndFeel().findColour (
                juce::ResizableWindow::backgroundColourId),
            std::move (holder));

        mainWindow->setVisible (true);
    }

    void shutdown() override
    {
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
    juce::ApplicationProperties     appProperties;
    std::unique_ptr<MuToniWindow>   mainWindow;
};

//==============================================================================
juce::JUCEApplicationBase* juce_CreateApplication()
{
    return new MuToniApp();
}
