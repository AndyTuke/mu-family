// ProcessorBase::ScopedStorageRoot sends a processor's settings and content folders to a temp root,
// so a test can build a real product processor without touching the user's TDP folders.

#include "TestProcessorBase.h"

namespace
{
    // Exposes the protected initAppSettings / appSettings so a test can drive them directly.
    struct StorageProcessor : TestProcessorBase
    {
        StorageProcessor() : TestProcessorBase(WrapperType::wrapperType_Undefined) {}
        void init(const juce::String& name) { initAppSettings(name); }
        void saveSomething() { appSettings->setValue("probe", 1); appSettings->saveIfNeeded(); }
    };

    // Where initAppSettings would write without a root: the real per-user settings file.
    juce::File realSettingsFile(const juce::String& name)
    {
        juce::PropertiesFile::Options opts;
        opts.applicationName     = name;
        opts.filenameSuffix      = "xml";
        opts.folderName          = "TDP";
        opts.osxLibrarySubFolder = "Application Support";
        return opts.getDefaultFile();
    }
}

struct ScopedStorageRootTests : juce::UnitTest
{
    ScopedStorageRootTests() : juce::UnitTest("ScopedStorageRoot", "mu-core") {}

    void runTest() override
    {
        const juce::String name = "mu-test-storage-root";
        const auto real = realSettingsFile(name);
        const auto root = juce::File::getSpecialLocation(juce::File::tempDirectory)
                              .getChildFile("mu-storage-root-test-" + juce::String::toHexString(juce::Random().nextInt64()));

        beginTest("With a root set, the settings file and content dir sit under it and nothing is written to the real folder");
        {
            expect(! real.existsAsFile(), "precondition: no real settings file for the probe name");
            StorageProcessor p;
            {
                ProcessorBase::ScopedStorageRoot scope(root);
                p.init(name);
                p.saveSomething();
                expect(root.getChildFile("settings").getChildFile(name + ".xml").existsAsFile(),
                       "settings file is under <root>/settings");
                expectEquals(p.getContentDir().getFullPathName(),
                             root.getChildFile("content").getChildFile(name).getFullPathName());
                expect(! real.existsAsFile(), "the real settings file was not created");
            }

            beginTest("After the root is destroyed the default content dir is restored");
            expectEquals(p.getContentDir().getFullPathName(),
                         juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
                             .getChildFile("TDP").getChildFile(name).getFullPathName());
            expect(! real.existsAsFile(), "still no real settings file");
        }

        root.deleteRecursively();
    }
};

static ScopedStorageRootTests scopedStorageRootTests;
