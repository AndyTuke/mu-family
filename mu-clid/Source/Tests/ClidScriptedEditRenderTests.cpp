// A scripted structural-edit render on the REAL mu-Clid PluginProcessor, headless: three rhythms play
// from a running host transport while rhythms are added, reordered, reset and deleted between blocks.
// The render is deterministic (the host position advances one block per read, the deferred-work poll runs
// once per block), so the same script gives the same samples; set MU_SCRIPTED_EDIT_OUT to a file path and
// the run also writes the raw samples there, to compare one build against another byte for byte.

#include "Plugin/PluginProcessor.h"
#include "Tests/ProcessorSeamHelpers.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace
{
    using namespace mu_seam_test;

    struct ScriptedProcessor : mu_clid::PluginProcessor
    {
        using mu_clid::PluginProcessor::drainDeferredWork;
    };

    // tests/presets, found by walking up from the test executable to the repo root.
    juce::File presetsDir()
    {
        auto dir = juce::File::getSpecialLocation(juce::File::currentExecutableFile).getParentDirectory();
        for (int i = 0; i < 10 && dir.exists(); ++i, dir = dir.getParentDirectory())
            if (dir.getChildFile("tests").getChildFile("presets").isDirectory())
                return dir.getChildFile("tests").getChildFile("presets");
        return {};
    }

    // Writes a short decaying tone as `name` in the processor's Samples folder, so the presets that
    // name it have something to play under the temp storage root (the real library is not touched).
    void writeSample(ScriptedProcessor& p, const juce::String& name, double freq)
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
            b.setSample(0, i, 0.6f * std::exp(-6.0f * (float) i / 22050.0f) * (float) std::sin(juce::MathConstants<double>::twoPi * freq * i / 44100.0));
        w->writeFromAudioSampleBuffer(b, 0, b.getNumSamples());
    }

    // The editor's Add: ProcessorBase::addLayer (the new slot's default rhythm is applied by layerAdded).
    void editAdd(ScriptedProcessor& p)                 { p.addLayer(); }
    void editSwap(ScriptedProcessor& p, int a, int b)  { p.swapLayers(a, b); }
    void editReset(ScriptedProcessor& p, int i)        { p.resetLayer(i); }
    void editRemove(ScriptedProcessor& p, int i)       { p.removeLayer(i); }
}

struct ClidScriptedEditRenderTests : juce::UnitTest
{
    ClidScriptedEditRenderTests() : juce::UnitTest("mu-Clid scripted structural-edit render", "mu-clid-processor") {}

    static std::vector<float> render(const juce::File& presets, bool& ok)
    {
        TempStorage storage;
        ScriptedProcessor p;
        constexpr int kBlock = 512;
        constexpr int kBlocks = 420;
        p.prepareToPlay(48000.0, kBlock);
        RunningPlayHead host(48000.0, kBlock);
        p.setPlayHead(&host);

        p.getSamplesDir().createDirectory();
        writeSample(p, "Hat 01.wav", 5200.0);
        writeSample(p, "Kick 01.wav", 70.0);
        writeSample(p, "Synth C 01.wav", 261.6);

        // Setup: one master preset, two more rhythms from rhythm presets, and distinct strips with a sidechain.
        p.loadPreset(presets.getChildFile("A3_filter.muClid"));
        editAdd(p);
        editAdd(p);
        ok = p.getNumRhythms() == 3;
        ok = ok && p.applyRhythmPreset(presets.getChildFile("T11.muRhythm"), 1);
        ok = ok && p.applyRhythmPreset(presets.getChildFile("T13.muRhythm"), 2);
        setParam(p.apvts, "ch0_pan", -0.6f);
        setParam(p.apvts, "ch1_pan", 0.6f);
        setParam(p.apvts, "ch2_lvl", 0.7f);
        setParam(p.apvts, "ch2_scSrc", 1.0f);   // strip 2 ducks from layer 0
        // MU_SCRIPTED_EDIT_SC=1 makes the ducking audible, to compare how an edit treats the envelope of a moved strip.
        const bool duck = juce::SystemStats::getEnvironmentVariable("MU_SCRIPTED_EDIT_SC", {}).isNotEmpty();
        if (duck)
            setParam(p.apvts, "ch2_scAmt", 1.0f);

        std::vector<float> out;
        out.reserve((size_t) kBlocks * kBlock * 2);
        juce::AudioBuffer<float> buf(2, kBlock);
        juce::MidiBuffer midi;
        for (int b = 0; b < kBlocks; ++b)
        {
            switch (b)
            {
                case  60:
                    editAdd(p);
                    p.applyRhythmPreset(presets.getChildFile("T12.muRhythm"), 3);
                    if (duck)
                    {
                        setParam(p.apvts, "ch3_scSrc", 2.0f);   // strip 3 ducks from layer 1, which later edits move and reset
                        setParam(p.apvts, "ch3_scAmt", 1.0f);
                    }
                    break;
                case 110: editSwap(p, 0, 2); break;
                case 150: editReset(p, 1); setParam(p.apvts, "ch1_pan", 0.6f); break;   // a reset also returns the strip to its defaults
                case 190: p.applyRhythmPreset(presets.getChildFile("T13.muRhythm"), 1); break;   // retires slot 1's engine into a tail
                case 200: editRemove(p, 0); break;
                case 260: editSwap(p, 1, 2); break;
                case 300: editRemove(p, 1); break;
                case 330: editAdd(p); break;
                default: break;
            }
            buf.clear();
            midi.clear();
            p.processBlock(buf, midi);
            p.drainDeferredWork();
            for (int i = 0; i < kBlock; ++i)
            {
                out.push_back(buf.getSample(0, i));
                out.push_back(buf.getSample(1, i));
            }
        }
        p.setPlayHead(nullptr);
        p.releaseResources();
        return out;
    }

