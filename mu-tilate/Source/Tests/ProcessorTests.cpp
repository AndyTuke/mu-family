// The real mu-Tilate PluginProcessor, built headless: a WAV loaded through loadLoopAsync (worker decode, detection,
// publish on the message thread) plays from the transport through processBlock, a saved session restores the loop by
// path with its slices and timeline, and a missing file reports itself. createEditor is defined here: the editor
// translation unit is not linked into this target (docs/design-plugin-family.md, "Headless product processors").

#include "Plugin/PluginProcessor.h"
#include "Tests/ProcessorSeamHelpers.h"   // mu-core/Tests: TempStorage

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>

juce::AudioProcessorEditor* mu_tilate::PluginProcessor::createEditor() { return nullptr; }

using namespace mu_tilate;

namespace
{
constexpr double kSr = 48000.0;
constexpr int kBlock = 256;

// Four loud bursts half a second apart, two seconds of mono 16-bit audio: one beat each at 120 BPM.
juce::File writeBursts(const juce::File& dir)
{
    const int frames = 96000;
    juce::AudioBuffer<float> b(1, frames);
    b.clear();
    juce::Random rng(5);
    for (int t = 0; t < 4; ++t)
        for (int i = 0; i < (int) (0.3 * kSr); ++i)
            b.setSample(0, t * 24000 + i, 0.8f * std::exp(-(float) i / (0.03f * (float) kSr)) * (rng.nextFloat() * 2.0f - 1.0f));
    auto f = dir.getChildFile("bursts.wav");
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::OutputStream> stream(f.createOutputStream());
    auto writer = wav.createWriterFor(stream, juce::AudioFormatWriterOptions {}.withSampleRate(kSr).withNumChannels(1).withBitsPerSample(16));
    if (writer != nullptr) writer->writeFromAudioSampleBuffer(b, 0, frames);
    return f;
}

// Runs the message loop until `done` or the time is up.
template <class Fn>
bool pumpUntil(Fn&& done, int ms = 15000)
{
    const auto end = juce::Time::getMillisecondCounter() + (juce::uint32) ms;
    while (juce::Time::getMillisecondCounter() < end)
    {
        if (done()) return true;
        juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
    }
    return done();
}

// A host that plays at 120 BPM from beat 0, advanced by the test one block at a time.
struct Host : juce::AudioPlayHead
{
    double bpm = 120.0;
    double ppq = 0.0;
    juce::int64 samples = 0;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo info;
        info.setIsPlaying(true);
        info.setBpm(bpm);
        info.setPpqPosition(ppq);
        info.setTimeInSamples(samples);
        info.setTimeSignature(TimeSignature { 4, 4 });
        return info;
    }
    void advance(int n) { samples += n; ppq += (double) n / kSr * bpm / 60.0; }
};

float renderPeak(PluginProcessor& p, Host& host, int blocks)
{
    juce::AudioBuffer<float> buf(2, kBlock);
    juce::MidiBuffer midi;
    float peak = 0.0f;
    for (int i = 0; i < blocks; ++i)
    {
        buf.clear();
        p.processBlock(buf, midi);
        host.advance(kBlock);
        peak = std::max(peak, buf.getMagnitude(0, kBlock));
    }
    return peak;
}
}

struct ProcessorTests : public juce::UnitTest
{
    ProcessorTests() : juce::UnitTest("Phrase processor", "mu-tilate") {}

