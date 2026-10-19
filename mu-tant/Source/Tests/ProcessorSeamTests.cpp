// Tests on the REAL mu-Tant PluginProcessor, built headless (no editor, no host, no audio device):
// construction under a temp storage root, resetLayer against a fresh slot, and the structural
// edits and wavetable import against processBlock running on a worker thread.

#include "Plugin/PluginProcessor.h"
#include "Tests/ProcessorSeamHelpers.h"

#include <atomic>
#include <random>
#include <set>
#include <thread>

// The product class names its editor in a virtual, defined in PluginEditor.cpp, which this headless
// target does not link (see "Headless product processors" in design-plugin-family.md). No test opens
// an editor, so the target supplies this one-line stand-in.
juce::AudioProcessorEditor* mu_tant::PluginProcessor::createEditor() { return nullptr; }

namespace
{
    using namespace mu_seam_test;

    // The real processor with the layer storage and its lock exposed to the tests.
    struct ExposedPluginProcessor : mu_tant::PluginProcessor
    {
        using mu_tant::PluginProcessor::layers;
        using mu_tant::PluginProcessor::numLayers;
        using mu_tant::PluginProcessor::layersLock;
    };

    juce::String vp(int voice, const char* base) { return mu_tant::layerParamId(voice, base); }

    // Opens the gater on a voice so its drone reaches the output while the transport is stopped.
    void bypassGate(juce::AudioProcessorValueTreeState& apvts, int voice)
    {
        setParam(apvts, vp(voice, "gate_bypass"), 1.0f);
    }

    void renderBlock(juce::AudioProcessor& p, juce::AudioBuffer<float>& buf)
    {
        juce::MidiBuffer midi;
        buf.clear();
        p.processBlock(buf, midi);
    }

    float peakOf(const juce::AudioBuffer<float>& b)
    {
        return b.getMagnitude(0, b.getNumSamples());
    }

