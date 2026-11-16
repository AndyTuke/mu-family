#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>
#include "Audio/FX/Algorithms/ChorusFx.h"
#include "Audio/FX/Algorithms/FlangerFx.h"
#include "Audio/FX/Algorithms/FxFactory.h"
#include "Audio/FX/Algorithms/PhaserFx.h"
#include <cmath>
#include <random>

// The shared Chorus / Flanger / Phaser algorithms (one class per effect, one DSP core, used by the send Effect slot and the
// per-voice insert): the role flags, each adapter against its bare core, the chorus insert's hidden values, finite output at
// every rate, block size and channel count in both roles, and reset equal to a fresh object.
struct FxMergeTests : juce::UnitTest
{
    FxMergeTests() : juce::UnitTest("FX merge: Chorus, Flanger, Phaser", "mu-core") {}

    static constexpr int kBlock = 256;

    static std::vector<float> noise(int n, unsigned seed)
    {
        std::mt19937 rng(seed);
        std::uniform_real_distribution<float> d(-0.8f, 0.8f);
        std::vector<float> v((size_t) n);
        for (auto& x : v) x = d(rng);
        return v;
    }

    static VoiceParams makeInsert(int algo, float s0, float s1, float s2, float s3)
    {
        VoiceParams p;
        p.insertAlgo = algo;
        p.insertParam[0] = s0; p.insertParam[1] = s1; p.insertParam[2] = s2; p.insertParam[3] = s3;
        return p;
    }

    // Runs `in` (both channels the same) through an insert in blocks of `block`; returns the left channel.
    template <class Fx>
    static std::vector<float> runInsert(Fx& fx, const VoiceParams& p, const std::vector<float>& in, int nCh = 2, int block = kBlock)
    {
        std::vector<float> out;
        juce::AudioBuffer<float> buf(2, block);
        float gr = 0.0f;
        for (size_t pos = 0; pos < in.size(); pos += (size_t) block)
        {
            const int n = (int) juce::jmin((size_t) block, in.size() - pos);
            for (int i = 0; i < n; ++i) for (int c = 0; c < 2; ++c) buf.setSample(c, i, in[pos + (size_t) i]);
            fx.process(buf, n, nCh, p, gr);
            for (int i = 0; i < n; ++i) out.push_back(buf.getSample(0, i));
        }
        return out;
    }

    // Runs `in` through the send side in blocks; the algorithm must already be in send mode.
    template <class Fx>
    static std::vector<float> runSend(Fx& fx, const std::vector<float>& in, int nCh = 2, int block = kBlock)
    {
        std::vector<float> out;
        juce::AudioBuffer<float> buf(nCh, block);
        for (size_t pos = 0; pos < in.size(); pos += (size_t) block)
        {
            const int n = (int) juce::jmin((size_t) block, in.size() - pos);
            for (int i = 0; i < n; ++i) for (int c = 0; c < nCh; ++c) buf.setSample(c, i, in[pos + (size_t) i]);
            juce::dsp::AudioBlock<float> ab(buf);
            auto sub = ab.getSubBlock(0, (size_t) n);
            fx.processInner(sub);
            for (int i = 0; i < n; ++i) out.push_back(buf.getSample(0, i));
        }
        return out;
    }

    static float maxDiff(const std::vector<float>& a, const std::vector<float>& b)
    {
        float d = a.size() == b.size() ? 0.0f : 1.0e9f;
        for (size_t i = 0; i < a.size() && i < b.size(); ++i) d = juce::jmax(d, std::abs(a[i] - b[i]));
        return d;
    }

    static bool finiteAndBounded(const std::vector<float>& v, float bound = 10.0f)
    {
        for (float x : v) if (! std::isfinite(x) || std::abs(x) > bound) return false;
        return true;
    }

