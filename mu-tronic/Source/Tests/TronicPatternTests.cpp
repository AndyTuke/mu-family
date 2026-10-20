// The Pattern layer type in a mu-Tronic slot (docs/mu-tronic/design.md T1b, 2.1.10 points 3 and 5, 2.4): its rows and the
// shared tonal centre are in the 16-slot layout and saved only for a slot that holds a Pattern; it renders the stream
// mu-Tant records; its Note mode is per layer and saved in the layer node; the wavetable bank and the slots' engines are
// the type's shared services; a replaced Pattern fades out and leaves nothing stuck; its presets are mu-Tant's own and
// carry the layer's data.

#include "Plugin/PluginProcessor.h"
#include "Persistence/PatternLayerType.h"
#include "Sequencer/PatternType.h"          // mu-Tant's layer library: the layer, its services and parameter table
#include "UI/PatternHost.h"                 // and what the layer reads from its host
#include "Tests/AllocationProbe.h"          // the operators are defined in TronicArpTests.cpp (one test executable)
#include "Tests/PatternLayerFixture.h"      // mu-Tant: the recorded Pattern stream shared with mu-Tant's own tests
#include "Tests/GoldenRender.h"             // mu-core: MockHost
#include "Tests/ProcessorSeamHelpers.h"

#include <atomic>
#include <cmath>
#include <thread>

namespace
{
    using mu_core::LayerTypeId;

    // The processor with the channel tap, the bin and the view exposed.
    struct PatternProcessor : mu_tronic::PluginProcessor
    {
        using mu_tronic::PluginProcessor::layerAccess;
        using mu_tronic::PluginProcessor::retireBin;
        using mu_tronic::PluginProcessor::typedLayerPresetDir;
        using mu_tronic::PluginProcessor::typedLayerPresetExtension;
    };

    mu_tant::Pattern& patternIn(ProcessorBase& p, int slot) { return static_cast<mu_tant::Pattern&>(*p.getLayer(slot)); }

    int rowCount(const juce::ValueTree& node)
    {
        int n = 0;
        for (int i = 0; i < node.getNumChildren(); ++i)
            if (node.getChild(i).hasType(mu_pp::kRowTag)) ++n;
        return n;
    }

    float getPlain(juce::AudioProcessorValueTreeState& apvts, const juce::String& id)
    {
        auto* p = apvts.getParameter(id);
        return p->convertFrom0to1(p->getValue());
    }

    // Appends channel 0's stream as the mixer receives it (sized up front: nothing allocates while rendering).
    struct Channel0Tap : MixerEngine::ChannelTap
    {
        explicit Channel0Tap(int frames) { buf.setSize(2, frames); buf.clear(); }
        void capture(int ch, const juce::AudioBuffer<float>& b, int n) noexcept override
        {
            if (ch != 0) return;
            const int len = juce::jmin(n, buf.getNumSamples() - written);
            for (int c = 0; c < 2 && c < b.getNumChannels(); ++c)
                if (len > 0) juce::FloatVectorOperations::copy(buf.getWritePointer(c) + written, b.getReadPointer(c), len);
            written += juce::jmax(0, len);
        }
        juce::AudioBuffer<float> buf;
        int written = 0;
    };

    // The peak each channel handed the mixer since the last reset.
    struct PeakTap : MixerEngine::ChannelTap
    {
        void capture(int ch, const juce::AudioBuffer<float>& b, int n) noexcept override
        {
            if (ch < 0 || ch >= 16) return;
            for (int c = 0; c < 2 && c < b.getNumChannels(); ++c)
                peak[ch] = juce::jmax(peak[ch], b.getMagnitude(c, 0, n));
        }
        void reset() { for (auto& v : peak) v = 0.0f; }
        float peak[16] {};
    };

    // A short wavetable wav on disk (4 frames of 2048 samples).
    juce::File writeWavetable(const juce::File& dir, const juce::String& name)
    {
        const int frames = 4, frameSize = 2048;
        juce::AudioBuffer<float> data(1, frames * frameSize);
        for (int f = 0; f < frames; ++f)
            for (int i = 0; i < frameSize; ++i)
            {
                const double ph = juce::MathConstants<double>::twoPi * i / frameSize;
                data.setSample(0, f * frameSize + i, 0.5f * (float) (std::sin(ph) + (f / 3.0) * std::sin(3.0 * ph) / 3.0));
            }
        auto file = dir.getChildFile(name + ".wav");
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> out = file.createOutputStream();
        if (out == nullptr) return file;
        auto writer = wav.createWriterFor(out, juce::AudioFormatWriterOptions{}.withSampleRate(48000.0)
                                                   .withNumChannels(1).withBitsPerSample(16));
        if (writer != nullptr) writer->writeFromAudioSampleBuffer(data, 0, data.getNumSamples());
        return file;
    }

    // Runs `blocks` blocks of `n` frames through a prepared, playing processor.
    struct Run
    {
        Run(mu_tronic::PluginProcessor& proc, int blockSize) : p(proc), n(blockSize), buf(2, blockSize)
        {
            host.bpm = 120.0;
            p.setPlayHead(&host);
            p.setRateAndBufferSizeDetails(48000.0, n);
            p.prepareToPlay(48000.0, n);
        }
        ~Run() { p.setPlayHead(nullptr); p.releaseResources(); }

