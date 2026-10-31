#include <juce_audio_processors/juce_audio_processors.h>
#include "Audio/MixerEngine.h"
#include "Audio/FX/Slots/FXChain.h"

// The mixer publishes a left and a right peak beside the combined one for every strip, FX return and the
// master, so the meters can show two true bars. These tests drive the real MixerEngine::processBlock
// (through the render hook, so no voice engine is needed) and check each path reports the two sides
// separately.
struct MixerStereoPeakTests : juce::UnitTest
{
    MixerStereoPeakTests() : juce::UnitTest("Mixer stereo peaks (strip, return, master)", "mu-core") {}

    static constexpr int    kBlock = 256;
    static constexpr double kRate  = 48000.0;

    // Renders a steady centred tone into every strip so the pan alone decides how the sides differ.
    static MixerEngine::RenderChannelFn tone()
    {
        return [](int, juce::AudioBuffer<float>& buf, int n)
        {
            for (int c = 0; c < buf.getNumChannels(); ++c)
                for (int i = 0; i < n; ++i)
                    buf.setSample(c, i, 0.5f * std::sin(0.05f * (float) i));
        };
    }

    void runTest() override
    {
        beginTest("master peaks follow the master pan: hard left leaves the right side silent");
        {
            MixerEngine mixer;
            FXChain fx;
            mixer.prepare(kRate, kBlock);
            fx.prepare(kRate, kBlock);
            juce::AudioBuffer<float> out(2, kBlock);
            const auto render = tone();

            mixer.masterPan.store(-1.0f);
            mixer.processBlock(out, 1, nullptr, fx, kBlock, nullptr, nullptr, nullptr, &render);
            expect(mixer.masterPeakL.load() > 0.05f, "left side sounds (" + juce::String(mixer.masterPeakL.load(), 3) + ")");
            expect(mixer.masterPeakR.load() < 1.0e-3f, "right side is silent (" + juce::String(mixer.masterPeakR.load(), 5) + ")");
            expectWithinAbsoluteError(mixer.masterPeak.load(), juce::jmax(mixer.masterPeakL.load(), mixer.masterPeakR.load()), 1.0e-6f);

            mixer.masterPan.store(0.0f);
            mixer.processBlock(out, 1, nullptr, fx, kBlock, nullptr, nullptr, nullptr, &render);   // the pan ramps across this block
            mixer.processBlock(out, 1, nullptr, fx, kBlock, nullptr, nullptr, nullptr, &render);
            expect(mixer.masterPeakL.load() > 0.05f && std::abs(mixer.masterPeakL.load() - mixer.masterPeakR.load()) < 1.0e-3f,
                   "centred: both sides equal");
        }

        beginTest("a mono output buffer reports its one channel as both sides");
        {
            MixerEngine mixer;
            FXChain fx;
            mixer.prepare(kRate, kBlock);
            fx.prepare(kRate, kBlock);
            juce::AudioBuffer<float> out(1, kBlock);
            const auto render = tone();
            mixer.processBlock(out, 1, nullptr, fx, kBlock, nullptr, nullptr, nullptr, &render);
            expect(mixer.masterPeakL.load() > 0.05f, "the mono channel sounds");
            expectWithinAbsoluteError(mixer.masterPeakL.load(), mixer.masterPeakR.load(), 1.0e-6f);
        }

        beginTest("FX return peaks follow the return pan, and a silent return reads zero on both sides");
        {
            MixerEngine mixer;
            FXChain fx;
            mixer.prepare(kRate, kBlock);
            fx.prepare(kRate, kBlock);
            juce::AudioBuffer<float> out(2, kBlock);
            const auto render = tone();

            mixer.channels[0].sendReverb.store(1.0f);
            mixer.returns[2].pan.store(-1.0f);

            // Run enough blocks for the reverb's wet output to build up, tracking the largest left peak.
            float maxL = 0.0f, maxR = 0.0f;
            for (int b = 0; b < 40; ++b)
            {
                mixer.processBlock(out, 1, nullptr, fx, kBlock, nullptr, nullptr, nullptr, &render);
                maxL = juce::jmax(maxL, mixer.returnPeaksL[2].load());
                maxR = juce::jmax(maxR, mixer.returnPeaksR[2].load());
            }
            expect(maxL > 1.0e-4f, "the reverb return sounds on the left (" + juce::String(maxL, 6) + ")");
            expect(maxR < 1.0e-6f, "and not on the right when panned hard left (" + juce::String(maxR, 8) + ")");

            // The effect return has no send, so it stays at zero on every side.
            expect(mixer.returnPeaks[0].load() == 0.0f && mixer.returnPeaksL[0].load() == 0.0f && mixer.returnPeaksR[0].load() == 0.0f,
                   "an unused return reads zero");
        }
    }
};

static MixerStereoPeakTests mixerStereoPeakTests;