    // A short wavetable wav on disk (4 frames of 2048 samples); harmonic <= 0 writes a constant (DC) table.
    juce::File writeWavetable(const juce::File& dir, const juce::String& name, float harmonic)
    {
        const int frames = 4, frameSize = 2048;
        juce::AudioBuffer<float> data(1, frames * frameSize);
        for (int f = 0; f < frames; ++f)
            for (int i = 0; i < frameSize; ++i)
            {
                const double ph = juce::MathConstants<double>::twoPi * i / frameSize;
                const float v = harmonic <= 0.0f ? 0.5f   // a constant frame: the output does not depend on phase
                                                 : 0.5f * (float) (std::sin(ph) + (f / 3.0) * std::sin(harmonic * ph) / harmonic);
                data.setSample(0, f * frameSize + i, v);
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

    // Renders `blocks` blocks of `n` samples on `p` and returns every block concatenated.
    juce::AudioBuffer<float> renderRun(juce::AudioProcessor& p, int n, int blocks)
    {
        juce::AudioBuffer<float> out(2, n * blocks), buf(2, n);
        for (int b = 0; b < blocks; ++b)
        {
            renderBlock(p, buf);
            for (int c = 0; c < 2; ++c) out.copyFrom(c, b * n, buf, c, 0, n);
        }
        return out;
    }

    // Puts state in voice 0's filter, noise and insert, then renders so the delay lines and filter
    // memories hold something.
    void seedVoiceState(ExposedPluginProcessor& p, int n, int insertAlgo)
    {
        bypassGate(p.apvts, 0);
        setParam(p.apvts, vp(0, "flt_res"), 0.9f);
        setParam(p.apvts, vp(0, "flt_drv"), 0.7f);
        setParam(p.apvts, vp(0, "drvChar"), (float) insertAlgo);
        for (const char* k : { "insP1", "insP2", "insP3", "insP4" }) setParam(p.apvts, vp(0, k), 0.8f);
        setParam(p.apvts, vp(0, "noise_lvl"), -12.0f);
        juce::AudioBuffer<float> buf(2, n);
        for (int b = 0; b < 12; ++b) renderBlock(p, buf);
    }

    // A constant (DC) oscillator through a resonant filter and an insert. The wavetable is constant
    // because reset keeps oscillator phase running by design, so a rendered slot and a fresh one
    // differ in phase; with a constant table only the filter, insert and smoother memory can show.
    void patchDcThroughFilterAndInsert(ExposedPluginProcessor& p, const juce::File& dcTable, int insertAlgo)
    {
        p.loadUserWavetable(0, 0, dcTable);
        setParam(p.apvts, vp(0, "o1_lvl"), 0.0f);
        setParam(p.apvts, vp(0, "o2_lvl"), -60.0f);
        setParam(p.apvts, vp(0, "noise_lvl"), -60.0f);
        setParam(p.apvts, vp(0, "flt_res"), 0.9f);
        setParam(p.apvts, vp(0, "drvChar"), (float) insertAlgo);
        for (const char* k : { "insP1", "insP2", "insP3", "insP4" }) setParam(p.apvts, vp(0, k), 0.8f);
    }

    struct ResetResult { float maxDiff = 0.0f, peak = 0.0f, snapDiff = 0.0f; };

    // Seeds voice 0 of `seeded` with filter, insert and snapshot state, resets it, patches it and a
    // brand-new processor identically, and compares what both render at block size `n`. Both are slot 0,
    // so the per-slot noise seeds of the Bitcrusher and Vocoder inserts match (a reset slot equals a
    // fresh slot of the SAME index).
    ResetResult resetVersusFresh(ExposedPluginProcessor& seeded, const juce::File& dcTable, int n, int insertAlgo)
    {
        ExposedPluginProcessor fresh;
        seeded.prepareToPlay(48000.0, n);
        fresh.prepareToPlay(48000.0, n);

        seedVoiceState(seeded, n, insertAlgo);
        seeded.resetLayer(0);

        // The reset put the slot's parameters and wavetable choice back to defaults. Give both voices
        // the same patch, then compare what they render.
        for (auto* p : { &seeded, &fresh })
            patchDcThroughFilterAndInsert(*p, dcTable, insertAlgo);

        // Phase 1: gate closed, so only leftover memory (a stale delay line or envelope) can sound;
        // this also lets the parameter smoothers settle (about 15 ms), which a fresh voice has to do
        // too. Phase 2: gate open, compare the audible render.
        auto a = renderRun(seeded, n, juce::jmax(2, 6000 / n));
        auto b = renderRun(fresh, n, juce::jmax(2, 6000 / n));
        for (auto* p : { &seeded, &fresh })
            bypassGate(p->apvts, 0);
        const int blocks = juce::jmax(2, 4096 / n);
        auto a2 = renderRun(seeded, n, blocks);
        auto b2 = renderRun(fresh, n, blocks);
        auto join = [](const juce::AudioBuffer<float>& x, const juce::AudioBuffer<float>& y)
        {
            juce::AudioBuffer<float> out(2, x.getNumSamples() + y.getNumSamples());
            for (int c = 0; c < 2; ++c)
            {
                out.copyFrom(c, 0, x, c, 0, x.getNumSamples());
                out.copyFrom(c, x.getNumSamples(), y, c, 0, y.getNumSamples());
            }
            return out;
        };
        a = join(a, a2);
        b = join(b, b2);

        ResetResult r;
        r.peak = juce::jmax(peakOf(a), peakOf(b));
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < a.getNumSamples(); ++i)
                r.maxDiff = juce::jmax(r.maxDiff, std::abs(a.getSample(c, i) - b.getSample(c, i)));
        for (int s = 0; s < mu_tant::kTantSnapCount; ++s)
            r.snapDiff = juce::jmax(r.snapDiff, std::abs(seeded.getTantSnap(0, s) - fresh.getTantSnap(0, s)));
        return r;
    }

    // A stamp of the real per-user folders, to prove the temp root kept a test out of them.
    juce::int64 realFolderStamp()
    {
        juce::int64 stamp = 0;
        const auto docs = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
                              .getChildFile("TDP").getChildFile("muTant");
        for (const auto& f : docs.findChildFiles(juce::File::findFiles, true))
            stamp += f.getLastModificationTime().toMilliseconds() + f.getSize();
        juce::PropertiesFile::Options o;
        o.applicationName = "muTant"; o.filenameSuffix = "xml"; o.folderName = "TDP"; o.osxLibrarySubFolder = "Application Support";
        const auto settings = o.getDefaultFile();
        if (settings.existsAsFile()) stamp += settings.getLastModificationTime().toMilliseconds() + settings.getSize();
        return stamp;
    }
}

// Note: constructing a mu-Tant takes seconds in Debug (the factory wavetable bank is built), so the
// sweeps below reuse one processor across block sizes wherever the test allows.
struct MuTantProcessorSeamTests : juce::UnitTest
{
    MuTantProcessorSeamTests() : juce::UnitTest("mu-Tant real PluginProcessor", "mu-tant-processor") {}

