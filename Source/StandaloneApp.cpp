// Custom standalone application class that shows a confirmation dialog before closing.
// Activated by JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP=1 in CMakeLists for the Standalone target.
// Must include PluginProcessor.h first so all JUCE module headers are in scope before the
// standalone window header, which does not include its own JUCE dependencies.

#include "PluginProcessor.h"
#include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>

//==============================================================================
class MuClidWindow : public juce::StandaloneFilterWindow
{
public:
    MuClidWindow (const juce::String& title,
                  juce::Colour background,
                  std::unique_ptr<juce::StandalonePluginHolder> holder)
        : juce::StandaloneFilterWindow (title, background, std::move (holder))
    {}

    void closeButtonPressed() override
    {
        // If a dialog is already open, cancel it and quit immediately.
        if (dialogOpen) return;
        dialogOpen = true;

        auto opts = juce::MessageBoxOptions()
            .withIconType (juce::MessageBoxIconType::QuestionIcon)
            .withTitle (juce::String(juce::CharPointer_UTF8("Close \xce\xbc-Clid?")))
            .withMessage ("Are you sure you want to close?")
            .withButton ("OK")
            .withButton ("Save")
            .withButton ("Cancel");

        juce::Component::SafePointer<MuClidWindow> safeThis (this);
        juce::AlertWindow::showAsync (opts, [safeThis] (int result)
        {
            if (safeThis == nullptr) return;
            safeThis->dialogOpen = false;

            if (result == 1) // OK: close without explicit save
            {
                juce::JUCEApplication::getInstance()->quit();
            }
            else if (result == 2) // Save: open Save dialog, quit on completion
            {
                auto* proc = safeThis->pluginHolder
                    ? dynamic_cast<PluginProcessor*>(safeThis->pluginHolder->processor.get())
                    : nullptr;
                if (proc && proc->onSaveAndQuit)
                {
                    proc->onSaveAndQuit([]
                    {
                        juce::JUCEApplication::getInstance()->quit();
                    });
                }
                else
                {
                    safeThis->pluginHolder->savePluginState();
                    juce::JUCEApplication::getInstance()->quit();
                }
            }
            // result == 0 (Cancel / Escape / dismissed): do nothing
        });
    }

private:
    bool dialogOpen = false;
};

//==============================================================================
class MuClidApp : public juce::JUCEApplication
{
public:
    MuClidApp()
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
        // #446: the JucePlugin_Name macro is fed into MSVC via CMake `-D` defines
        // and the μ (U+03BC, UTF-8 0xCE 0xBC) gets mangled to '?' somewhere in the
        // command-line round-trip (stdout/cmd codepage mismatch — same mojibake
        // observed in the Inno Setup console output, see #421). Hard-code the bytes
        // for the user-visible title bar so it always renders correctly. Matches
        // the pattern already used at line 27 for the "Close μ-Clid?" dialog.
       #if MUCLID_LITE_BUILD
        return juce::String (juce::CharPointer_UTF8 ("\xce\xbc-Clid Lite"));
       #else
        return juce::String (juce::CharPointer_UTF8 ("\xce\xbc-Clid"));
       #endif
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

        mainWindow = std::make_unique<MuClidWindow> (
            getApplicationName(),
            juce::LookAndFeel::getDefaultLookAndFeel().findColour (
                juce::ResizableWindow::backgroundColourId),
            std::move (holder));

        mainWindow->setVisible (true);
    }

    void shutdown() override
    {
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
    juce::ApplicationProperties          appProperties;
    std::unique_ptr<MuClidWindow>        mainWindow;
};

//==============================================================================
juce::JUCEApplicationBase* juce_CreateApplication()
{
    return new MuClidApp();
}
