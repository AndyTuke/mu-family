// Tests for a batch of audit minors in mu-core: the Layer reset taking its locks, the FX slots' parameter
// hand-off and sizing, the chorus voice handling, NaN containment and the return sidechain's source in the
// mixer, and the processor-owned online-activation worker.

#include "TestProcessorBase.h"
#include "Audio/MixerEngine.h"
#include "Audio/FX/Slots/FXChain.h"
#include "Audio/FX/Slots/EffectSlot.h"
#include "Audio/FX/Send/ChorusEffect.h"
#include "Audio/FX/Send/PhaserEffect.h"

#include <atomic>
#include <cmath>
#include <thread>

namespace
{
    struct ActivationHost : TestProcessorBase
    {
        ActivationHost() : TestProcessorBase(WrapperType::wrapperType_Undefined) {}
        using ProcessorBase::prepareForDestruction;
    };

    // A steady tone and white-ish noise, deterministic.
    float tone(int i) { return 0.8f * std::sin(0.07f * (float) i); }
}

struct AuditMinorsTests : juce::UnitTest
{
    AuditMinorsTests() : juce::UnitTest("Audit minors (locks, FX hand-off, NaN containment, activation worker)", "mu-core") {}

    static constexpr int    kBlock = 256;
    static constexpr double kRate  = 48000.0;

    void runTest() override
    {
        layerResetLocks();
        delaySlotSizing();
        reverbParams();
        chorusVoices();
        phaserStages();
        effectSlotBeforePrepare();
        mixerPrepareZero();
        nonFiniteContainment();
        returnSidechainPreFader();
        activationWorker();
    }

    void layerResetLocks()
    {
        beginTest("Layer::adoptDefaults waits for the modulator and parameter locks");
        {
            Layer live, fresh;
            fresh.voiceParams.insertAlgo = 3;

            auto runWhileHeld = [&](CopyableSpinLock& lock)
            {
                juce::WaitableEvent held;
                std::thread holder([&]
                {
                    mu_core::spinLock(lock);
                    held.signal();
                    juce::Thread::sleep(120);
                    mu_core::spinUnlock(lock);
                });
                held.wait(2000);
                const auto t0 = juce::Time::getMillisecondCounterHiRes();
                live.adoptDefaults(fresh);
                const auto waited = juce::Time::getMillisecondCounterHiRes() - t0;
                holder.join();
                return waited;
            };

            expect(runWhileHeld(live.modLock) > 60.0, "the reset waited for the modulator lock");
            expect(runWhileHeld(live.voiceParamsLock) > 60.0, "and for the parameter lock");
            expect(! live.modLock.v.load() && ! live.voiceParamsLock.v.load(), "it leaves both locks free");
            expectEquals((int) live.voiceParams.insertAlgo, 3);
        }
    }

    void delaySlotSizing()
    {
        beginTest("a long sync delay is not clamped to 4 s at 192 kHz");
        {
            DelaySlot d;
            d.setFeedback(0.0f);
            d.setTimeMode(DelaySlot::TimeMode::Sync);
            d.setHostBpm(60.0);
            d.setTimeDivision(4, false, false);
            d.setTimeCount(8);   // eight quarter notes at 60 BPM = 8 s
            d.prepare(192000.0, 512);   // prepare derives the targets and starts the smoothers on them

            const int total = 192000 * 8 + 4096;
            int  peakAt  = -1;
            float peak   = 0.0f;
            juce::AudioBuffer<float> buf(2, 4096);
            for (int pos = 0; pos < total; pos += 4096)
            {
                buf.clear();
                if (pos == 0) { buf.setSample(0, 0, 1.0f); buf.setSample(1, 0, 1.0f); }
                d.processReturn(buf);
                for (int i = 0; i < 4096; ++i)
                    if (std::abs(buf.getSample(0, i)) > peak) { peak = std::abs(buf.getSample(0, i)); peakAt = pos + i; }
            }
            expect(std::abs(peakAt - 192000 * 8) <= 3, "the echo lands at 8 s (" + juce::String(peakAt) + ")");
        }

        beginTest("delay times set from two threads at once leave a finite, bounded output");
        {
            DelaySlot d;
            d.prepare(kRate, kBlock);
            std::atomic<bool> stop { false };
            std::thread a([&] { while (! stop) { d.setDelayMs(120.0f); d.setSpread(0.3f); } });
            std::thread b([&] { while (! stop) { d.setHostBpm(133.0); d.setTimeDivision(8, true, false); } });
            juce::AudioBuffer<float> buf(2, kBlock);
            bool finite = true;
            for (int blk = 0; blk < 200; ++blk)
            {
                for (int i = 0; i < kBlock; ++i) { buf.setSample(0, i, tone(i)); buf.setSample(1, i, tone(i)); }
                d.processReturn(buf);
                finite = finite && std::isfinite(buf.getMagnitude(0, 0, kBlock));
            }
            stop = true; a.join(); b.join();
            expect(finite);
        }
    }