    // The shared swap-timing parameter exists in every product, with that product's default.
    void swapTimingDefault()
    {
        beginTest("mstr_swap exists and defaults to OnMasterLoop in mu-Tant");
        mu_seam_test::TempStorage storage;
        ExposedPluginProcessor p;
        auto* swap = p.apvts.getParameter("mstr_swap");
        expect(swap != nullptr, "the swap timing parameter is declared");
        if (swap != nullptr)
            expectEquals((int) std::lround(swap->convertFrom0to1(swap->getDefaultValue())), 0);
    }

    void runTest() override
    {
        swapTimingDefault();
        constructsAndRendersUnderTempRoot();
        resetLayerEqualsFresh();
        noiseSeeding();
        resetLayerFromSecondThread();
        resetLayerWithBusyEnvelopeLock();
        layerEditInFlightSkipsRender();
        clipBankSurvivesStateRoundTrip();
        structuralEditsVersusProcessBlock();
        wavetableImportVersusProcessBlock();
    }

    void constructsAndRendersUnderTempRoot()
    {
        beginTest("The real processor constructs under a temp root, prepares and renders a block");
        RealFolderWatch realFolder(realFolderStamp, "mu-Tant.exe");
        {
            TempStorage storage;
            ExposedPluginProcessor p;
            expectEquals(p.getNumLayers(), 1);
            expectEquals(p.numLayers.load(), 1);
            for (int i = 0; i < mu_limits::kMaxLayers; ++i)
                expect(p.layers[(size_t) i] != nullptr, "all eight voice slots exist from construction");
            expectEquals(p.getContentDir().getFullPathName(),
                         storage.root.getChildFile("content").getChildFile("muTant").getFullPathName());

            p.prepareToPlay(48000.0, 512);
            bypassGate(p.apvts, 0);
            juce::AudioBuffer<float> buf(2, 512);
            for (int b = 0; b < 4; ++b) renderBlock(p, buf);
            expect(bufferIsSane(buf), "output is finite");
            expect(peakOf(buf) > 1.0e-4f, "the opened gater lets the drone through");
            p.releaseResources();
        }
        realFolder.expectUnchanged(*this, "nothing under the real muTant settings / content folders changed");
    }

