#include "SampleLibrary.h"
#include "PluginProcessor.h"

SampleLibrary::SampleLibrary(PluginProcessor& p) : proc_(p)
{
    // One path slot per possible rhythm, so index writes never grow the array.
    for (int i = 0; i < SequencerEngine::MaxRhythms; ++i)
        paths_.add(juce::String());
}

void SampleLibrary::load(int rhythmIndex, const juce::File& file)
{
    if (rhythmIndex < 0 || rhythmIndex >= proc_.numActiveRhythms.load(std::memory_order_acquire)) return;
    proc_.voiceEngines[(size_t) rhythmIndex]->loadFile(file);
    paths_.set(rhythmIndex, file.getFullPathName());
}

void SampleLibrary::swapPaths(int i, int j)
{
    const juce::String tmp = paths_[i];
    paths_.set(i, paths_[j]);
    paths_.set(j, tmp);
}

juce::String SampleLibrary::getSampleName(int rhythmIndex) const
{
    if (rhythmIndex < 0 || rhythmIndex >= paths_.size()) return {};
    const auto& p = paths_[rhythmIndex];
    return p.isEmpty() ? juce::String() : juce::File(p).getFileName();
}

bool SampleLibrary::isSampleMissing(int rhythmIndex) const
{
    if (rhythmIndex < 0 || rhythmIndex >= paths_.size()) return false;
    if (paths_[rhythmIndex].isEmpty()) return false;
    const auto& engine = proc_.voiceEngines[(size_t) rhythmIndex];
    return engine == nullptr || ! engine->hasSample();
}

juce::File SampleLibrary::getPrimarySampleDir() const
{
    if (proc_.appSettings != nullptr)
    {
        const juce::String stored = proc_.appSettings->getValue("primarySampleDir");
        if (stored.isNotEmpty())
            return juce::File(stored);
    }
    return juce::File::getSpecialLocation(juce::File::userMusicDirectory);
}

void SampleLibrary::setPrimarySampleDir(const juce::File& dir)
{
    if (proc_.appSettings == nullptr) return;

    // An empty File clears the override so the SettingsOverlay "Default" button reuses this setter.
    proc_.appSettings->setValue("primarySampleDir",
                                dir == juce::File{} ? juce::String{} : dir.getFullPathName());
    proc_.appSettings->saveIfNeeded();
}
