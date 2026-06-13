// Send-FX DSP smoke tests (ported from mu-clid for parity).
//
// Verifies all three shared mu-core send-FX slots run without crash or NaN/Inf
// when fed a stereo impulse at representative settings — the same FX rack
// mu-tant's MixerOverlay drives. Not golden-master; exact DSP output isn't asserted.
//   - EffectSlot: all algorithms + Echo processReturn
//   - DelaySlot:  Free + Sync modes + processReturn + multi-block feedback
//   - ReverbSlot: all 4 algorithms + processReturn + multi-block

#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>
#include "Audio/FX/Slots/EffectSlot.h"
#include "Audio/FX/Slots/DelaySlot.h"
#include "Audio/FX/Slots/ReverbSlot.h"

class MuTantSendFXSmokeTest : public juce::UnitTest
{
public:
    MuTantSendFXSmokeTest() : juce::UnitTest ("Send-FX DSP smoke", "DSP") {}

    static bool hasNaN (const juce::AudioBuffer<float>& buf, int nCh, int ns)
    {
        for (int ch = 0; ch < nCh; ++ch)
        {
            const float* d = buf.getReadPointer(ch);
            for (int i = 0; i < ns; ++i)
                if (!std::isfinite(d[i])) return true;
        }
        return false;
    }

    static juce::AudioBuffer<float> impulseBuffer (int nCh, int ns)
    {
        juce::AudioBuffer<float> buf(nCh, ns);
        buf.clear();
        buf.setSample(0, 0, 0.5f);
        if (nCh > 1) buf.setSample(1, 0, 0.5f);
        return buf;
    }

    void runTest() override
    {
        constexpr double kSR = 48000.0;
        constexpr int    kNs = 512;

        const char* effectNames[] = { "Chorus", "Flanger", "Phaser", "Echo" };
        for (int algo = 0; algo < EffectSlot::kNumEffectAlgos; ++algo)
        {
            beginTest (juce::String("EffectSlot algo ") + juce::String(algo)
                       + ": " + effectNames[algo] + " — no crash, no NaN");
            {
                EffectSlot slot;
                slot.setAlgorithm(algo);
                slot.prepare(kSR, kNs);
                auto buf = impulseBuffer(2, kNs);
                slot.process(buf);
                expect (!hasNaN(buf, 2, kNs), juce::String(effectNames[algo]) + " produced NaN/Inf");
            }
        }

        beginTest ("EffectSlot Echo: processReturn — no crash, no NaN");
        {
            EffectSlot slot;
            slot.setAlgorithm(EffectSlot::kEchoAlgoIndex);
            slot.prepare(kSR, kNs);
            auto buf = impulseBuffer(2, kNs);
            slot.processReturn(buf);
            expect (!hasNaN(buf, 2, kNs), "Echo processReturn produced NaN/Inf");
        }

        beginTest ("DelaySlot: Free mode (250 ms) — no crash, no NaN");
        {
            DelaySlot slot;
            slot.setTimeMode(DelaySlot::TimeMode::Free);
            slot.setDelayMs(250.0f);
            slot.setFeedback(0.4f);
            slot.prepare(kSR, kNs);
            auto buf = impulseBuffer(2, kNs);
            slot.process(buf);
            expect (!hasNaN(buf, 2, kNs), "DelaySlot Free produced NaN/Inf");
        }

        beginTest ("DelaySlot: Sync mode (quarter @ 120 BPM) — no crash, no NaN");
        {
            DelaySlot slot;
            slot.setHostBpm(120.0);
            slot.setTimeMode(DelaySlot::TimeMode::Sync);
            slot.setTimeDivision(4, false, false);
            slot.setFeedback(0.3f);
            slot.setSpread(0.3f);
            slot.prepare(kSR, kNs);
            auto buf = impulseBuffer(2, kNs);
            slot.process(buf);
            expect (!hasNaN(buf, 2, kNs), "DelaySlot Sync produced NaN/Inf");
        }

        beginTest ("DelaySlot: processReturn — no crash, no NaN");
        {
            DelaySlot slot;
            slot.setDelayMs(100.0f);
            slot.prepare(kSR, kNs);
            auto buf = impulseBuffer(2, kNs);
            slot.processReturn(buf);
            expect (!hasNaN(buf, 2, kNs), "DelaySlot processReturn produced NaN/Inf");
        }

        const char* reverbNames[] = { "Room", "Hall", "Plate", "Spring" };
        for (int algo = 0; algo < 4; ++algo)
        {
            beginTest (juce::String("ReverbSlot algo ") + juce::String(algo)
                       + ": " + reverbNames[algo] + " — no crash, no NaN");
            {
                ReverbSlot slot;
                slot.setAlgorithm(algo);
                slot.prepare(kSR, kNs);
                auto buf = impulseBuffer(2, kNs);
                slot.process(buf);
                expect (!hasNaN(buf, 2, kNs), juce::String(reverbNames[algo]) + " produced NaN/Inf");
            }
        }

        beginTest ("ReverbSlot: processReturn — no crash, no NaN");
        {
            ReverbSlot slot;
            slot.prepare(kSR, kNs);
            auto buf = impulseBuffer(2, kNs);
            slot.processReturn(buf);
            expect (!hasNaN(buf, 2, kNs), "ReverbSlot processReturn produced NaN/Inf");
        }

        beginTest ("DelaySlot: 8 blocks of noise — feedback stays finite");
        {
            DelaySlot slot;
            slot.setDelayMs(50.0f);
            slot.setFeedback(0.6f);
            slot.prepare(kSR, kNs);
            juce::Random rng (42);
            bool ok = true;
            for (int blk = 0; blk < 8 && ok; ++blk)
            {
                juce::AudioBuffer<float> buf(2, kNs);
                for (int ch = 0; ch < 2; ++ch)
                    for (int i = 0; i < kNs; ++i)
                        buf.setSample(ch, i, (rng.nextFloat() * 2.0f - 1.0f) * 0.1f);
                slot.process(buf);
                if (hasNaN(buf, 2, kNs)) ok = false;
            }
            expect (ok, "DelaySlot produced NaN/Inf during multi-block noise run");
        }

        beginTest ("ReverbSlot: 8 blocks of noise — output stays finite");
        {
            ReverbSlot slot;
            slot.prepare(kSR, kNs);
            juce::Random rng (99);
            bool ok = true;
            for (int blk = 0; blk < 8 && ok; ++blk)
            {
                juce::AudioBuffer<float> buf(2, kNs);
                for (int ch = 0; ch < 2; ++ch)
                    for (int i = 0; i < kNs; ++i)
                        buf.setSample(ch, i, (rng.nextFloat() * 2.0f - 1.0f) * 0.1f);
                slot.processReturn(buf);
                if (hasNaN(buf, 2, kNs)) ok = false;
            }
            expect (ok, "ReverbSlot produced NaN/Inf during multi-block noise run");
        }
    }
};

static MuTantSendFXSmokeTest muTantSendFXSmokeTest;
