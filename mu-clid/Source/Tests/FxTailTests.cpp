// A full-preset commit mid-bar keeps the global FX tail, on the REAL mu-Clid PluginProcessor, built headless.
// (createEditor is defined in ProcessorSeamTests.cpp.)

#include "Plugin/PluginProcessor.h"
#include "Tests/FxTailChecks.h"

namespace
{
    // tests/presets, found by walking up from the test executable to the repo root.
    juce::File presetsDir()
    {
        auto dir = juce::File::getSpecialLocation(juce::File::currentExecutableFile).getParentDirectory();
        for (int i = 0; i < 10 && dir.exists(); ++i, dir = dir.getParentDirectory())
            if (dir.getChildFile("tests").getChildFile("presets").isDirectory())
                return dir.getChildFile("tests").getChildFile("presets");
        return {};
    }

    // A short decaying tone as `name` in the processor's Samples folder, so the preset has something to trigger.
    void writeSample(mu_clid::PluginProcessor& p, const juce::String& name, double freq)
    {
        auto f = p.getSamplesDir().getChildFile(name);
        f.deleteFile();
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> out = f.createOutputStream();
        if (out == nullptr) return;
        auto w = wav.createWriterFor(out, juce::AudioFormatWriterOptions{}.withSampleRate(44100.0)
                                                                          .withNumChannels(1)
                                                                          .withBitsPerSample(16));
        if (w == nullptr) return;
        juce::AudioBuffer<float> b(1, 22050);
        for (int i = 0; i < b.getNumSamples(); ++i)
            b.setSample(0, i, 0.6f * std::exp(-6.0f * (float) i / 22050.0f)
                                    * (float) std::sin(juce::MathConstants<double>::twoPi * freq * i / 44100.0));
        w->writeFromAudioSampleBuffer(b, 0, b.getNumSamples());
    }
}

struct ClidFxTailTests : juce::UnitTest
{
    ClidFxTailTests() : juce::UnitTest("mu-Clid full-preset commit keeps the FX tail", "mu-clid-processor") {}

    void runTest() override
    {
        const auto presets = presetsDir();
        expect(presets.isDirectory(), "tests/presets found");
        if (! presets.isDirectory()) return;

        mu_fxtail_test::runFxTailChecks<mu_clid::PluginProcessor>(*this, [presets](mu_clid::PluginProcessor& p)
        {
            p.getSamplesDir().createDirectory();
            writeSample(p, "Hat 01.wav", 5200.0);
            writeSample(p, "Kick 01.wav", 70.0);
            writeSample(p, "Synth C 01.wav", 261.6);
            p.loadPreset(presets.getChildFile("A3_filter.muClid"));
        });
    }
};

static ClidFxTailTests clidFxTailTests;
