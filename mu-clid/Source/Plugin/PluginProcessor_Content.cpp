// PluginProcessor — samples and content folders: per-rhythm sample load / preview and the
// presets / rhythms / samples directories. Partial-class TU split from PluginProcessor.cpp.

#include "PluginProcessor.h"
#include "PluginProcessor_Internal.h"
#include "Audio/SpinLock.h"            // mu-core: spin lock helpers
#include "Audio/InsertSlotConfig.h"
#include "Plugin/ModulationSkew.h"     // proportion-space skew helpers (shared with test C5)
#include "Modulation/MuClidModDest.h"  // mu-clid modulation targets
#include "Sequencer/Rhythm.h"

void PluginProcessor::loadSampleForRhythm(int rhythmIndex, const juce::File& file)
{
    if (rhythmIndex < 0 || rhythmIndex >= numActiveRhythms.load(std::memory_order_acquire)) return;
    voiceEngines[rhythmIndex]->loadFile(file);
    loadedSamplePaths.set(rhythmIndex, file.getFullPathName());
}

void PluginProcessor::startSamplePreview(const juce::File& file) { samplePreview.start(file); }
void PluginProcessor::stopSamplePreview()                        { samplePreview.stop(); }

//==============================================================================
// Hot-swap: stage a rhythm preset for atomic commit at the next loop boundary.
// or a MIDI program change was queued.

juce::File PluginProcessor::getPresetsDir() const { return getContentDir().getChildFile("Presets"); }
juce::File PluginProcessor::getRhythmsDir() const { return getContentDir().getChildFile("Rhythms"); }
juce::File PluginProcessor::getSamplesDir() const { return getContentDir().getChildFile("Samples"); }

void PluginProcessor::setContentDir(const juce::File& dir)
{
    if (appSettings != nullptr)
    {
        appSettings->setValue("contentDir", dir.getFullPathName());
        appSettings->saveIfNeeded();
    }
    ensureContentFoldersExist();
}

// primary sample library — user's personal sample folder, distinct
// from the My Documents content dir (which hosts factory / preset-linked
// material). Default-unset returns the OS Music directory so the sample-load
// dialog opens somewhere sensible even on first launch.
juce::File PluginProcessor::getPrimarySampleDir() const
{
    if (appSettings != nullptr)
    {
        const juce::String stored = appSettings->getValue("primarySampleDir");
        if (stored.isNotEmpty())
            return juce::File(stored);
    }
    return juce::File::getSpecialLocation(juce::File::userMusicDirectory);
}

void PluginProcessor::setPrimarySampleDir(const juce::File& dir)
{
    if (appSettings != nullptr)
    {
        // Empty string clears the override → next getPrimarySampleDir() returns
        // the default (user Music). Lets the SettingsOverlay "Default" button
        // reuse the same setter.
        appSettings->setValue("primarySampleDir",
                              dir == juce::File{} ? juce::String{} : dir.getFullPathName());
        appSettings->saveIfNeeded();
    }
}

void PluginProcessor::ensureContentFoldersExist()
{
    getPresetsDir().createDirectory();
    getRhythmsDir().createDirectory();
    getSamplesDir().createDirectory();
}
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new PluginProcessor();
}
