// GrooveVoices + RumbleEngine behaviour on the audio path: renderAll at every block size, a Rumble
// that processes its source's audio, and the Rumble's tail handling (cleared by reset, skipped once a
// silent feed has let it die away).

#include "Audio/GrooveVoices.h"
#include "Plugin/PluginProcessor.h"
#include "Tests/ProcessorSeamHelpers.h"

namespace
{
    using namespace mu_on;

    float rmsOf(const juce::AudioBuffer<float>& b, int n)
    {
        double sum = 0.0;
        for (int c = 0; c < b.getNumChannels(); ++c)
            for (int i = 0; i < n; ++i) { const double v = b.getReadPointer(c)[i]; sum += v * v; }
        return (float) std::sqrt(sum / juce::jmax(1, n * b.getNumChannels()));
    }

    bool allFinite(const juce::AudioBuffer<float>& b, int n)
    {
        for (int c = 0; c < b.getNumChannels(); ++c)
            for (int i = 0; i < n; ++i)
                if (! std::isfinite(b.getReadPointer(c)[i])) return false;
        return true;
    }

    // A decaying low burst standing in for a kick.
    void fillBurst(juce::AudioBuffer<float>& b, int n)
    {
        for (int c = 0; c < b.getNumChannels(); ++c)
            for (int i = 0; i < n; ++i)
                b.setSample(c, i, std::sin(0.02f * (float) i) * std::exp(-0.004f * (float) i));
    }
}

class GrooveVoicesTest : public juce::UnitTest
{
public:
    GrooveVoicesTest() : juce::UnitTest("mu-on GrooveVoices", "Engine") {}

    void runTest() override
    {
        using namespace mu_seam_test;
        constexpr double sr = 48000.0;
        constexpr int    maxBlock = 512;

        beginTest("renderAll gives finite audio at every block size, including one larger than prepared");
        {
            TempStorage storage;
            PluginProcessor p;
            for (int i = 1; i < 5; ++i) p.addLayer();                       // Kick, Bass, Hat, Snare, Rumble (on the Kick)
            auto voices = std::make_unique<GrooveVoices>();
            voices->prepare(sr, maxBlock);
            voices->cacheParams(p.apvts);

            std::array<Track*, mu_limits::kMaxLayers> tracks {};
            for (int i = 0; i < 5; ++i) tracks[(size_t) i] = &p.getTrack(i);

            juce::AudioBuffer<float> got(2, maxBlock);
            for (const int n : { 1, 64, 512, 1024 })
            {
                voices->beginBlock(tracks.data(), 5);
                voices->applyParams(0.0, 120.0);
                for (int s = 0; s < 4; ++s) voices->trigger(s, 1.0f);
                voices->renderAll(n);
                for (int s = 0; s < 5; ++s)
                {
                    voices->copyOut(s, got, juce::jmin(n, maxBlock));
                    expect(allFinite(got, juce::jmin(n, maxBlock)), "slot " + juce::String(s) + " finite at block " + juce::String(n));
                }
                voices->copyOut(0, got, juce::jmin(n, maxBlock));
                if (n >= 64) expect(rmsOf(got, juce::jmin(n, maxBlock)) > 1.0e-4f, "the kick sounds at block " + juce::String(n));
            }
        }

        beginTest("a Rumble processes its source's audio and goes quiet when the source is gone");
        {
            TempStorage storage;
            PluginProcessor p;
            for (int i = 1; i < 5; ++i) p.addLayer();
            auto voices = std::make_unique<GrooveVoices>();
            voices->prepare(sr, maxBlock);
            voices->cacheParams(p.apvts);
            std::array<Track*, mu_limits::kMaxLayers> tracks {};
            for (int i = 0; i < 5; ++i) tracks[(size_t) i] = &p.getTrack(i);

            juce::AudioBuffer<float> got(2, maxBlock);
            float peak = 0.0f;
            for (int b = 0; b < 8; ++b)
            {
                voices->beginBlock(tracks.data(), 5);
                voices->applyParams(0.0, 120.0);
                if (b == 0) voices->trigger(0, 1.0f);
                voices->renderAll(maxBlock);
                voices->copyOut(4, got, maxBlock);
                peak = juce::jmax(peak, rmsOf(got, maxBlock));
            }
            expect(peak > 1.0e-4f, "the Rumble sounds while its Kick plays");

            // Point it at a slot that is not live: silence in, and the chain is skipped once the tail is spent.
            setParam(p.apvts, "t4_src", 7.0f);
            float late = 1.0f;
            for (int b = 0; b < (int) (10.0 * sr / maxBlock); ++b)
            {
                voices->beginBlock(tracks.data(), 5);
                voices->applyParams(0.0, 120.0);
                voices->renderAll(maxBlock);
                voices->copyOut(4, got, maxBlock);
                late = rmsOf(got, maxBlock);
            }
            expectEquals(late, 0.0f, "no output after the tail has gone");
        }

        beginTest("RumbleEngine: reset clears the reverb tail; a spent silent feed is skipped; a returning feed plays");
        {
            RumbleEngine r;
            r.prepare(sr, maxBlock);
            // Drive 0, no delay taps, a fully wet reverb: the output is the reverb alone.
            r.setParams(120.0, 0.0f, 0.0f, 0.0f, 0.0f, 0.8f, 1.0f, 18000.0f, 20000.0f, 0.0f);

            juce::AudioBuffer<float> in(2, maxBlock), silent(2, maxBlock), out(2, maxBlock);
            fillBurst(in, maxBlock);
            silent.clear();

            for (int b = 0; b < 3; ++b) { out.clear(); r.render(in, out, maxBlock); }
            out.clear(); r.render(silent, out, maxBlock);
            expect(rmsOf(out, maxBlock) > 1.0e-6f, "a reverb tail follows the feed");
            out.clear(); r.render(silent, out, maxBlock);

            r.reset();
            out.clear(); r.render(silent, out, maxBlock);
            expectEquals(rmsOf(out, maxBlock), 0.0f, "nothing resurfaces after a reset");

            for (int b = 0; b < 3; ++b) { out.clear(); r.render(in, out, maxBlock); }
            float quiet = 1.0f;
            for (int b = 0; b < (int) (10.0 * sr / maxBlock); ++b)
            {
                out.clear(); r.render(silent, out, maxBlock, true);
                quiet = rmsOf(out, maxBlock);
            }
            expectEquals(quiet, 0.0f, "a silent feed ends in silence");

            out.clear(); r.render(in, out, maxBlock);
            expect(rmsOf(out, maxBlock) > 1.0e-6f, "a real feed plays again after the idle spell");
        }
    }
};

static GrooveVoicesTest grooveVoicesTest;
