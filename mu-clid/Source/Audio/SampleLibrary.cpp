#include "SampleLibrary.h"
#include "Audio/VoiceEngine.h"
#include "MuLimits.h"

#include <type_traits>
#include <utility>

namespace mu_clid {

SampleLibrary::SampleLibrary(SampleSlotHost& h) : host(h)
{
    // One path slot per possible rhythm, so index writes never grow the array.
    for (int i = 0; i < mu_limits::kMaxLayers; ++i)
        samplePaths.add(juce::String());
}

void SampleLibrary::load(int rhythmIndex, const juce::File& file)
{
    if (rhythmIndex < 0 || rhythmIndex >= host.getSampleSlotCount()) return;
    auto* engine = host.getSampleSlotEngine(rhythmIndex);
    if (engine == nullptr) return;   // a slot with no voice has nothing to load into

    // Record the path only when the engine took the file: a failed decode keeps the old sample playing,
    // so the UI and presets must keep naming that one. Works whether loadFile reports success or not yet.
    if constexpr (std::is_void_v<decltype(engine->loadFile(file))>)
    {
        engine->loadFile(file);
        samplePaths.set(rhythmIndex, file.getFullPathName());
    }
    else if (engine->loadFile(file))
        samplePaths.set(rhythmIndex, file.getFullPathName());
}

// rt-ok-fn: sample-path bookkeeping, message thread
void SampleLibrary::swapPaths(int i, int j)
{
    // Called under layersLock: never grow the array there (the constructor sized it to every slot).
    jassert(i >= 0 && j >= 0 && i < samplePaths.size() && j < samplePaths.size());
    if (i < 0 || j < 0 || i >= samplePaths.size() || j >= samplePaths.size()) return;
    const juce::String tmp = samplePaths[i];
    samplePaths.set(i, samplePaths[j]);
    samplePaths.set(j, tmp);
}

void SampleLibrary::shiftPathsDown(int index, int count)
{
    for (int d = index; d < count - 1; ++d)
        swapPaths(d, d + 1);
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
    const auto* engine = std::as_const(host).getSampleSlotEngine(rhythmIndex);
    return engine == nullptr || ! engine->hasSample();
}

// rt-ok-fn: sample-path bookkeeping, message thread
juce::File SampleLibrary::getPrimarySampleDir() const
{
    if (auto* settings = host.getSampleSettingsFile())
    {
        const juce::String stored = settings->getValue("primarySampleDir");
        if (stored.isNotEmpty())
            return juce::File(stored);
    }
    return juce::File::getSpecialLocation(juce::File::userMusicDirectory);
}

void SampleLibrary::setPrimarySampleDir(const juce::File& dir)
{
    auto* settings = host.getSampleSettingsFile();
    if (settings == nullptr) return;

    // An empty File clears the override so the SettingsOverlay "Default" button reuses this setter.
    settings->setValue("primarySampleDir",
                       dir == juce::File{} ? juce::String{} : dir.getFullPathName());
    settings->saveIfNeeded();
}

} // namespace mu_clid