    // Per-block record of the strip that the delete moves (slot 2 before the edit, slot 1 after it).
    struct DuckTrace
    {
        std::vector<float> gr;          // the strip's peak gain reduction in each block
        std::vector<float> env;         // its sidechain follower at the end of each block
        float envBeforeEdit = -1.0f;    // the follower in slot 2 just before the delete
        float envAfterEdit  = -1.0f;    // the follower in slot 1 just after it
    };

    // Two strips duck from layer 0; the middle layer is deleted mid-play (unless `remove` is false, the reference),
    // so the second ducked strip lands in the slot of the first (whose follower state is its own, not the strip's).
    // Returns the samples either side of the edit.
    static std::vector<float> renderDuckedMove(const juce::File& presets, DuckTrace* trace = nullptr, bool remove = true)
    {
        TempStorage storage;
        ScriptedProcessor p;
        constexpr int kBlock = 512;
        constexpr int kBlocks = 140;
        p.prepareToPlay(48000.0, kBlock);
        RunningPlayHead host(48000.0, kBlock);
        p.setPlayHead(&host);
        p.getSamplesDir().createDirectory();
        writeSample(p, "Hat 01.wav", 5200.0);
        writeSample(p, "Kick 01.wav", 70.0);
        writeSample(p, "Synth C 01.wav", 261.6);
        p.loadPreset(presets.getChildFile("A3_filter.muClid"));
        editAdd(p);
        editAdd(p);
        p.applyRhythmPreset(presets.getChildFile("T11.muRhythm"), 1);
        p.applyRhythmPreset(presets.getChildFile("T13.muRhythm"), 2);
        for (const char* ch : { "ch1", "ch2" })
        {
            setParam(p.apvts, juce::String(ch) + "_scSrc", 1.0f);   // ducks from layer 0
            setParam(p.apvts, juce::String(ch) + "_scAmt", 1.0f);
        }
        // Strip 1's follower is slow, so the two followers differ and one left behind in slot 1 by the delete shows.
        setParam(p.apvts, "ch1_scAtk", 200.0f);
        setParam(p.apvts, "ch1_scRel", 1500.0f);

        std::vector<float> out;
        juce::AudioBuffer<float> buf(2, kBlock);
        juce::MidiBuffer midi;
        constexpr int kEditBlock = 70;
        for (int b = 0; b < kBlocks; ++b)
        {
            if (remove && b == kEditBlock)
            {
                if (trace != nullptr) trace->envBeforeEdit = p.mixerEngine.getSidechainEnv(2);
                editRemove(p, 1);
                if (trace != nullptr) trace->envAfterEdit = p.mixerEngine.getSidechainEnv(1);
            }
            buf.clear();
            midi.clear();
            p.processBlock(buf, midi);
            p.drainDeferredWork();
            // The moved strip's gain reduction and follower, read from the slot it occupies in this block.
            if (trace != nullptr)
            {
                const int slot = (remove && b >= kEditBlock) ? 1 : 2;
                trace->gr .push_back(p.mixerEngine.sidechainGR[slot].load());
                trace->env.push_back(p.mixerEngine.getSidechainEnv(slot));
            }
            for (int i = 0; i < kBlock; ++i) { out.push_back(buf.getSample(0, i)); out.push_back(buf.getSample(1, i)); }
        }
        p.setPlayHead(nullptr);
        p.releaseResources();
        return out;
    }

