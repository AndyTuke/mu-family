#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include "SamplePreview.h"

namespace mu_clid {

class PluginProcessor;

// mu-Clid's per-rhythm sample bookkeeping: the sample path each rhythm slot plays, the
// load / name / missing queries the UI reads, the file-browser preview player and the
// user's primary sample-library folder. Owned by PluginProcessor as `samples`.
//
// Threading: everything here is message-thread except mixPreviewInto (audio thread).
// Path slots are indexed by rhythm and stay sized to SequencerEngine::MaxRhythms.
class SampleLibrary
{
public:
    explicit SampleLibrary(PluginProcessor& p);

    // Loads a file into the rhythm's voice engine and records its path.
    void load(int rhythmIndex, const juce::File& file);

    // Recorded sample path per rhythm slot (empty = no sample).
    juce::String             path    (int rhythmIndex) const { return samplePaths[rhythmIndex]; }
    void                     setPath (int rhythmIndex, const juce::String& p) { samplePaths.set(rhythmIndex, p); }
    void                     clearPath(int rhythmIndex) { samplePaths.set(rhythmIndex, juce::String()); }
    void                     swapPaths(int i, int j);
    const juce::StringArray& paths() const noexcept { return samplePaths; }

    // File name of the rhythm's sample, or empty when none is assigned.
    juce::String getSampleName(int rhythmIndex) const;

    // True when the rhythm has a sample path recorded but the voice engine couldn't
    // load it (e.g. a linked sample later moved or deleted). The RhythmPanel sample bar
    // shows a "missing — click to find" affordance instead of silently leaving it empty.
    bool isSampleMissing(int rhythmIndex) const;

    // Sample preview — plays a file through the master output without assigning it.
    void prepare(int blockSize, double sampleRate) { preview.prepare(blockSize, sampleRate); }
    void releaseResources()                        { preview.releaseResources(); }
    void startPreview(const juce::File& file)      { preview.start(file); }
    void stopPreview()                             { preview.stop(); }
    void mixPreviewInto(juce::AudioBuffer<float>& masterBus, int numSamples) { preview.mixInto(masterBus, numSamples); }

    // The user's personal sample library, distinct from the content folder (which hosts
    // factory + preset-linked material). Unset = the OS Music folder; an empty File clears it.
    juce::File getPrimarySampleDir() const;
    void       setPrimarySampleDir(const juce::File& dir);

private:
    PluginProcessor&  proc;
    juce::StringArray samplePaths;
    SamplePreview     preview;
};

} // namespace mu_clid