    void reverbParams()
    {
        beginTest("reverb parameter writes from two threads while the audio runs stay finite");
        {
            ReverbSlot r;
            r.prepare(kRate, kBlock);
            std::atomic<bool> stop { false };
            std::thread a([&] { float v = 0.0f; while (! stop) { r.setParam("size", v); v = v > 0.99f ? 0.0f : v + 0.01f; } });
            std::thread b([&] { float v = 0.5f; while (! stop) { r.setParam("decay", v); v = v > 9.0f ? 0.5f : v + 0.1f; r.setAlgorithm(((int) v) & 3); } });
            juce::AudioBuffer<float> buf(2, kBlock);
            bool finite = true;
            for (int blk = 0; blk < 150; ++blk)
            {
                for (int i = 0; i < kBlock; ++i) { buf.setSample(0, i, tone(i)); buf.setSample(1, i, tone(i)); }
                r.processReturn(buf);
                finite = finite && std::isfinite(buf.getMagnitude(0, 0, kBlock));
            }
            stop = true; a.join(); b.join();
            expect(finite);
        }
    }

    // Runs one chorus over `blocks` blocks of `fill(i)` input, returns the last block's output.
    template <class Fill>
    static std::vector<float> runChorus(ChorusEffect& c, int blocks, Fill&& fill)
    {
        std::vector<float> out;
        juce::AudioBuffer<float> buf(2, kBlock);
        for (int b = 0; b < blocks; ++b)
        {
            for (int i = 0; i < kBlock; ++i)
            {
                const float v = fill(b * kBlock + i);
                buf.setSample(0, i, v);
                buf.setSample(1, i, v);
            }
            juce::dsp::AudioBlock<float> block(buf);
            c.processInner(block);
        }
        for (int i = 0; i < kBlock; ++i) out.push_back(buf.getSample(0, i));
        return out;
    }

    void chorusVoices()
    {
        beginTest("chorus: a voice that becomes active replays no stale audio");
        {
            ChorusEffect c;
            c.setSendMode(true);
            c.prepareInner(kRate, kBlock);
            c.setParam("mix", 100.0f);

            juce::Random rng(7);
            c.setParam("voices", 4.0f);
            runChorus(c, 60, [&](int) { return rng.nextFloat() - 0.5f; });      // noise through all four lines
            c.setParam("voices", 2.0f);
            runChorus(c, 200, [](int) { return 0.0f; });                         // silence through two of them
            c.setParam("voices", 4.0f);
            const auto out = runChorus(c, 1, [](int) { return 0.0f; });          // the others must be silent too
            float peak = 0.0f;
            for (float v : out) peak = juce::jmax(peak, std::abs(v));
            expect(peak < 1.0e-6f, "silence in, silence out after the voice count rises (" + juce::String(peak, 6) + ")");
        }

        beginTest("chorus: the voice count rounds to the nearest whole voice");
        {
            ChorusEffect a, b;
            for (auto* c : { &a, &b }) { c->setSendMode(true); c->prepareInner(kRate, kBlock); c->setParam("mix", 100.0f); }
            a.setParam("voices", 3.9f);
            b.setParam("voices", 4.0f);
            const auto outA = runChorus(a, 20, [](int i) { return tone(i); });
            const auto outB = runChorus(b, 20, [](int i) { return tone(i); });
            expect(outA == outB, "3.9 voices is 4 voices, not 3");
        }
    }

    void phaserStages()
    {
        beginTest("phaser: the stage count rounds to the nearest even number");
        {
            PhaserEffect a, b;
            for (auto* p : { &a, &b }) { p->setSendMode(true); p->prepareInner(kRate, kBlock); p->setParam("mix", 100.0f); }
            a.setParam("stages", 5.9f);
            b.setParam("stages", 6.0f);
            juce::AudioBuffer<float> x(2, kBlock), y(2, kBlock);
            for (int blk = 0; blk < 10; ++blk)
            {
                for (int i = 0; i < kBlock; ++i)
                    for (int c = 0; c < 2; ++c) { x.setSample(c, i, tone(blk * kBlock + i)); y.setSample(c, i, tone(blk * kBlock + i)); }
                juce::dsp::AudioBlock<float> bx(x), by(y);
                a.processInner(bx);
                b.processInner(by);
            }
            bool same = true;
            for (int i = 0; i < kBlock; ++i) same = same && x.getSample(0, i) == y.getSample(0, i);
            expect(same, "5.9 stages is 6 stages");
        }
    }

    void effectSlotBeforePrepare()
    {
        beginTest("an Effect slot used before prepare returns silence instead of crashing");
        {
            EffectSlot e;
            juce::AudioBuffer<float> buf(2, kBlock);
            for (int i = 0; i < kBlock; ++i) { buf.setSample(0, i, 1.0f); buf.setSample(1, i, 1.0f); }
            e.processReturn(buf);
            expectEquals(buf.getMagnitude(0, kBlock), 0.0f);
        }
    }

    void mixerPrepareZero()
    {
        beginTest("the mixer prepares at least one sample per block");
        {
            MixerEngine m;
            m.prepare(kRate, 0);
            expectEquals(m.getPreparedBlockSize(), 1);
        }
    }

