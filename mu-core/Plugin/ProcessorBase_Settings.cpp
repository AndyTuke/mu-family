// ProcessorBase, part of the class split by topic (code moved unchanged from ProcessorBase.cpp).
// Test storage root, the shared settings file, app settings and the host-sync / UI scale setters.
#include "Plugin/ProcessorBase.h"
#include "Plugin/MixerFxParams.h"           // mu_mixfx::isGlobalFxParamId
#include "Sequencer/ClipClipboard.h"         // the process-wide clip clipboard
#include "Persistence/PresetFiles.h"         // mu_pp full-preset files
#include "Audio/FX/Slots/FXAlgorithmDef.h"   // FXAlgorithmRegistry
#include "Audio/AlgorithmNames.h"             // mu_audio::kInsertAlgorithmCount
#include <cstring>                            // std::strcmp — alloc-free suffix compare
#include <map>
#include <memory>
#include <mutex>

namespace
{
// The test storage root (empty when unset). A function-local so there is no static-init order issue.
juce::File& storageRootOverride()
{
    static juce::File root;
    return root;
}
}

ProcessorBase::ScopedStorageRoot::ScopedStorageRoot(const juce::File& root)
    : previous(storageRootOverride())
{
    storageRootOverride() = root;
}

ProcessorBase::ScopedStorageRoot::~ScopedStorageRoot()
{
    storageRootOverride() = previous;
}

namespace
{
// One PropertiesFile per settings file per module: every instance in the host process shares it, so a
// second instance's write can no longer revert a setting the first one saved (each used to hold its own
// copy, loaded at construction and rewritten whole). PropertiesFile serialises its own accessors.
std::shared_ptr<juce::PropertiesFile> sharedSettingsFile(const juce::File& file, const juce::PropertiesFile::Options& opts)
{
    static std::mutex registryLock;
    static std::map<juce::String, std::weak_ptr<juce::PropertiesFile>> registry;

    const std::lock_guard<std::mutex> guard(registryLock);
    auto& slot = registry[file.getFullPathName()];
    if (auto existing = slot.lock()) return existing;
    auto created = std::make_shared<juce::PropertiesFile>(file, opts);
    slot = created;
    return created;
}
}

void ProcessorBase::initAppSettings(const juce::String& name)
{
    appName = name;
    juce::PropertiesFile::Options opts;
    opts.applicationName     = name;
    opts.filenameSuffix      = "xml";
    opts.folderName          = "TDP";
    opts.osxLibrarySubFolder = "Application Support";
    const auto& testRoot = storageRootOverride();
    auto settingsFile = testRoot != juce::File()
                          ? testRoot.getChildFile("settings").getChildFile(name + ".xml")
                          : opts.getDefaultFile();
    settingsFile.getParentDirectory().createDirectory();
    appSettings = sharedSettingsFile(settingsFile, opts);

    // The MIDI control map is per user and per product, beside the settings file.
    midiControlMap.setStorageFile(settingsFile.getParentDirectory().getChildFile(name + "_midiControl.json"));
    midiControlMap.load();

    // Restore the shared preferences: a fresh open uses the last UI size + MIDI-clock choice.
    uiScale = juce::jlimit(kUiScaleMedium, kUiScaleLarge,
                           (float) appSettings->getDoubleValue("uiScale", (double) kUiScaleMedium));
    midiClockSync.setEnabled (appSettings->getBoolValue("midiSyncEnabled",  false));
    midiClockSync.setMessages(appSettings->getIntValue ("midiSyncMessages", 2));
    syncOffsetMs.store(juce::jlimit(kSyncOffsetMinMs, kSyncOffsetMaxMs, appSettings->getIntValue("syncOffsetMs", 0)));
    setTimeSignature(appSettings->getIntValue("timeSigNumerator", 4), appSettings->getIntValue("timeSigDenominator", 4));
    clipAutoSaveOn = appSettings->getBoolValue("clipAutoSave", true);
}

juce::File ProcessorBase::getContentDir() const
{
    if (appSettings != nullptr)
        if (const auto stored = appSettings->getValue("contentDir"); stored.isNotEmpty())
            return juce::File(stored);
    if (appName.isEmpty()) return {};
    if (const auto& testRoot = storageRootOverride(); testRoot != juce::File())
        return testRoot.getChildFile("content").getChildFile(appName);
    return juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
               .getChildFile("TDP").getChildFile(appName);
}

void ProcessorBase::setUiScale(float scale)
{
    const float clamped = juce::jlimit(kUiScaleMedium, kUiScaleLarge, scale);
    if (uiScale == clamped) return;
    if (appSettings != nullptr) { appSettings->setValue("uiScale", (double) clamped); appSettings->saveIfNeeded(); }
    uiScale = clamped;
    if (onUiScaleChanged) onUiScaleChanged(clamped);
}

void ProcessorBase::setMidiSyncEnabled(bool on)
{
    midiClockSync.setEnabled(on);
    if (appSettings != nullptr) { appSettings->setValue("midiSyncEnabled", on); appSettings->saveIfNeeded(); }
}

void ProcessorBase::setMidiSyncMessages(int mode)
{
    midiClockSync.setMessages(mode);
    if (appSettings != nullptr) { appSettings->setValue("midiSyncMessages", mode); appSettings->saveIfNeeded(); }
}

void ProcessorBase::setSyncOffsetMs(int ms)
{
    ms = juce::jlimit(kSyncOffsetMinMs, kSyncOffsetMaxMs, ms);
    syncOffsetMs.store(ms, std::memory_order_relaxed);
    if (appSettings != nullptr) { appSettings->setValue("syncOffsetMs", ms); appSettings->saveIfNeeded(); }
}

void ProcessorBase::setTimeSignature(int numerator, int denominator)
{
    // x/4 and x/8 only (the beat is a quarter or an eighth), 1..16 beats a bar.
    denominator = denominator == 8 ? 8 : 4;
    numerator   = juce::jlimit(1, 16, numerator);
    timeSignature.store((numerator << 8) | denominator, std::memory_order_relaxed);
    if (appSettings != nullptr)
    {
        appSettings->setValue("timeSigNumerator", numerator);
        appSettings->setValue("timeSigDenominator", denominator);
        appSettings->saveIfNeeded();
    }
}