    // Noise seeding through the real processor: MU_DETERMINISTIC_NOISE makes two instances render the same
    // noise (the golden harness relies on it) and a moved / reset slot restart its noise from the slot's seed;
    // without it two instances must not share noise (they would sum correlated in a DAW).
    void noiseSeeding()
    {
        TempStorage storage;
        const auto dc = writeWavetable(storage.root, "dc", 0.0f);
        const int n = 256;

        // A constant oscillator plus full-level noise with the gate open, so noise is the only moving signal.
        auto patch = [&](ExposedPluginProcessor& p)
        {
            patchDcThroughFilterAndInsert(p, dc, 0);
            setParam(p.apvts, vp(0, "noise_lvl"), 0.0f);
            bypassGate(p.apvts, 0);
            p.prepareToPlay(48000.0, n);
        };
        // The last four of `blocks` blocks (earlier ones let the parameter smoothers settle).
        auto tail = [&](ExposedPluginProcessor& p, int blocks)
        {
            auto all = renderRun(p, n, blocks);
            juce::AudioBuffer<float> out(2, 4 * n);
            for (int c = 0; c < 2; ++c) out.copyFrom(c, 0, all, c, (blocks - 4) * n, 4 * n);
            return out;
        };
        auto same = [&](const juce::AudioBuffer<float>& x, const juce::AudioBuffer<float>& y)
        {
            for (int c = 0; c < 2; ++c)
                for (int i = 0; i < x.getNumSamples(); ++i)
                    if (x.getSample(c, i) != y.getSample(c, i)) return false;
            return true;
        };

        beginTest("Deterministic noise mode: two instances render identical noise; a moved slot restarts its noise like a fresh one");
        {
            ScopedEnvVar deterministic("MU_DETERMINISTIC_NOISE", "1");
            ExposedPluginProcessor a, b;
            patch(a); patch(b);
            const auto ta = tail(a, 12), tb = tail(b, 12);
            expect(peakOf(ta) > 1.0e-3f, "the compared render carries noise");
            expect(same(ta, tb), "two instances on the same slot render identical noise");

            ExposedPluginProcessor moved, fresh;
            patch(moved); patch(fresh);
            renderRun(moved, n, 4);                   // advance the noise stream
            moved.layersMoved(0, 0);                  // the slot is reset: its noise restarts from the slot's seed
            const auto tm = tail(moved, 12), tf = tail(fresh, 12);
            expect(same(tm, tf), "a reseeded slot renders the same noise as a fresh one of that index");
        }

        beginTest("Live noise mode: two instances on the same slot do not share noise");
        {
            ScopedEnvVar live("MU_DETERMINISTIC_NOISE", "");
            ExposedPluginProcessor a, b;
            patch(a); patch(b);
            expect(! same(tail(a, 12), tail(b, 12)), "per-instance salts keep instances uncorrelated");
        }
    }

    void resetLayerEqualsFresh()
    {
        TempStorage storage;
        ExposedPluginProcessor seeded;
        const auto dc = writeWavetable(storage.root, "dc", 0.0f);

        beginTest("resetLayer leaves voice 0 rendering exactly like a fresh voice, across block sizes");
        {
            int count = 0;
            const int* sizes = blockSizes(count);
            for (int k = 0; k < count; k += 2)   // 1, 64, 1024: tiny, typical, large (a fresh processor costs seconds)
            {
                const auto r = resetVersusFresh(seeded, dc, sizes[k], 7);   // 7: compressor, which carries an envelope
                const auto where = "block size " + juce::String(sizes[k]);
                expectEquals(r.maxDiff, 0.0f, where + ": reset slot renders differently from a fresh one");
                expectEquals(r.snapDiff, 0.0f, where + ": live-arc snapshots differ from a fresh slot");
                expect(r.peak > 1.0e-4f, where + ": the compared render is not silent");
            }
        }

        beginTest("resetLayer leaves voice 0 rendering exactly like a fresh voice, for each insert algorithm");
        {
            // A constant input folds to nothing, so there is no signal to compare.
            const std::set<int> silentOnConstantInput { 3 };

            for (int algo = 0; algo < 14; ++algo)
            {
                const auto r = resetVersusFresh(seeded, dc, 512, algo);
                const auto where = "insert algo " + juce::String(algo);
                if (silentOnConstantInput.count(algo) == 0)
                    expect(r.peak > 1.0e-4f, where + ": the compared render is not silent");
                expectEquals(r.maxDiff, 0.0f, where + ": reset slot renders differently from a fresh one");
            }
        }
    }