    void nonFiniteContainment()
    {
        beginTest("a NaN in the master bus silences that block and the tails recover");
        {
            MixerEngine mixer;
            FXChain fx;
            mixer.prepare(kRate, kBlock);
            fx.prepare(kRate, kBlock);
            juce::AudioBuffer<float> out(2, kBlock);

            int block = 0;
            MixerEngine::RenderChannelFn render = [&block](int, juce::AudioBuffer<float>& buf, int n)
            {
                for (int c = 0; c < buf.getNumChannels(); ++c)
                    for (int i = 0; i < n; ++i)
                        buf.setSample(c, i, block == 3 && i == 10 ? std::numeric_limits<float>::quiet_NaN() : 0.4f * std::sin(0.05f * (float) i));
            };
            mixer.channels[0].sendReverb.store(1.0f);
            mixer.channels[0].sendDelay .store(1.0f);

            bool allFinite = true;
            float lastPeak = 0.0f;
            for (block = 0; block < 12; ++block)
            {
                mixer.processBlock(out, 1, nullptr, fx, kBlock, nullptr, nullptr, nullptr, &render);
                for (int c = 0; c < 2; ++c)
                    for (int i = 0; i < kBlock; ++i)
                        allFinite = allFinite && std::isfinite(out.getSample(c, i));
                if (block == 3)
                    expectEquals(out.getMagnitude(0, kBlock), 0.0f, "the poisoned block is silent");
                lastPeak = out.getMagnitude(0, kBlock);
            }
            expect(allFinite, "no non-finite sample reached the output");
            expectEquals(mixer.nonFiniteBlocks.load(), 1, "exactly one block was contained");
            expect(lastPeak > 0.01f, "the output sounds again after the hit");
        }
    }

    void returnSidechainPreFader()
    {
        beginTest("a return's sidechain hears the channel before its fader");
        {
            MixerEngine mixer;
            FXChain fx;
            mixer.prepare(kRate, kBlock);
            fx.prepare(kRate, kBlock);
            juce::AudioBuffer<float> out(2, kBlock);

            MixerEngine::RenderChannelFn render = [](int, juce::AudioBuffer<float>& buf, int n)
            {
                for (int c = 0; c < buf.getNumChannels(); ++c)
                    for (int i = 0; i < n; ++i)
                        buf.setSample(c, i, 1.0f);
            };
            mixer.channels[0].level.store(0.0f);                 // fader all the way down
            mixer.returns[2].sidechainSource.store(0);
            mixer.returns[2].sidechainAmount.store(1.0f);

            float maxGr = 0.0f;
            for (int b = 0; b < 8; ++b)
            {
                mixer.processBlock(out, 1, nullptr, fx, kBlock, nullptr, nullptr, nullptr, &render);
                maxGr = juce::jmax(maxGr, mixer.returnSidechainGR[2].load());
            }
            expect(maxGr > 0.5f, "the return still ducks from a source whose fader is down (" + juce::String(maxGr, 3) + ")");
        }
    }

    void activationWorker()
    {
        beginTest("online activation: the processor joins the worker and drops the reply");
        {
            ActivationHost p;
            std::atomic<bool> finished { false }, doneCalled { false };
            juce::WaitableEvent started, release;
            p.activateOnlineFn = [&](const juce::String&)
            {
                started.signal();
                release.wait(5000);
                finished = true;
                return mu_core::OnlineActivationOutcome {};
            };
            expect(p.activateOnlineAsync("key", [&](const mu_core::OnlineActivationOutcome&) { doneCalled = true; }));
            started.wait(2000);
            expect(! p.activateOnlineAsync("second", [](const mu_core::OnlineActivationOutcome&) {}),
                   "a second request while one runs is refused");

            std::thread releaser([&] { juce::Thread::sleep(150); release.signal(); });
            p.prepareForDestruction();   // joins the worker: the blocked call has finished when it returns
            releaser.join();
            expect(finished.load(), "teardown waited for the blocked call");

            juce::MessageManager::getInstance()->runDispatchLoopUntil(100);
            expect(! doneCalled.load(), "no reply reaches the callback once the processor is going away");
        }

        beginTest("online activation: the reply arrives on the message thread");
        {
            ActivationHost p;
            bool replied = false, onMessageThread = false;
            p.activateOnlineFn = [](const juce::String&)
            {
                mu_core::OnlineActivationOutcome o;
                o.ok = true;
                return o;
            };
            expect(p.activateOnlineAsync("key", [&](const mu_core::OnlineActivationOutcome& o)
            {
                replied = o.ok;
                onMessageThread = juce::MessageManager::getInstance()->isThisTheMessageThread();
            }));
            juce::MessageManager::getInstance()->runDispatchLoopUntil(400);
            expect(replied, "the outcome reached the callback");
            expect(onMessageThread, "on the message thread");

            ActivationHost q;
            expect(! q.activateOnlineAsync("key", [](const mu_core::OnlineActivationOutcome&) {}), "no function set: refused");
        }
    }
};

static AuditMinorsTests auditMinorsTests;