    void runTest() override
    {
        mu_seam_test::TempStorage storage;
        const auto file = writeBursts(storage.root);

        beginTest("a loaded loop is detected, laid out and played from the transport");
        {
            PluginProcessor p;
            Host host;
            p.setPlayHead(&host);
            p.setRateAndBufferSizeDetails(kSr, kBlock);
            p.prepareToPlay(kSr, kBlock);
            expectEquals(renderPeak(p, host, 10), 0.0f);                 // no loop: silence
            p.loadLoopAsync(0, file);
            expect(pumpUntil([&] { return p.getPhrase(0).getData() != nullptr && p.getPhrase(0).getData()->audio != nullptr
                                       && p.getPhrase(0).getData()->timeline.size() > 0; }), "the loop did not arrive");
            const auto d = p.getPhrase(0).getData();
            expectGreaterOrEqual(d->slices.size(), 3);                    // bursts at 0, 0.5, 1.0, 1.5 s
            expectGreaterThan(d->timeline.size(), 2);
            expect(p.getLoopStatus(0).contains("slices"), p.getLoopStatus(0));

            host.ppq = 0.0; host.samples = 0;
            expectGreaterThan(renderPeak(p, host, 200), 0.05f);          // it plays
            p.setPlayHead(nullptr);
        }

        beginTest("a saved session restores the loop by path, with its slices and timeline");
        {
            juce::MemoryBlock saved;
            int slices = 0, blocks = 0;
            {
                PluginProcessor p;
                p.prepareToPlay(kSr, kBlock);
                p.loadLoopAsync(0, file);
                expect(pumpUntil([&] { return p.getPhrase(0).getData() != nullptr && p.getPhrase(0).getData()->timeline.size() > 0; }));
                // An edit the file must carry: one block removed from the timeline.
                auto d = p.getPhrase(0).getData();
                Timeline tl = d->timeline;
                tl.remove(0);
                p.getPhrase(0).commitEdit(d->slices, tl);
                slices = d->slices.size();
                blocks = tl.size();
                p.getStateInformation(saved);
            }
            PluginProcessor q;
            q.prepareToPlay(kSr, kBlock);
            q.setStateInformation(saved.getData(), (int) saved.getSize());
            expect(pumpUntil([&] { return q.getPhrase(0).getData() != nullptr && q.getPhrase(0).getData()->audio != nullptr; }), "the loop did not reload");
            const auto d = q.getPhrase(0).getData();
            expectEquals(d->slices.size(), slices);
            expectEquals(d->timeline.size(), blocks);                     // the edit was kept, not laid out afresh
            expect(q.getPhrase(0).loopPath().endsWith("bursts.wav"));
        }

        beginTest("a session restored onto a layer that holds another loop brings in the loop it names");
        {
            juce::MemoryBlock saved;
            {
                PluginProcessor a;
                a.prepareToPlay(kSr, kBlock);
                a.loadLoopAsync(0, file);
                expect(pumpUntil([&] { return a.getPhrase(0).getData() != nullptr && a.getPhrase(0).getData()->timeline.size() > 0; }));
                a.getStateInformation(saved);
            }

            // A shorter loop in the target layer.
            const int shortFrames = 48000;
            juce::AudioBuffer<float> sb(1, shortFrames);
            sb.clear();
            for (int i = 0; i < 4000; ++i) sb.setSample(0, i, 0.5f * (float) (i % 50) / 50.0f);
            const auto other = storage.root.getChildFile("other.wav");
            {
                juce::WavAudioFormat wav;
                std::unique_ptr<juce::OutputStream> stream(other.createOutputStream());
                if (auto w = wav.createWriterFor(stream, juce::AudioFormatWriterOptions {}.withSampleRate(kSr).withNumChannels(1).withBitsPerSample(16)))
                    w->writeFromAudioSampleBuffer(sb, 0, shortFrames);
            }

            PluginProcessor b;
            b.prepareToPlay(kSr, kBlock);
            b.loadLoopAsync(0, other);
            expect(pumpUntil([&] { return b.getPhrase(0).getData() != nullptr && b.getPhrase(0).getData()->audio != nullptr
                                       && b.getPhrase(0).getData()->audio->getNumSamples() == shortFrames; }));
            b.setStateInformation(saved.getData(), (int) saved.getSize());
            expect(pumpUntil([&] { return b.getPhrase(0).getData() != nullptr && b.getPhrase(0).getData()->audio != nullptr
                                       && b.getPhrase(0).getData()->audio->getNumSamples() == 96000; }), "the named loop did not replace the other one");
            expect(b.getPhrase(0).loadedLoopPath().endsWith("bursts.wav"));
        }

        beginTest("a layer preset loaded onto a layer brings in the loop it names, onto an empty layer or one holding another");
        {
            juce::ValueTree tree;
            {
                PluginProcessor a;
                a.prepareToPlay(kSr, kBlock);
                a.loadLoopAsync(0, file);
                expect(pumpUntil([&] { return a.getPhrase(0).getData() != nullptr && a.getPhrase(0).getData()->timeline.size() > 0; }));
                tree = a.captureLayerPresetTree(0);
            }
            expect(tree.isValid());
            const auto preset = storage.root.getChildFile("layer.muPhrase");
            if (auto xml = tree.createXml()) preset.replaceWithText(xml->toString());

            // Onto an empty layer.
            {
                PluginProcessor b;
                b.prepareToPlay(kSr, kBlock);
                b.loadLayerPreset(0, preset);
                expect(pumpUntil([&] { return b.getPhrase(0).getData() != nullptr && b.getPhrase(0).getData()->audio != nullptr
                                           && b.getPhrase(0).getData()->audio->getNumSamples() == 96000; }), "the preset's loop did not load onto an empty layer");
            }

            // Onto a layer holding a shorter loop.
            {
                juce::AudioBuffer<float> sb(1, 48000);
                sb.clear();
                const auto other = storage.root.getChildFile("other2.wav");
                {
                    juce::WavAudioFormat wav;
                    std::unique_ptr<juce::OutputStream> stream(other.createOutputStream());
                    if (auto w = wav.createWriterFor(stream, juce::AudioFormatWriterOptions {}.withSampleRate(kSr).withNumChannels(1).withBitsPerSample(16)))
                        w->writeFromAudioSampleBuffer(sb, 0, 48000);
                }
                PluginProcessor b;
                b.prepareToPlay(kSr, kBlock);
                b.loadLoopAsync(0, other);
                expect(pumpUntil([&] { return b.getPhrase(0).getData() != nullptr && b.getPhrase(0).getData()->audio != nullptr; }));
                b.loadLayerPreset(0, preset);
                expect(pumpUntil([&] { return b.getPhrase(0).getData() != nullptr && b.getPhrase(0).getData()->audio != nullptr
                                           && b.getPhrase(0).getData()->audio->getNumSamples() == 96000; }), "the preset's loop did not replace the other one");
            }
        }

        // Not a check: with MU_TILATE_DEMO_LOOP (a loop file), MU_TILATE_DEMO_TEMPO (its tempo) and MU_TILATE_DEMO_OUT (a folder)
        // set, renders the loop at a faster project tempo in the three tempo modes to WAV files, to be listened to.
        const auto demoLoop = juce::SystemStats::getEnvironmentVariable("MU_TILATE_DEMO_LOOP", {});
        if (demoLoop.isNotEmpty())
        {
            beginTest("demo render (not a check)");
            const auto out = juce::File(juce::SystemStats::getEnvironmentVariable("MU_TILATE_DEMO_OUT", "."));
            out.createDirectory();
            const double loopTempo = juce::SystemStats::getEnvironmentVariable("MU_TILATE_DEMO_TEMPO", "80").getDoubleValue();

            // Renders 12 s of the loop to demo_<name>.wav: tempo mode `mode` at `projectTempo`, after `prepare` has edited the layer.
            auto renderDemo = [&](const juce::String& name, int mode, double projectTempo, const std::function<void(PluginProcessor&)>& prepare)
            {
                PluginProcessor p;
                Host host;
                host.bpm = projectTempo;
                p.setPlayHead(&host);
                p.setRateAndBufferSizeDetails(kSr, kBlock);
                p.prepareToPlay(kSr, kBlock);
                auto set = [&](int row, float v)
                {
                    auto* prm = p.apvts.getParameter(layerParamPrefix(0) + ppi::suffix[row]);
                    prm->setValueNotifyingHost(prm->convertTo0to1(v));
                };
                set(ppi::mode, (float) mode);
                set(ppi::ltempo, (float) loopTempo);
                p.loadLoopAsync(0, juce::File(demoLoop));
                pumpUntil([&] { return p.getPhrase(0).getData() != nullptr && p.getPhrase(0).getData()->timeline.size() > 0; }, 30000);
                if (p.getPhrase(0).getData() == nullptr) { logMessage("demo: the loop did not load: " + p.getLoopStatus(0)); return; }
                if (prepare) prepare(p);

                const int total = (int) (kSr * 12.0);
                juce::AudioBuffer<float> all(2, total);
                juce::AudioBuffer<float> blockBuf(2, kBlock);
                juce::MidiBuffer midi;
                for (int pos = 0; pos + kBlock <= total; pos += kBlock)
                {
                    blockBuf.clear();
                    p.processBlock(blockBuf, midi);
                    host.advance(kBlock);
                    for (int c = 0; c < 2; ++c) all.copyFrom(c, pos, blockBuf, c, 0, kBlock);
                }
                const auto demoFile = out.getChildFile("demo_" + name + ".wav");
                demoFile.deleteFile();
                juce::WavAudioFormat wav;
                std::unique_ptr<juce::OutputStream> stream(demoFile.createOutputStream());
                if (auto w = wav.createWriterFor(stream, juce::AudioFormatWriterOptions {}.withSampleRate(kSr).withNumChannels(2).withBitsPerSample(24)))
                    w->writeFromAudioSampleBuffer(all, 0, total);
                logMessage("demo " + name + ": " + p.getLoopStatus(0) + ", peak " + juce::String(all.getMagnitude(0, total), 3)
                           + ", rms " + juce::String(all.getRMSLevel(0, 0, total), 3) + " -> " + demoFile.getFullPathName());
                p.setPlayHead(nullptr);
            };

            // The three tempo modes at a faster project tempo.
            const char* names[] = { "off", "repitch", "stretch" };
            for (int mode = 0; mode < 3; ++mode)
                renderDemo(names[mode], mode, loopTempo * 1.25, {});

            // The loop at its own tempo, plain and with a pre-fade on its loudest hits (a reversed swell of 200 ms at -9 dB, a little
            // dark); and the same with the stretch at a faster tempo, which transposes nothing and keeps the swell in time.
            auto addPreFade = [](PluginProcessor& p)
            {
                const auto d = p.getPhrase(0).getData();
                SliceList slices = d->slices;
                std::vector<float> attack((size_t) slices.size(), 0.0f);
                float loudest = 0.0f;
                for (int i = 0; i < slices.size(); ++i)
                {
                    const auto r = slices.slice(i);
                    const int n = std::min(r.length(), (int) (0.03 * d->fileSampleRate));
                    attack[(size_t) i] = d->audio->getMagnitude(0, r.start, n);
                    loudest = std::max(loudest, attack[(size_t) i]);
                }
                Timeline timeline = d->timeline;
                for (int b = 0; b < timeline.size(); ++b)   // each hit has its own settings: switch the pre-fade on for the loudest
                {
                    const int i = timeline.blocks()[(size_t) b].slice;
                    if (i < 1 || attack[(size_t) i] <= 0.6f * loudest) continue;
                    SliceSettings s = timeline.blocks()[(size_t) b].own;
                    s.preFade = true; s.preLengthMs = 200.0f; s.preLevelDb = -9.0f; s.preTone = -0.3f;
                    timeline.setSettings(b, s);
                }
                p.getPhrase(0).commitEdit(slices, timeline);
            };
            renderDemo("plain", 0, loopTempo, {});
            renderDemo("prefade", 0, loopTempo, addPreFade);
            renderDemo("prefade_stretch", 2, loopTempo * 1.25, addPreFade);
        }

        beginTest("a missing file reports itself and leaves the layer as it was");
        {
            PluginProcessor p;
            p.prepareToPlay(kSr, kBlock);
            p.loadLoopAsync(0, storage.root.getChildFile("nope.wav"));
            expect(pumpUntil([&] { return p.getLoopStatus(0).startsWith("File not found"); }), p.getLoopStatus(0));
            expect(p.getPhrase(0).getData() == nullptr);
        }
    }
};

static ProcessorTests processorTests;