    void runTest() override
    {
        if (auto out = juce::SystemStats::getEnvironmentVariable("MU_DUCKED_MOVE_OUT", {}); out.isNotEmpty())
        {
            const auto presets = presetsDir();
            const auto d = renderDuckedMove(presets);
            juce::File f(out);
            f.deleteFile();
            if (auto os = f.createOutputStream())
                os->write(d.data(), d.size() * sizeof(float));
            logMessage("wrote ducked-move render " + f.getFullPathName());
        }

        beginTest("a ducked strip moved by a delete keeps its follower: no jump at the edit");
        {
            const auto presets2 = presetsDir();
            expect(presets2.isDirectory(), "tests/presets found");
            if (presets2.isDirectory())
            {
                // The duck follows the source level, so the gain reduction pumps with layer 0's hits and a
                // block-to-block step alone cannot tell a lost follower from the music. Compare instead with the same
                // render without the delete: the moved strip must carry its follower across the edit and then duck
                // exactly as it would have in its old slot (the source and the strip's own settings are unchanged).
                DuckTrace edited, reference;
                renderDuckedMove(presets2, &edited, true);
                renderDuckedMove(presets2, &reference, false);
                float maxGrDiff = 0.0f, maxEnvDiff = 0.0f, maxNaturalStep = 0.0f;
                const size_t n = std::min(edited.gr.size(), reference.gr.size());
                for (size_t b = 0; b < n; ++b)
                {
                    maxGrDiff  = std::max(maxGrDiff,  std::abs(edited.gr [b] - reference.gr [b]));
                    maxEnvDiff = std::max(maxEnvDiff, std::abs(edited.env[b] - reference.env[b]));
                    if (b > 0 && b >= 68 && b <= 74) maxNaturalStep = std::max(maxNaturalStep, std::abs(reference.gr[b] - reference.gr[b - 1]));
                }
                logMessage("ducked-move follower " + juce::String(edited.envBeforeEdit, 5) + " -> " + juce::String(edited.envAfterEdit, 5)
                           + "; max difference to the unedited render: gain reduction " + juce::String(maxGrDiff, 6)
                           + ", follower " + juce::String(maxEnvDiff, 6)
                           + "; the unedited strip's own largest block step near the edit " + juce::String(maxNaturalStep, 5));
                expect(n == 140 && reference.gr.size() == edited.gr.size(), "both renders traced every block");
                expect(edited.envBeforeEdit > 0.01f, "the strip was ducking when the delete came");
                expectWithinAbsoluteError(edited.envAfterEdit, edited.envBeforeEdit, 1.0e-7f, "the follower moved with its strip");
                expect(maxGrDiff < 1.0e-4f && maxEnvDiff < 1.0e-4f, "the moved strip does not jump when its slot changes");
            }
        }

        beginTest("the scripted add / reorder / reset / delete render is audible, sane and repeatable");
        const auto presets = presetsDir();
        expect(presets.isDirectory(), "tests/presets found");
        if (! presets.isDirectory()) return;

        bool ok1 = false, ok2 = false;
        const auto a = render(presets, ok1);
        const auto b = render(presets, ok2);
        expect(ok1 && ok2, "the setup loaded all three rhythms");
        expect(a == b, "two runs of the script give identical samples");

        bool finite = true;
        float peak = 0.0f;
        for (float s : a) { finite = finite && std::isfinite(s); peak = std::max(peak, std::abs(s)); }
        expect(finite && peak < 100.0f, "the output is finite and bounded");
        expect(peak > 1.0e-3f, "and audible");
        logMessage("scripted render: " + juce::String((int) a.size()) + " samples, peak " + juce::String(peak, 4));

        if (auto out = juce::SystemStats::getEnvironmentVariable("MU_SCRIPTED_EDIT_OUT", {}); out.isNotEmpty())
        {
            juce::File f(out);
            f.deleteFile();
            if (auto os = f.createOutputStream())
                os->write(a.data(), a.size() * sizeof(float));
            logMessage("wrote " + f.getFullPathName());
        }
    }
};