        void block(const juce::MidiBuffer& midi = {})
        {
            juce::MidiBuffer m(midi);
            host.timeInSamples = (juce::int64) index * n;
            buf.clear();
            p.processBlock(buf, m);
            host.ppq += (double) n * host.bpm / (60.0 * 48000.0);
            ++index;
        }

        mu_tronic::PluginProcessor& p;
        int n, index = 0;
        mu_golden::MockHost host;
        juce::AudioBuffer<float> buf;
    };

    // The gate bypassed, so the drone sounds without a gate pattern.
    void openGate(ProcessorBase& p, int slot) { mu_seam_test::setParam(p.apvts, mu_tant::layerParamId(slot, "gate_bypass"), 1.0f); }
}

struct MuTronicPatternTests : juce::UnitTest
{
    MuTronicPatternTests() : juce::UnitTest("mu-Tronic Pattern layer type", "mu-tronic-pattern") {}

    void runTest() override
    {
        layoutAndRegistry();
        savedState();
        sameStreamAsMuTant();
        noteModePerLayer();
        servicesAndEngines();
        wavetablesThroughTheBank();
        swapLeavesNothingStuck();
        successorInsideTheFade();
        gatedTailIsSilent();
        noteSwitchRamps();
        presets();
        typeChangeStress();
    }

