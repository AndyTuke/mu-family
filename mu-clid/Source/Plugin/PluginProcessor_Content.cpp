// PluginProcessor — content folders: the presets / rhythms / samples directories. Partial-class TU split from PluginProcessor.cpp.

#include "PluginProcessor.h"
#include "PluginProcessor_Internal.h"
#include "Audio/SpinLock.h"            // mu-core: spin lock helpers
#include "Audio/InsertSlotConfig.h"
#include "Plugin/ModulationSkew.h"     // proportion-space skew helpers (shared with test C5)
#include "Modulation/MuClidModDest.h"  // mu-clid modulation targets
#include "Sequencer/Rhythm.h"

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
