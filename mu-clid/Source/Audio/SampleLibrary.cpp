#include "SampleLibrary.h"
#include "Plugin/PluginProcessor.h"

namespace mu_clid {

SampleLibrary::SampleLibrary(PluginProcessor& p) : proc(p)
{
    // One path slot per possible rhythm, so index writes never grow the array.
    for (int i = 0; i < SequencerEngine::MaxRhythms; ++i)
        samplePaths.add(juce::String());
}

void SampleLibrary::load(int rhythmIndex, const juce::File& file)
{
    if (rhythmIndex < 0 || rhythmIndex >= proc.numActiveRhythms.load(std::memory_order_acquire)) return;
    proc.voiceEngines[(size_t) rhythmIndex]->loadFile(file);
    samplePaths.set(rhythmIndex, file.getFullPathName());
}

void SampleLibrary::swapPaths(int i, int j)
{
    const juce::String tmp = samplePaths[i];
    samplePaths.set(i, samplePaths[j]);
    samplePaths.set(j, tmp);
}

juce::String SampleLibrary::getSampleName(int rhythmIndex) const
{
    if (rhythmIndex < 0 || rhythmIndex >= samplePaths.size()) return {};
    const auto& p = samplePaths[rhythmIndex];
    return p.isEmpty() ? juce::String() : juce::File(p).getFileName();
}

bool SampleLibrary::isSampleMissing(int rhythmIndex) const
{
    if (rhythmIndex < 0 || rhythmIndex >= samplePaths.size()) return false;
    if (samplePaths[rhythmIndex].isEmpty()) return false;
    const auto& engine = proc.voiceEngines[(size_t) rhythmIndex];
    return engine == nullptr || ! engine->hasSample();
}

juce::File SampleLibrary::getPrimarySampleDir() const
{
    if (proc.appSettings != nullptr)
    {
        const juce::String stored = proc.appSettings->getValue("primarySampleDir");
        if (stored.isNotEmpty())
            return juce::File(stored);
    }
    return juce::File::getSpecialLocation(juce::File::userMusicDirectory);
}

void SampleLibrary::setPrimarySampleDir(const juce::File& dir)
{
    if (proc.appSettings == nullptr) return;

    // An empty File clears the override so the SettingsOverlay "Default" button reuses this setter.
    proc.appSettings->setValue("primarySampleDir",
                                dir == juce::File{} ? juce::String{} : dir.getFullPathName());
    proc.appSettings->saveIfNeeded();
}

} // namespace mu_clid