    void layoutAndRegistry()
    {
        beginTest("the Pattern type is offered, its rows exist under v{N}_ for all sixteen slots, and the tonal centre once");
        mu_seam_test::TempStorage storage;
        PatternProcessor p;
        const auto* info = p.getLayerTypes().find(LayerTypeId::Pattern);
        expect(info != nullptr);
        if (info != nullptr)
        {
            expectEquals(juce::String(info->name), juce::String("Pattern"));
            expectEquals(juce::String(info->group), juce::String("mu-Tant"));
            expectEquals((int) info->paramPrefixLetter, (int) 'v');
            expectEquals(juce::String(info->presetExtension), juce::String(".muPattern"));
            expectEquals(juce::String(info->presetFolder), juce::String("Voices"));
            expectEquals(juce::String(info->presetTag), juce::String("MuTantVoice"));
            expectEquals(juce::String(info->typeTag), juce::String("Tant"));
            expect(info->createServices != nullptr && info->attachServices != nullptr && info->addGlobalParams != nullptr);
        }

        for (int s : { 0, 7, 15 })
            expect(p.apvts.getParameter("v" + juce::String(s) + "_flt_cut") != nullptr, "v" + juce::String(s) + "_flt_cut");
        expect(p.apvts.getParameter("v16_flt_cut") == nullptr, "no seventeenth slot");
        int patternRows = 0, firstSlotRows = 0, rootRows = 0, scaleRows = 0;
        for (auto* raw : p.getParameters())
            if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*>(raw))
            {
                const auto id = withId->getParameterID();
                if (id.startsWith("v3_")) ++patternRows;
                if (id.startsWith("v0_")) ++firstSlotRows;
                if (id == mu_tant::kRootParamId) ++rootRows;
                if (id == mu_tant::kScaleParamId) ++scaleRows;
            }
        expect(patternRows > 30, "the Pattern table has its rows");
        logMessage("Pattern rows per slot: " + juce::String(patternRows));
        expectEquals(patternRows, firstSlotRows, "and every slot has the whole table");
        expectEquals(rootRows, 1, "one shared root");
        expectEquals(scaleRows, 1, "one shared scale");
    }

    // A slot's node holds the rows of its live type only, the shared tonal centre is a global, and the layer's own data
    // (gate envelopes, Note mode) comes back with the type.
    void savedState()
    {
        beginTest("a slot saves the rows of its live type only; the layer's envelopes, Note mode and the tonal centre round trip");
        mu_seam_test::TempStorage storage;
        PatternProcessor p;
        expect(p.setLayerType(3, LayerTypeId::Pattern));
        expect(p.setLayerType(4, LayerTypeId::Pattern));
        mu_seam_test::setParam(p.apvts, "v3_flt_cut", 1800.0f);
        mu_seam_test::setParam(p.apvts, "v7_flt_cut", 900.0f);   // slot 7 is Empty: this prefix is dormant
        mu_seam_test::setParam(p.apvts, mu_tant::kRootParamId, 5.0f);
        patternIn(p, 3).gate.patternLengthBars.store(2);
        patternIn(p, 3).gate.addEnvelope({ 0, 6, 0.2f, 0.0f, 0.0f, false });
        expect(mu_tronic::setPatternNoteMode(p, 3, 1));

        auto state = p.captureState();
        expect(rowCount(mu_pp::findLayerNode(state, 3)) > 30, "a Pattern slot saves its rows");
        expectEquals(rowCount(mu_pp::findLayerNode(state, 7)), 0, "a dormant prefix is not written");
        expectEquals(rowCount(mu_pp::findLayerNode(state, 0)), 0, "an Empty slot writes no rows");
        expectEquals((int) mu_pp::findLayerNode(state, 3).getProperty("noteMode", -1), 1, "the Note mode is in the layer's node");
        expectEquals((int) mu_pp::findLayerNode(state, 4).getProperty("noteMode", -1), 0, "and per layer: the other Pattern is Free");
        expect(! mu_pp::findLayerNode(state, 7).hasProperty("noteMode"), "an Empty slot has none");

        juce::MemoryBlock block;
        p.getStateInformation(block);
        mu_seam_test::TempStorage storage2;
        PatternProcessor q;
        q.setStateInformation(block.getData(), (int) block.getSize());
        expect(q.getLayerTypeId(3) == LayerTypeId::Pattern && q.getLayerTypeId(4) == LayerTypeId::Pattern);
        expectWithinAbsoluteError(getPlain(q.apvts, "v3_flt_cut"), 1800.0f, 2.0f);
        expectEquals(getPlain(q.apvts, mu_tant::kRootParamId), 5.0f, "the tonal centre is a global and is restored");
        expectEquals(mu_tronic::getPatternNoteMode(q, 3), 1);
        expectEquals(mu_tronic::getPatternNoteMode(q, 4), 0);
        expectEquals(patternIn(q, 3).gate.patternLengthBars.load(), 2, "the gate pattern comes back with the layer");
        auto again = q.captureState();
        expect(again.isEquivalentTo(state), "restoring then capturing reproduces the saved state");

        // A slot that loads Empty leaves the Pattern rows of the previous tenant out of the next save.
        q.flushPendingAsyncUpdates();
        expect(q.setLayerType(3, LayerTypeId::Empty));
        expectEquals(rowCount(mu_pp::findLayerNode(q.captureState(), 3)), 0);
        expectEquals(mu_tronic::getPatternNoteMode(q, 3), 0, "an Empty slot has no Note mode");
        expect(! mu_tronic::setPatternNoteMode(q, 3, 1), "and takes none");
        q.flushPendingAsyncUpdates();
        p.flushPendingAsyncUpdates();
    }

    // The recorded Pattern stream of mu-Tant (Tests/PatternLayerFixture.h): the layer in a mu-Tronic slot must reproduce
    // it bit for bit, which is "sample-identical to mu-Tant's own" since mu-Tant's tests hold the same hash. mu-Tronic
    // gates Note mode on the layer's own output where mu-Tant gates the final mix, so the layer's own gate is switched
    // off for this comparison: it is the only difference between the two hosts.
    void sameStreamAsMuTant()
    {
        beginTest("a Pattern in a mu-Tronic slot renders the stream mu-Tant records");
        namespace fx = mu_tant_fixture;
        mu_seam_test::TempStorage storage;
        mu_seam_test::ScopedEnvVar deterministic("MU_DETERMINISTIC_NOISE", "1");

        PatternProcessor p;
        mu_golden::MockHost host;
        host.bpm = fx::kBpm;
        p.setPlayHead(&host);
        p.setRateAndBufferSizeDetails(fx::kSampleRate, fx::kBlock);
        p.prepareToPlay(fx::kSampleRate, fx::kBlock);
        expect(p.setLayerType(0, LayerTypeId::Pattern));
        expect(patternIn(p, 0).gatesOwnOutput, "the mu-Tronic Pattern gates its own output");
        patternIn(p, 0).gatesOwnOutput = false;
        expect(mu_tronic::setPatternNoteMode(p, 0, 1));
        fx::configure(p.apvts, patternIn(p, 0), 0);

        Channel0Tap tap(fx::kBlock * fx::kBlocks);
        p.setChannelTapForTest(&tap);
        juce::AudioBuffer<float> buf(2, fx::kBlock);
        for (int b = 0; b < fx::kBlocks; ++b)
        {
            juce::MidiBuffer midi;
            for (const auto& k : fx::keys())
                if (k.block == b) midi.addEvent(juce::MidiMessage(k.status, k.note, k.velocity), 0);
            host.timeInSamples = (juce::int64) b * fx::kBlock;
            buf.clear();
            p.processBlock(buf, midi);
            host.ppq += (double) fx::kBlock * fx::kBpm / (60.0 * fx::kSampleRate);
        }
        p.setChannelTapForTest(nullptr);
        p.setPlayHead(nullptr);
        p.releaseResources();

        expectEquals(tap.written, fx::kBlock * fx::kBlocks, "every block reached the mixer");
        fx::StreamHash hash;
        for (int b = 0; b < fx::kBlocks; ++b) hash.add(tap.buf, fx::kBlock, b * fx::kBlock);
        logMessage("mu-Tronic Pattern stream hash " + juce::String::toHexString((juce::int64) hash.h));
        expect(hash.h == fx::kRecordedHash, "the slot renders the recorded Pattern stream");
        expect(hash.peak > 0.01f && hash.peak < 4.0f, "and it is a real, bounded sound");
        p.flushPendingAsyncUpdates();
    }

    // Note mode is per layer: a Free Pattern drones, a Note one is silent until a key is held, in the same processor.
    void noteModePerLayer()
    {
        beginTest("Note mode is per layer: Free drones, Note is silent until a key is held and again after its release");
        mu_seam_test::TempStorage storage;
        PatternProcessor p;
        expect(p.setLayerType(1, LayerTypeId::Pattern));
        expect(p.setLayerType(2, LayerTypeId::Pattern));
        openGate(p, 1);
        openGate(p, 2);
        expect(mu_tronic::setPatternNoteMode(p, 2, 1));
        expect(! mu_tronic::setPatternNoteMode(p, 5, 1), "an Empty slot takes none");

        PeakTap tap;
        p.setChannelTapForTest(&tap);
        {
            Run run(p, 256);
            for (int b = 0; b < 40; ++b) run.block();   // settle: the mode change resets the held keys on the first block
            tap.reset();
            for (int b = 0; b < 40; ++b) run.block();
            expect(tap.peak[1] > 0.01f, "the Free Pattern drones");
            expectEquals(tap.peak[2], 0.0f, "the Note Pattern with no key is exactly silent");

            tap.reset();
            juce::MidiBuffer on;
            on.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8) 100), 0);
            run.block(on);
            for (int b = 0; b < 60; ++b) run.block();
            expect(tap.peak[2] > 0.01f, "a held key opens the Note Pattern");

            juce::MidiBuffer off;
            off.addEvent(juce::MidiMessage::noteOff(1, 60), 0);
            run.block(off);
            for (int b = 0; b < 10; ++b) run.block();   // the 8 ms ramp (about 2 blocks) has finished
            tap.reset();
            for (int b = 0; b < 20; ++b) run.block();
            expectEquals(tap.peak[2], 0.0f, "after the release and the ramp it is silent again");
            expect(tap.peak[1] > 0.01f, "the Free Pattern kept droning throughout");
        }
        p.setChannelTapForTest(nullptr);

        // A layer preset from mu-Tant carries no Note mode and leaves the layer's own choice alone.
        juce::ValueTree node(mu_pp::kLayerTag);
        patternIn(p, 2).applyExtras(node);
        expectEquals(mu_tronic::getPatternNoteMode(p, 2), 1);
        p.flushPendingAsyncUpdates();
    }

    // The bank, its lock and the engines are the type's services: one per processor, shared by every Pattern, built with
    // the first one; an engine belongs to its slot and outlives the Pattern in it.
    void servicesAndEngines()
    {
        beginTest("the Pattern layers share one set of services; each slot has its own engine, which outlives its Pattern");
        mu_seam_test::TempStorage storage;
        PatternProcessor p;
        expect(p.findLayerServices(LayerTypeId::Pattern) == nullptr, "nothing is built before the first Pattern");
        expect(p.setLayerType(1, LayerTypeId::Pattern));
        auto* services = p.findLayerServices(LayerTypeId::Pattern);
        expect(services != nullptr);
        expect(p.setLayerType(5, LayerTypeId::Pattern));
        expect(p.findLayerServices(LayerTypeId::Pattern) == services, "the second Pattern shares them");

        auto* host1 = patternIn(p, 1).host.ptr;
        auto* host5 = patternIn(p, 5).host.ptr;
        expect(host1 != nullptr && host1 == host5, "both Patterns are linked to the one host adapter");
        if (host1 == nullptr) return;
        auto* e1 = host1->getPatternEngine(1);
        auto* e5 = host1->getPatternEngine(5);
        expect(e1 != nullptr && e5 != nullptr && e1 != e5, "each slot has an engine of its own");
        expect(host1->getPatternBankLock() != nullptr, "and the bank has a lock");
        expect(host1->getPatternEngine(2) == nullptr, "a slot that never held a Pattern has none");

        expect(p.setLayerType(1, LayerTypeId::Empty));
        expect(p.setLayerType(1, LayerTypeId::Pattern));
        auto* e1b = host1->getPatternEngine(1);
        expect(e1b != nullptr && e1b != e1, "a replacement takes the slot's other engine, so the old tail keeps its own");
        expect(p.setLayerType(1, LayerTypeId::Empty));
        expect(p.setLayerType(1, LayerTypeId::Pattern));
        expect(host1->getPatternEngine(1) == e1, "and the pool of two alternates");
        expect(patternIn(p, 1).host.ptr == host1);
        p.flushPendingAsyncUpdates();

        // The processor goes with Patterns in slots and a replaced one possibly still in the bin.
        expect(p.setLayerType(5, LayerTypeId::Empty));
    }

    // A user wavetable is a file in mu-Tant's folder: it enters the shared bank through the host adapter, travels in the
    // layer's extras and is found (or loaded) again by a processor that has not seen it.
    void wavetablesThroughTheBank()
    {
        beginTest("a user wavetable loads into the shared bank, is saved with the layer and resolves in a fresh processor");
        mu_seam_test::TempStorage storage;
        const auto wav = writeWavetable(storage.root, "mytable");
        expect(wav.existsAsFile());

        PatternProcessor p;
        expect(p.setLayerType(2, LayerTypeId::Pattern));
        auto* host = patternIn(p, 2).host.ptr;
        expect(host != nullptr);
        if (host == nullptr) return;
        expect(host->getPatternWavetablesDir().getFileName() == "Wavetables", "the browser opens mu-Tant's Wavetables folder");
        expect(host->getPatternWavetablesDir().getParentDirectory().getFileName() == "muTant");
        expectEquals(host->getPatternWavetablePath(2, 0), juce::String(), "the factory selection to start with");

        host->loadPatternWavetable(2, 0, wav);
        expectEquals(host->getPatternWavetablePath(2, 0), wav.getFullPathName());
        const int idx = patternIn(p, 2).osc1UserIndex.load();
        expectGreaterThan(idx, 5, "appended after the factory tables");
        expect(! host->isPatternWavetableMissing(2, 0));
        expectEquals(host->resolvePatternWavetable(wav.getFullPathName()), idx, "the same file is the same table");

        // A second Pattern asking for the same file gets the same table, not a copy.
        expect(p.setLayerType(6, LayerTypeId::Pattern));
        host->loadPatternWavetable(6, 1, wav);
        expectEquals(patternIn(p, 6).osc2UserIndex.load(), idx, "one table in the bank for both");

        auto state = p.captureState();
        expectEquals(mu_pp::findLayerNode(state, 2).getProperty("o1WtPath").toString(), wav.getFullPathName(), "the path is saved");

        mu_seam_test::TempStorage storage2;
        PatternProcessor q;
        q.applyStateTree(state);
        expect(q.getLayerTypeId(2) == LayerTypeId::Pattern);
        expectEquals(patternIn(q, 2).osc1UserPath, wav.getFullPathName());
        expectGreaterThan(patternIn(q, 2).osc1UserIndex.load(), 5, "the fresh bank loaded it from the file");

        // A path whose file has gone resolves to none and is reported missing.
        host->clearPatternWavetable(2, 0);
        expectEquals(patternIn(p, 2).osc1UserIndex.load(), -1);
        expectEquals(host->resolvePatternWavetable((storage.root.getChildFile("gone.wav")).getFullPathName()), -1);
        patternIn(p, 2).osc1UserPath = storage.root.getChildFile("gone.wav").getFullPathName();
        expect(host->isPatternWavetableMissing(2, 0));
        p.flushPendingAsyncUpdates();
        q.flushPendingAsyncUpdates();
    }

    // A Pattern replaced while it sounds fades out through the shared bin (25 ms) and then nothing of it is left: the
    // slot is silent, the bin is empty, and the next Pattern in the slot renders normally.
    void swapLeavesNothingStuck()
    {
        beginTest("replacing a sounding Pattern fades it out; nothing is left in the bin or stuck in the slot's engine");
        mu_seam_test::TempStorage storage;
        PatternProcessor p;
        expect(p.setLayerType(0, LayerTypeId::Pattern));
        openGate(p, 0);

        PeakTap tap;
        p.setChannelTapForTest(&tap);
        {
            Run run(p, 256);
            for (int b = 0; b < 60; ++b) run.block();
            tap.reset();
            for (int b = 0; b < 10; ++b) run.block();
            expect(tap.peak[0] > 0.01f, "it sounds before the swap");

            expect(p.setLayerType(0, LayerTypeId::Empty));   // the Pattern leaves; its tail fades in the bin
            tap.reset();
            run.block();
            run.block();
            run.block();
            run.block();
            expect(tap.peak[0] > 0.0f, "the tail is still heard while it fades");
            for (int b = 0; b < 12; ++b) run.block();      // 25 ms is five 256-frame blocks
            tap.reset();
            for (int b = 0; b < 20; ++b) run.block();
            expectEquals(tap.peak[0], 0.0f, "after the fade the slot is silent");
            p.flushPendingAsyncUpdates();
            expect(! p.retireBin.hasPendingWork(), "the bin holds nothing once the grace has passed");

            // Back to a Pattern in the same slot: it sounds again through the same engine.
            expect(p.setLayerType(0, LayerTypeId::Pattern));
            openGate(p, 0);
            for (int b = 0; b < 40; ++b) run.block();
            tap.reset();
            for (int b = 0; b < 10; ++b) run.block();
            expect(tap.peak[0] > 0.01f, "the new Pattern sounds");
        }
        p.setChannelTapForTest(nullptr);
        p.flushPendingAsyncUpdates();
    }


    // The Goertzel magnitude of `frames` samples of channel 0 from `from` at `hz`.
    static double goertzel(const juce::AudioBuffer<float>& b, int from, int frames, double hz)
    {
        const double w = 2.0 * juce::MathConstants<double>::pi * hz / 48000.0, c = 2.0 * std::cos(w);
        double s1 = 0.0, s2 = 0.0;
        const float* x = b.getReadPointer(0);
        for (int i = 0; i < frames; ++i) { const double s0 = x[from + i] + c * s1 - s2; s2 = s1; s1 = s0; }
        return std::sqrt(s1 * s1 + s2 * s2 - c * s1 * s2) / frames;
    }

    static float maxStep(const juce::AudioBuffer<float>& b, int from, int to)
    {
        float m = 0.0f;
        const float* x = b.getReadPointer(0);
        for (int i = from + 1; i < to; ++i) m = juce::jmax(m, std::abs(x[i] - x[i - 1]));
        return m;
    }

    // A Pattern put back into a slot inside the 25 ms fade of the one before it: the tail and its successor each have an
    // engine, so neither advances the other's phase. A shared engine made the tail a series of phase jumps at block edges.
    void successorInsideTheFade()
    {
        beginTest("Pattern, Empty, Pattern inside one fade: the tail has no steps at block edges and the successor holds the pitch");
        for (int block : { 64, 100, 128, 256, 512 })
        {
            mu_seam_test::TempStorage storage;
            PatternProcessor p;
            expect(p.setLayerType(0, LayerTypeId::Pattern));
            openGate(p, 0);
            const int preFrames = 8192, postFrames = 8192;
            const int preBlocks = preFrames / block, postBlocks = postFrames / block;
            Channel0Tap tap((preBlocks + postBlocks) * block);
            p.setChannelTapForTest(&tap);
            {
                Run run(p, block);
                for (int b = 0; b < preBlocks; ++b) run.block();
                expect(p.setLayerType(0, LayerTypeId::Empty));
                expect(p.setLayerType(0, LayerTypeId::Pattern));
                openGate(p, 0);
                for (int b = 0; b < postBlocks; ++b) run.block();
            }
            p.setChannelTapForTest(nullptr);

            const int preEnd = preBlocks * block;
            const float steady = maxStep(tap.buf, 4096, preEnd);
            expect(steady > 1.0e-4f, "the pre-swap drone sounds");

            double bestHz = 0.0, best = 0.0;
            for (double hz = 20.0; hz < 4000.0; hz += 1.0)
                if (const double m = goertzel(tap.buf, preEnd - 4096, 4096, hz); m > best) { best = m; bestHz = hz; }

            // The tail's 25 ms (1200 frames), after the first block that carries the successor's own start.
            const float overlap = maxStep(tap.buf, preEnd + block, preEnd + 1200);
            logMessage("block " + juce::String(block) + ": fundamental " + juce::String(bestHz) + " Hz, steady step "
                       + juce::String(steady) + ", step in the overlap " + juce::String(overlap));
            expect(overlap <= 3.0f * steady + 1.0e-4f, "block " + juce::String(block) + ": a step at a block edge in the overlap");

            // The successor alone, after the fade: the same fundamental as before the swap.
            double postHz = 0.0, postBest = 0.0;
            for (double hz = 20.0; hz < 4000.0; hz += 1.0)
                if (const double m = goertzel(tap.buf, preEnd + 2400, 4096, hz); m > postBest) { postBest = m; postHz = hz; }
            expectWithinAbsoluteError(postHz, bestHz, 1.0, "block " + juce::String(block) + ": the successor keeps the pitch");
            p.flushPendingAsyncUpdates();
        }
    }

    // A Pattern that was silent when it left (Note mode with no key held; a stopped transport) leaves a silent tail.
    void gatedTailIsSilent()
    {
        beginTest("a gated Pattern swapped away leaves a silent tail: Note mode with no key, and a stopped transport");
        const int block = 128;
        for (int variant = 0; variant < 2; ++variant)
        {
            mu_seam_test::TempStorage storage;
            PatternProcessor p;
            expect(p.setLayerType(0, LayerTypeId::Pattern));
            if (variant == 0)
            {
                openGate(p, 0);
                expect(mu_tronic::setPatternNoteMode(p, 0, 1));   // no key is ever held
            }   // variant 1: the gate stays closed (no bypass, no envelopes) while the transport plays
            PeakTap peaks;
            p.setChannelTapForTest(&peaks);
            {
                Run run(p, block);
                for (int b = 0; b < 40; ++b) run.block();
                expectEquals(peaks.peak[0], 0.0f, "silent before the swap");
                expect(p.setLayerType(0, LayerTypeId::Empty));
                peaks.reset();
                for (int b = 0; b < 20; ++b) run.block();   // 20 blocks of 128 frames cover the 25 ms fade twice over
                expectEquals(peaks.peak[0], 0.0f, juce::String(variant == 0 ? "Note mode, no key" : "closed gate") + ": the tail is silent");
            }
            p.setChannelTapForTest(nullptr);
            p.flushPendingAsyncUpdates();
        }

        // The control: a sounding Pattern in Note mode with a key held does leave an audible, fading tail.
        mu_seam_test::TempStorage storage;
        PatternProcessor p;
        expect(p.setLayerType(0, LayerTypeId::Pattern));
        openGate(p, 0);
        expect(mu_tronic::setPatternNoteMode(p, 0, 1));
        PeakTap peaks;
        p.setChannelTapForTest(&peaks);
        {
            Run run(p, block);
            juce::MidiBuffer on;
            on.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8) 100), 0);
            run.block(on);
            for (int b = 0; b < 40; ++b) run.block();
            expect(peaks.peak[0] > 0.01f, "sounding before the swap");
            expect(p.setLayerType(0, LayerTypeId::Empty));
            peaks.reset();
            run.block();
            run.block();
            expect(peaks.peak[0] > 0.01f, "a sounding Pattern keeps its tail");
        }
        p.setChannelTapForTest(nullptr);
        p.flushPendingAsyncUpdates();
    }

    // Free to Note on a sounding layer closes the own-output gate over its 8 ms ramp, not in one sample.
    void noteSwitchRamps()
    {
        beginTest("Free to Note on a sounding Pattern ramps the gate shut: no sample step beyond the drone's own");
        for (int block : { 64, 256 })
        {
            mu_seam_test::TempStorage storage;
            PatternProcessor p;
            expect(p.setLayerType(0, LayerTypeId::Pattern));
            openGate(p, 0);
            const int pre = 4096 / block, post = 4096 / block;
            Channel0Tap tap((pre + post) * block);
            p.setChannelTapForTest(&tap);
            {
                Run run(p, block);
                for (int b = 0; b < pre; ++b) run.block();
                expect(mu_tronic::setPatternNoteMode(p, 0, 1));
                for (int b = 0; b < post; ++b) run.block();
            }
            p.setChannelTapForTest(nullptr);
            const int preEnd = pre * block;
            const float steady = maxStep(tap.buf, 1024, preEnd);
            const float across = maxStep(tap.buf, preEnd - 2, preEnd + 2 * block);
            logMessage("block " + juce::String(block) + ": drone step " + juce::String(steady) + ", step across the switch " + juce::String(across));
            expect(across <= 3.0f * steady + 1.0e-4f, "the gate closes by ramp");
            expectEquals(tap.buf.getMagnitude(0, preEnd + 6 * block, block), 0.0f, "and is shut once the ramp is done");
            p.flushPendingAsyncUpdates();
        }
    }

    // Layer presets are mu-Tant's own folder and format; the Pattern's data and Note mode travel with them.
    void presets()
    {
        beginTest("a Pattern preset round trips through a slot, lands in mu-Tant's Voices folder, and a mu-Tant file loads");
        mu_seam_test::TempStorage storage;
        PatternProcessor p;
        expect(p.setLayerType(2, LayerTypeId::Pattern));
        mu_seam_test::setParam(p.apvts, "v2_flt_cut", 2400.0f);
        mu_seam_test::setParam(p.apvts, "v2_o1_semi", 4.0f);
        patternIn(p, 2).gate.patternLengthBars.store(3);
        patternIn(p, 2).gate.addEnvelope({ 2, 4, 0.5f, 0.1f, 0.0f, false });
        expect(mu_tronic::setPatternNoteMode(p, 2, 1));

        p.saveLayerPreset(2, "PatternRT");
        const auto dir = p.typedLayerPresetDir(2);
        const auto file = dir.getChildFile("PatternRT." + p.typedLayerPresetExtension(2));
        expect(file.existsAsFile(), "written into the type's folder");
        expectEquals(dir.getFileName(), juce::String("Voices"));
        expectEquals(dir.getParentDirectory().getFileName(), juce::String("muTant"), "mu-Tant's own folder, not a copy under mu-Tronic");
        expectEquals(p.typedLayerPresetExtension(2), juce::String("muPattern"));
        if (const auto xml = juce::XmlDocument::parse(file))
            expectEquals(xml->getTagName(), juce::String("MuTantVoice"));

        mu_seam_test::TempStorage storage2;
        PatternProcessor q;
        juce::String error;
        q.onLoadError = [&error](const juce::String& m) { error = m; };
        int committed = -1;
        q.onLayerPresetCommitted = [&committed](int s) { committed = s; };
        q.loadLayerPreset(5, file);
        expect(q.getLayerTypeId(5) == LayerTypeId::Pattern, "an Empty slot adopts the file's type");
        expectEquals(error, juce::String());
        expectEquals(committed, 5);
        expectWithinAbsoluteError(getPlain(q.apvts, "v5_flt_cut"), 2400.0f, 2.0f);
        expectEquals(getPlain(q.apvts, "v5_o1_semi"), 4.0f);
        expectEquals(patternIn(q, 5).gate.patternLengthBars.load(), 3, "the gate pattern travels");
        expectEquals(mu_tronic::getPatternNoteMode(q, 5), 1, "and so does the layer's Note mode");

        // Into a filled slot (stopped: applies at once).
        expect(q.setLayerType(7, LayerTypeId::Pattern));
        q.loadLayerPreset(7, file);
        expectWithinAbsoluteError(getPlain(q.apvts, "v7_flt_cut"), 2400.0f, 2.0f);
        expectEquals(patternIn(q, 7).gate.patternLengthBars.load(), 3);

        // What mu-Tant itself writes: the same root tag, rows without a prefix, no Note mode, a wavetable path.
        const auto wav = writeWavetable(storage2.root, "fromtant");
        juce::ValueTree fromTant("MuTantVoice");
        juce::ValueTree row(mu_pp::kRowTag);
        row.setProperty("id", "flt_cut", nullptr);
        row.setProperty("x", 1500.0, nullptr);
        fromTant.appendChild(row, nullptr);
        fromTant.setProperty("o1WtPath", wav.getFullPathName(), nullptr);
        const auto dirQ = q.typedLayerPresetDir(7);
        const auto tantFile = dirQ.getChildFile("FromTant.muPattern");
        dirQ.createDirectory();
        if (auto xml = fromTant.createXml()) xml->writeTo(tantFile);
        q.loadLayerPreset(7, tantFile);
        expectEquals(error, juce::String());
        expectWithinAbsoluteError(getPlain(q.apvts, "v7_flt_cut"), 1500.0f, 2.0f);
        expectEquals(patternIn(q, 7).osc1UserPath, wav.getFullPathName(), "its wavetable path is loaded into the shared bank");
        expectGreaterThan(patternIn(q, 7).osc1UserIndex.load(), 5);
        expectEquals(mu_tronic::getPatternNoteMode(q, 7), 1, "a file without a Note mode leaves the layer's own choice (set by the earlier preset)");

        // preloadStaged warms the bank for a staged tree, so the commit at the boundary reads no file.
        const auto wav2 = writeWavetable(storage2.root, "staged");
        juce::ValueTree staged("MuTantVoice");
        staged.setProperty("o2WtPath", wav2.getFullPathName(), nullptr);
        auto* host = patternIn(q, 7).host.ptr;
        expect(host != nullptr);
        if (host != nullptr)
        {
            patternIn(q, 7).preloadStaged(staged);
            expectGreaterThan(host->resolvePatternWavetable(wav2.getFullPathName()), 5, "the staged table is in the bank before the boundary");
        }

        // The folder's files are listed for a Pattern slot and for an Empty one.
        expectEquals(q.slotPresetFiles(7).size(), 1);
        expectEquals(q.slotPresetFiles(0).size(), 1);
        p.flushPendingAsyncUpdates();
        q.flushPendingAsyncUpdates();
    }

    // Type changes under a running audio thread, with a Pattern held in Note mode: no allocation on the audio thread.
    void typeChangeStress()
    {
        beginTest("two-thread stress with the real Pattern in the mix: audio thread allocation-free");
        constexpr int kChanges = 600;
        constexpr int kBlock = 128;

        mu_seam_test::TempStorage storage;
        PatternProcessor p;
        mu_golden::MockHost host;
        p.setPlayHead(&host);
        p.setRateAndBufferSizeDetails(48000.0, kBlock);
        p.prepareToPlay(48000.0, kBlock);
        expect(p.setLayerType(0, LayerTypeId::Pattern));   // one Pattern that stays, held in Note mode by a key
        expect(mu_tronic::setPatternNoteMode(p, 0, 1));

        {   // the first block after a prepare sizes the host MIDI buffer (one allocation, at start-up): not counted here
            juce::AudioBuffer<float> warm(2, kBlock);
            juce::MidiBuffer warmMidi;
            p.processBlock(warm, warmMidi);
        }
        std::atomic<bool> stop { false };
        std::atomic<int> blocks { 0 }, allocs { 0 }, frees { 0 };
        juce::String firstOffender;   // written by the audio thread, read after it has joined
        mu_alloc::captureBudget = 3;
        std::thread audio([&]
        {
            juce::AudioBuffer<float> buf(2, kBlock);
            juce::MidiBuffer midi;
            while (! stop.load())
            {
                midi.clear();
                if (blocks.load() % 16 == 0) midi.addEvent(juce::MidiMessage::noteOn(1, 48 + blocks.load() % 12, (juce::uint8) 100), 0);
                buf.clear();
                {
                    mu_alloc::Armed armed;
                    p.processBlock(buf, midi);
                    armed.stop();
                    allocs += armed.allocs();
                    frees += armed.frees();
                    if (armed.allocs() > 0 && firstOffender.isEmpty()) firstOffender = armed.firstAllocStack();
                }
                host.timeInSamples += kBlock;
                host.ppq += (double) kBlock * host.bpm / (60.0 * 48000.0);
                blocks.fetch_add(1);
            }
        });
        bool allOk = true;
        for (int i = 0; i < kChanges; ++i)
        {
            const int slot = 1 + i % 7;
            const LayerTypeId cycle[] = { LayerTypeId::Pattern, LayerTypeId::Empty, LayerTypeId::Pattern, LayerTypeId::Arp };
            allOk = p.setLayerType(slot, cycle[(i / 7) % 4]) && allOk;
            if (i % 50 == 0) p.flushPendingAsyncUpdates();
        }
        stop.store(true);
        audio.join();
        p.flushPendingAsyncUpdates();
        p.setPlayHead(nullptr);
        p.releaseResources();
        logMessage("Pattern type-change stress: " + juce::String(kChanges) + " changes, " + juce::String(blocks.load()) + " blocks");
        expect(allOk, "every type change succeeded");
        if (firstOffender.isNotEmpty()) logMessage("first allocation on the audio thread:\n" + firstOffender);
        expectEquals(allocs.load() + frees.load(), 0, "the audio thread allocated or freed");
        expectGreaterThan(blocks.load(), 50, "audio blocks ran during the type changes");
    }
};

static MuTronicPatternTests muTronicPatternTests;