static ClidScriptedEditRenderTests clidScriptedEditRenderTests;

// The clip bank end to end with a running host transport, as the clip strip drives it: edit the steps, store two
// clips, then launch 1, 2, 1 while the audio thread plays. After each boundary commit the rendered hits per bar
// must be that clip's.
struct ClidClipPlayingRenderTests : juce::UnitTest
{
    ClidClipPlayingRenderTests() : juce::UnitTest("mu-Clid clip switch while playing (rendered)", "mu-clid-processor") {}

    void runTest() override
    {
        beginTest("steps edited between two stores: switching clips while the transport runs changes the rendered hits");
        TempStorage storage;
        ScriptedProcessor p;
        constexpr int kBlock = 512;
        p.prepareToPlay(48000.0, kBlock);
        RunningPlayHead host(48000.0, kBlock);
        p.setPlayHead(&host);

        p.getSamplesDir().createDirectory();
        writeSample(p, "Hat 01.wav", 5200.0);
        p.samples.load(0, p.getSamplesDir().getChildFile("Hat 01.wav"));

        juce::AudioBuffer<float> buf(2, kBlock);
        juce::MidiBuffer midi;
        bool prevLoud = false;
        // Renders `blocks` blocks and returns the number of hit onsets in them.
        const auto run = [&](int blocks)
        {
            int onsets = 0;
            for (int b = 0; b < blocks; ++b)
            {
                buf.clear(); midi.clear();
                p.processBlock(buf, midi);
                p.drainDeferredWork();
                const bool loud = buf.getMagnitude(0, kBlock) > 0.01f;
                if (loud && ! prevLoud) ++onsets;
                prevLoud = loud;
            }
            return onsets;
        };
        constexpr int kTwoBars = 190;   // one 16-step bar (2 s at 120 bpm)
        using Type = mu_core::ControlActionType;
        const auto act = [](Type type, int index)
        {
            mu_core::ControlAction a;
            a.type = type; a.layer = 0; a.index = index;
            return a;
        };

        setParam(p.apvts, "r0_stepsA", 16.0f);
        setParam(p.apvts, "r0_aEnvRel", 0.02f);   // short release: hits stay separate
        setParam(p.apvts, "r0_hitsA", 2.0f);
        run(40);
        expect(p.perform(act(Type::StoreClip, 0)));
        setParam(p.apvts, "r0_hitsA", 4.0f);
        run(40);
        expect(p.perform(act(Type::StoreClip, 1)));
        run(200);
        const int base = run(kTwoBars);
        logMessage("live (clip 2) hits in 2 bars: " + juce::String(base));

        int hits[3] {};
        int n = 0;
        for (const int pad : { 0, 1, 0 })
        {
            expect(p.perform(act(Type::LaunchClip, pad)), "launch");
            run(kTwoBars);                        // the boundary passes and the swap commits
            hits[n++] = run(kTwoBars);
            expectEquals((int) getParam(p.apvts, "r0_hitsA"), pad == 0 ? 2 : 4, "APVTS follows the clip");
            expectEquals(p.getRhythm(0).genA.hits, pad == 0 ? 2 : 4, "live rhythm follows the clip");
        }
        logMessage("rendered hits per 2 bars after clip 1, 2, 1: " + juce::String(hits[0]) + ", " + juce::String(hits[1]) + ", " + juce::String(hits[2]));
        expect(hits[0] < hits[1], "clip 2 sounds denser than clip 1");
        expectEquals(hits[0], hits[2], "returning to clip 1 gives its pattern again");
        p.setPlayHead(nullptr);
        p.releaseResources();
    }
};

static ClidClipPlayingRenderTests clidClipPlayingRenderTests;
