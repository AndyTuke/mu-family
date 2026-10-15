#pragma once

// Shared by the mu-Clid golden-render scenarios and the extraction pins: a real headless PluginProcessor
// that owns its own temporary storage root, runs the deferred-work pass after every block (as the host's
// 100 Hz poll would, only deterministic), and writes the small fixture samples the scenarios play.
// The fixtures are generated here, never read from a content folder, so a run depends on nothing outside
// the repo.

#include "Plugin/PluginProcessor.h"
#include "Tests/ProcessorSeamHelpers.h"

namespace mu_golden_rig
{
    // Set up before the processor base is built (base classes construct in order): deterministic noise
    // for the processor's lifetime, and a private storage root so settings never leak between runs.
    struct StorageHolder
    {
        mu_seam_test::ScopedEnvVar deterministicNoise { "MU_DETERMINISTIC_NOISE", "1" };
        mu_seam_test::TempStorage  storage;
    };

    // The real processor. A host runs the swap commits and the retired-engine cleanup from a message-thread
    // timer; a headless render never returns to a message loop, so the drain runs once after each block,
    // the rate the scripted-edit tests already use. The block still renders before its drain, so a commit
    // the audio thread asked for lands between two blocks, never inside one.
    struct OwnedProcessor : StorageHolder, mu_clid::PluginProcessor
    {
        void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override
        {
            mu_clid::PluginProcessor::processBlock(buffer, midi);
            drainDeferredWork();
        }
        using mu_clid::PluginProcessor::drainDeferredWork;
    };

    inline void setParam(juce::AudioProcessorValueTreeState& apvts, const juce::String& id, float plain)
    {
        mu_seam_test::setParam(apvts, id, plain);
    }

    // tests/presets, from this source tree (<root>/mu-clid/Source/Tests/GoldenTestRig.h).
    inline juce::File presetsDir()
    {
        return juce::File(__FILE__).getParentDirectory().getParentDirectory().getParentDirectory()
                                   .getParentDirectory().getChildFile("tests").getChildFile("presets");
    }

    // A mono 16-bit WAV of `seconds` at 44.1 kHz: a sine of `hz` with an exponential decay of `decayPerSec`.
    inline void writeTone(const juce::File& f, double hz, double seconds, double decayPerSec, float level = 0.6f)
    {
        f.deleteFile();
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> out = f.createOutputStream();
        if (out == nullptr) return;
        auto w = wav.createWriterFor(out, juce::AudioFormatWriterOptions{}.withSampleRate(44100.0)
                                                                          .withNumChannels(1)
                                                                          .withBitsPerSample(16));
        if (w == nullptr) return;
        juce::AudioBuffer<float> b(1, (int) std::lround(seconds * 44100.0));
        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            const double t = (double) i / 44100.0;
            b.setSample(0, i, level * (float) (std::exp(-decayPerSec * t) * std::sin(juce::MathConstants<double>::twoPi * hz * t)));
        }
        w->writeFromAudioSampleBuffer(b, 0, b.getNumSamples());
    }

    // The samples every scenario draws from, written into the processor's own Samples folder (under its
    // private storage root). "Pad Long" is 12 s long, for the retired-tail cases that play a sample to its end.
    inline void writeFixtureSamples(mu_clid::PluginProcessor& p)
    {
        const auto dir = p.getSamplesDir();
        dir.createDirectory();
        writeTone(dir.getChildFile("Kick 01.wav"),      70.0,   0.5,  6.0);
        writeTone(dir.getChildFile("Hat 01.wav"),       5200.0, 0.5,  6.0);
        writeTone(dir.getChildFile("Synth C 01.wav"),   261.6,  0.5,  6.0);
        writeTone(dir.getChildFile("Snare 01.wav"),     1800.0, 0.3, 10.0);
        writeTone(dir.getChildFile("Pad Long.wav"),     220.0, 12.0,  0.12, 0.5f);
    }

    // Grows the processor to at least `n` rhythms.
    inline void useRhythms(mu_clid::PluginProcessor& p, int n)
    {
        while (p.getNumRhythms() < n) p.addLayer();
    }

    // Plays `file` (a name in the Samples folder) on rhythm `r`.
    inline void useSample(mu_clid::PluginProcessor& p, int r, const char* file)
    {
        p.samples.load(r, p.getSamplesDir().getChildFile(file));
    }

    inline juce::String rp(int r, const char* suffix) { return "r" + juce::String(r) + "_" + suffix; }
}