    void resetLayerFromSecondThread()
    {
        beginTest("resetLayer on the message thread while processBlock runs on a worker, across block sizes");
        TempStorage storage;
        ExposedPluginProcessor p;
        int count = 0;
        const int* sizes = blockSizes(count);
        for (int k = 0; k < count; ++k)
        {
            const int n = sizes[k];
            p.prepareToPlay(48000.0, n);
            seedVoiceState(p, n, 3);

            BlockRenderer renderer(p, n);
            renderer.start();
            expect(renderer.waitForAudible(), "the worker is rendering before the edits start");
            for (int i = 0; i < 40; ++i)
            {
                p.resetLayer(0);
                bypassGate(p.apvts, 0);
                setParam(p.apvts, vp(0, "flt_res"), 0.9f);
            }
            expect(renderer.waitForBlocks(2) && renderer.audibleBlocksRendered() > 0, "the audio thread kept rendering during the resets");
            renderer.stop();
            expect(! renderer.sawInsaneOutput(), "block size " + juce::String(n) + ": output stayed finite and bounded");
            expectEquals(p.numLayers.load(), 1);
        }
    }

    void resetLayerWithBusyEnvelopeLock()
    {
        beginTest("resetLayer empties the envelopes even while a helper thread holds the gate's edit lock");
        TempStorage storage;
        ExposedPluginProcessor p;
        p.prepareToPlay(48000.0, 256);
        auto& pat = p.getPattern(0);
        pat.gate.addEnvelope({ 0, 4, 0.0f, 0.0f, 0.0f, false });
        pat.filterGate.addEnvelope({ 0, 4, 0.0f, 0.0f, 0.0f, false });

        std::atomic<bool> held { false }, release { false };
        std::thread editor([&]
        {
            while (! mu_core::spinLockFor(pat.gate.editLock, 1000)) {}
            held.store(true);
            while (! release.load()) std::this_thread::yield();
            mu_core::spinUnlock(pat.gate.editLock);
        });
        while (! held.load()) std::this_thread::yield();
        p.resetLayer(0);
        release.store(true);
        editor.join();

        expect(pat.gate.envelopes.empty() && pat.filterGate.envelopes.empty(), "the reset left stale envelopes behind");
        expect(! pat.gate.hasEnvelopes.load(), "the lock-free flag must be cleared");
    }

    void layerEditInFlightSkipsRender()
    {
        beginTest("While a structural edit is in flight the block is skipped (silent), and resumes after");
        TempStorage storage;
        ExposedPluginProcessor p;
        p.prepareToPlay(48000.0, 256);
        bypassGate(p.apvts, 0);
        juce::AudioBuffer<float> buf(2, 256);
        for (int b = 0; b < 3; ++b) renderBlock(p, buf);
        expect(peakOf(buf) > 1.0e-4f, "audible before");

        p.layerEditInFlight.store(true);
        renderBlock(p, buf);
        expectEquals(peakOf(buf), 0.0f, "a block rendered while the edit is in flight is silent");
        p.layerEditInFlight.store(false);
        for (int b = 0; b < 3; ++b) renderBlock(p, buf);
        expect(peakOf(buf) > 1.0e-4f, "audible again after the edit finishes");

        p.addLayer();
        expect(! p.layerEditInFlight.load(), "a completed structural edit clears the flag");
    }

    void clipBankSurvivesStateRoundTrip()
    {
        beginTest("a host session carries each voice's clip bank and playing clip; reset and remove move or empty it");
        TempStorage storage;
        ExposedPluginProcessor p;
        p.addLayer();
        juce::ValueTree payload("MuTantVoice");
        payload.setProperty("tag", 3, nullptr);
        p.layers[0]->clips->store(1, payload, "swell");
        p.layers[1]->clips->store(4, payload);
        p.layers[1]->activeClip = 4;

        juce::MemoryBlock block;
        p.getStateInformation(block);

        ExposedPluginProcessor q;
        q.layers[0]->clips->store(6, payload);   // stale data the load must replace
        q.setStateInformation(block.getData(), (int) block.getSize());
        expect(q.layers[0]->clips->get(1).isEquivalentTo(payload));
        expectEquals(q.layers[0]->clips->getName(1), juce::String("swell"));
        expect(! q.layers[0]->clips->isOccupied(6), "the stale clip is gone");
        expectEquals(q.layers[1]->activeClip, 4);

        expect(q.removeLayer(0));
        expect(q.layers[0]->clips->isOccupied(4), "voice 1's bank moved down with it");
        q.resetLayer(0);
        expect(q.layers[0]->clips->isEmpty() && q.layers[0]->activeClip == -1, "reset empties the bank");
    }