    // The core fed the same input, with the parameters the insert adapter derives from the knobs.
    static std::vector<float> chorusCore(const VoiceParams& p, const std::vector<float>& in, double sr)
    {
        mu_audio::ChorusCore core;
        core.prepare(sr);
        mu_audio::ChorusCore::Params q;
        q.rateHz = juce::jlimit(0.05f, 8.0f, insertSlot(p, 0));
        q.depth = juce::jlimit(0.0f, 1.0f, insertSlot(p, 1) * 0.01f);
        q.centreMs = juce::jlimit(5.0f, 30.0f, insertSlot(p, 2));
        q.swingMs = 0.4f * q.centreMs;
        q.voices = 3; q.spread = 0.5f;
        core.setParams(q);
        std::vector<float> out;
        for (float x : in) { float l, r; core.tick(x, x, l, r); out.push_back(l); }
        return out;
    }

    void runTest() override
    {
        constexpr double sr = 48000.0;
        const auto in = noise(48000, 11);

        beginTest("role flags: all three are Both, the factories accept them, the registry rows are theirs");
        {
            static_assert(mu_audio::ChorusFx::kUse == mu_audio::FxUse::Both, "chorus is Both");
            static_assert(mu_audio::FlangerFx::kUse == mu_audio::FxUse::Both, "flanger is Both");
            static_assert(mu_audio::PhaserFx::kUse == mu_audio::FxUse::Both, "phaser is Both");
            static_assert(mu_audio::supports(mu_audio::FxUse::Both, mu_audio::FxUse::Send), "Both serves the send");
            static_assert(! mu_audio::supports(mu_audio::FxUse::Insert, mu_audio::FxUse::Send), "Insert does not serve the send");
            expect(dynamic_cast<mu_audio::ChorusFx*>(mu_audio::makeSend<mu_audio::ChorusFx>().get()) != nullptr);
            expect(dynamic_cast<mu_audio::FlangerFx*>(mu_audio::makeInsert<mu_audio::FlangerFx>().get()) != nullptr);
            expectEquals(mu_audio::ChorusFx().getDef().id, juce::String(FXAlgorithmRegistry::effectAlgorithms()[0].id));
            expectEquals(mu_audio::FlangerFx().getDef().id, juce::String(FXAlgorithmRegistry::effectAlgorithms()[1].id));
            expectEquals(mu_audio::PhaserFx().getDef().id, juce::String(FXAlgorithmRegistry::effectAlgorithms()[2].id));
        }

        beginTest("the insert at Mix 100 % is the bare core with the mapped parameters (chorus, flanger, phaser)");
        {
            const auto pc = makeInsert(17, 0.5f, 0.6f, 0.4f, 1.0f);
            mu_audio::ChorusFx c;
            c.prepare(sr, kBlock);
            expect(maxDiff(runInsert(c, pc, in), chorusCore(pc, in, sr)) < 1.0e-6f, "chorus insert equals ChorusCore");

            const auto pf = makeInsert(18, 0.4f, 0.7f, 0.8f, 1.0f);
            mu_audio::FlangerFx f;
            f.prepare(sr, kBlock);
            mu_audio::FlangerCore fc;
            fc.prepare(sr);
            mu_audio::FlangerCore::Params fq;
            fq.rateHz = juce::jlimit(0.05f, 5.0f, insertSlot(pf, 0));
            fq.depth = juce::jlimit(0.0f, 1.0f, insertSlot(pf, 1) * 0.01f);
            fq.feedback = juce::jlimit(-0.95f, 0.95f, insertSlot(pf, 2) * 0.01f);
            fq.stereoOffset = 0.25f;
            fc.setParams(fq);
            std::vector<float> ref;
            for (float x : in) { float l, r; fc.tick(x, x, l, r); ref.push_back(l); }
            expect(maxDiff(runInsert(f, pf, in), ref) < 1.0e-6f, "flanger insert equals FlangerCore");

            const auto pp = makeInsert(19, 0.3f, 0.5f, 0.6f, 1.0f);
            mu_audio::PhaserFx ph;
            ph.prepare(sr, kBlock);
            mu_audio::PhaserCore pc2;
            pc2.prepare(sr);
            mu_audio::PhaserCore::Params pq;
            pq.rateHz = juce::jlimit(0.05f, 5.0f, insertSlot(pp, 0));
            pq.depth = juce::jlimit(0.0f, 1.0f, insertSlot(pp, 1) * 0.01f);
            pq.stages = 6;
            pq.feedback = juce::jlimit(0.0f, 0.9f, insertSlot(pp, 2) * 0.01f);
            pq.stereoOffset = 0.25f;
            pc2.setParams(pq);
            std::vector<float> pref;
            for (float x : in) { float l, r; pc2.tick(x, x, l, r); pref.push_back(0.5f * (x + l)); }   // the notch blend at Mix 100 %
            expect(maxDiff(runInsert(ph, pp, in), pref) < 1.0e-6f, "phaser insert equals PhaserCore with the notch blend");
        }

        beginTest("the send side is wet-only and equals the bare core with the mapped parameters");
        {
            mu_audio::ChorusFx c;
            c.setSendMode(true);
            c.prepareInner(sr, kBlock);
            c.setParam("rate", 1.5f); c.setParam("depth", 60.0f); c.setParam("voices", 4.0f); c.setParam("spread", 70.0f);
            mu_audio::ChorusCore core;
            core.prepare(sr);
            mu_audio::ChorusCore::Params q;
            q.rateHz = 1.5f; q.depth = 0.6f; q.centreMs = 30.0f; q.swingMs = 20.0f; q.voices = 4; q.spread = 0.7f;
            core.setParams(q);
            std::vector<float> ref;
            for (float x : in) { float l, r; core.tick(x, x, l, r); ref.push_back(l); }
            expect(maxDiff(runSend(c, in), ref) < 1.0e-6f, "chorus send equals ChorusCore");
        }

        beginTest("the flanger and phaser sends are wet-only and equal the bare core with no stereo offset");
        {
            mu_audio::FlangerFx f;
            f.setSendMode(true);
            f.prepareInner(sr, kBlock);
            f.setParam("rate", 1.5f); f.setParam("depth", 60.0f); f.setParam("feedback", 50.0f);
            mu_audio::FlangerCore fc;
            fc.prepare(sr);
            mu_audio::FlangerCore::Params fq;
            fq.rateHz = 1.5f; fq.depth = 0.6f; fq.feedback = 0.5f * 0.95f; fq.stereoOffset = 0.0f;
            fc.setParams(fq);
            std::vector<float> fref;
            for (float x : in) { float l, r; fc.tick(x, x, l, r); fref.push_back(l); }
            expect(maxDiff(runSend(f, in), fref) < 1.0e-6f, "flanger send equals FlangerCore");

            mu_audio::PhaserFx p;
            p.setSendMode(true);
            p.prepareInner(sr, kBlock);
            p.setParam("rate", 1.5f); p.setParam("depth", 60.0f); p.setParam("stages", 8.0f); p.setParam("feedback", 50.0f);
            mu_audio::PhaserCore pc;
            pc.prepare(sr);
            mu_audio::PhaserCore::Params pq;
            pq.rateHz = 1.5f; pq.depth = 0.6f; pq.stages = 8; pq.feedback = 0.5f; pq.stereoOffset = 0.0f;
            pc.setParams(pq);
            std::vector<float> pref;
            for (float x : in) { float l, r; pc.tick(x, x, l, r); pref.push_back(l); }
            expect(maxDiff(runSend(p, in), pref) < 1.0e-6f, "phaser send equals PhaserCore");
        }

        beginTest("the chorus insert's hidden values are three voices and spread 0.5");
        {
            static_assert(mu_audio::ChorusFx::kInsertVoices == 3, "three voices");
            static_assert(mu_audio::ChorusFx::kInsertSpread == 0.5f, "spread 0.5");
            const auto p = makeInsert(17, 0.5f, 1.0f, 0.5f, 1.0f);
            mu_audio::ChorusFx c;
            c.prepare(sr, kBlock);
            const auto three = runInsert(c, p, in);
            mu_audio::ChorusCore core;
            core.prepare(sr);
            mu_audio::ChorusCore::Params q;
            q.rateHz = juce::jlimit(0.05f, 8.0f, insertSlot(p, 0));
            q.depth = 1.0f;
            q.centreMs = juce::jlimit(5.0f, 30.0f, insertSlot(p, 2));
            q.swingMs = 0.4f * q.centreMs;
            q.voices = 2; q.spread = 0.5f;
            core.setParams(q);
            std::vector<float> two;
            for (float x : in) { float l, r; core.tick(x, x, l, r); two.push_back(l); }
            expect(maxDiff(three, two) > 1.0e-3f, "the insert is not a two-voice chorus");
        }

        beginTest("finite and bounded at every rate, block size and channel count, in both roles");
        {
            for (double rate : { 44100.0, 96000.0, 192000.0 })
                for (int block : { 1, 7, 64, 2048 })
                    for (int nCh : { 1, 2 })
                    {
                        const auto x = noise(6000, 5);
                        const auto pc = makeInsert(17, 1.0f, 1.0f, 1.0f, 1.0f);
                        const auto pf = makeInsert(18, 1.0f, 1.0f, 1.0f, 1.0f);
                        const auto pp = makeInsert(19, 1.0f, 1.0f, 1.0f, 1.0f);
                        mu_audio::ChorusFx c; mu_audio::FlangerFx f; mu_audio::PhaserFx ph;
                        c.prepare(rate, block); f.prepare(rate, block); ph.prepare(rate, block);
                        expect(finiteAndBounded(runInsert(c, pc, x, nCh, block)), "chorus insert");
                        expect(finiteAndBounded(runInsert(f, pf, x, nCh, block)), "flanger insert");
                        expect(finiteAndBounded(runInsert(ph, pp, x, nCh, block)), "phaser insert");

                        mu_audio::ChorusFx sc; mu_audio::FlangerFx sf; mu_audio::PhaserFx sp;
                        for (EffectAlgorithmBase* a : std::initializer_list<EffectAlgorithmBase*>{ &sc, &sf, &sp })
                        {
                            a->setSendMode(true);
                            a->prepareInner(rate, block);
                            a->setParam("depth", 100.0f); a->setParam("feedback", 100.0f); a->setParam("rate", 8.0f);
                            a->setParam("voices", 4.0f); a->setParam("stages", 12.0f);
                        }
                        expect(finiteAndBounded(runSend(sc, x, nCh, block)), "chorus send");
                        expect(finiteAndBounded(runSend(sf, x, nCh, block)), "flanger send");
                        expect(finiteAndBounded(runSend(sp, x, nCh, block)), "phaser send");
                    }
        }

        beginTest("reset gives the output of a fresh object, bit for bit, in both roles");
        {
            const auto garbage = noise(9000, 99);
            const auto pc = makeInsert(17, 0.4f, 0.5f, 0.6f, 0.7f);
            const auto pf = makeInsert(18, 0.4f, 0.5f, 0.6f, 0.7f);
            const auto pp = makeInsert(19, 0.4f, 0.5f, 0.6f, 0.7f);
            auto checkInsert = [&](auto& fresh, auto& used, const VoiceParams& p, const char* name)
            {
                fresh.prepare(sr, kBlock);
                used.prepare(sr, kBlock);
                runInsert(used, p, garbage);
                used.reset();
                expect(runInsert(fresh, p, in) == runInsert(used, p, in), juce::String(name) + " insert reset");
            };
            { mu_audio::ChorusFx a, b; checkInsert(a, b, pc, "chorus"); }
            { mu_audio::FlangerFx a, b; checkInsert(a, b, pf, "flanger"); }
            { mu_audio::PhaserFx a, b; checkInsert(a, b, pp, "phaser"); }

            auto checkSend = [&](auto& fresh, auto& used, const char* name)
            {
                for (auto* a : { static_cast<EffectAlgorithmBase*>(&fresh), static_cast<EffectAlgorithmBase*>(&used) })
                {
                    a->setSendMode(true);
                    a->prepareInner(sr, kBlock);
                    a->setParam("rate", 2.0f); a->setParam("depth", 70.0f); a->setParam("feedback", 60.0f);
                }
                runSend(used, garbage);
                used.reset();
                expect(runSend(fresh, in) == runSend(used, in), juce::String(name) + " send reset");
            };
            { mu_audio::ChorusFx a, b; checkSend(a, b, "chorus"); }
            { mu_audio::FlangerFx a, b; checkSend(a, b, "flanger"); }
            { mu_audio::PhaserFx a, b; checkSend(a, b, "phaser"); }
        }
    }
};

static FxMergeTests fxMergeTests;