    // Layer invariants that must hold whenever the message thread is not mid-edit.
    void expectLayerInvariants(ExposedPluginProcessor& p, const juce::String& where)
    {
        const int n = p.numLayers.load();
        expect(n >= 1 && n <= mu_limits::kMaxLayers, where + ": count in range");
        std::set<const void*> seen;
        for (int i = 0; i < mu_limits::kMaxLayers; ++i)
        {
            expect(p.layers[(size_t) i] != nullptr, where + ": slot " + juce::String(i) + " is filled");
            seen.insert(p.layers[(size_t) i].get());
        }
        expectEquals((int) seen.size(), mu_limits::kMaxLayers, where + ": no layer appears twice");
    }

    void structuralEditsVersusProcessBlock()
    {
        TempStorage storage;
        ExposedPluginProcessor p;
        runStructuralEditStress(*this, p,
            [&](int layer) { bypassGate(p.apvts, layer); },
            [&](const juce::String& where) { expectLayerInvariants(p, where); });
    }

    void wavetableImportVersusProcessBlock()
    {
        beginTest("loadUserWavetable / clearUserWavetable vs processBlock on a worker, across block sizes");
        TempStorage storage;
        ExposedPluginProcessor p;
        juce::Array<juce::File> tables;
        for (int t = 0; t < 8; ++t)
            tables.add(writeWavetable(storage.root, "wt" + juce::String(t), 2.0f + (float) t));
        for (const auto& f : tables) expect(f.existsAsFile(), "test wavetable written");

        int count = 0;
        const int* sizes = blockSizes(count);
        for (int k = 0; k < count; ++k)
        {
            const int n = sizes[k];
            p.prepareToPlay(48000.0, n);
            bypassGate(p.apvts, 0);

            BlockRenderer renderer(p, n);
            renderer.start();
            expect(renderer.waitForAudible(), "the worker is rendering before the edits start");

            int firstIndex[2] = { -1, -1 };
            for (int pass = 0; pass < 3; ++pass)
                for (int t = 0; t < tables.size(); ++t)
                {
                    const int osc = t % 2;
                    p.loadUserWavetable(0, osc, tables[t]);
                    expectEquals(p.userWavetablePath(0, osc), tables[t].getFullPathName());
                    const int idx = osc == 0 ? p.getPattern(0).osc1UserIndex.load() : p.getPattern(0).osc2UserIndex.load();
                    expect(idx >= 0, "the imported table has a bank index");
                    if (pass == 0 && t < 2) firstIndex[t] = idx;
                    if (pass > 0 && t < 2) expectEquals(idx, firstIndex[t], "re-importing the same file reuses its bank slot");
                    if (t % 3 == 2) { p.clearUserWavetable(0, osc); expectEquals(p.userWavetablePath(0, osc), juce::String()); }
                }

            expect(renderer.waitForBlocks(2) && renderer.audibleBlocksRendered() > 0, "the audio thread was not stalled by the imports");
            renderer.stop();
            expect(! renderer.sawInsaneOutput(), "block size " + juce::String(n) + ": output stayed finite and bounded");
        }
    }
};

static MuTantProcessorSeamTests muTantProcessorSeamTests;

// The layout word the shared layer driver reads follows every count store of the real processor (E1).
struct MuTantLayoutWordTests : juce::UnitTest
{
    MuTantLayoutWordTests() : juce::UnitTest("mu-Tant layout word follows the live count", "mu-tant-processor") {}
    void runTest() override { mu_seam_test::checkLayoutWordFollowsCount<mu_tant::PluginProcessor>(*this); }
};
static MuTantLayoutWordTests sMuTantLayoutWordTests;
