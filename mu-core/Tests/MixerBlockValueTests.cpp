// Mixer block-value, insert-latency and structural-edit stress tests (the layer-extraction safety net).
//
// MixerStereoPeakTests only checks peaks. These tests drive the real MixerEngine::processBlock with known
// per-channel input blocks and compare the output sample for sample, so a change to gain staging, pan law,
// send routing, mute/solo rules or the sidechain tap shows up as a failure here before it reaches a listener.
//
// Exactness: every gain and every input is a dyadic fraction (a multiple of a power of two with few
// significant bits), so each product and sum below is exact in float whatever the vector unit does (fused or
// separate multiply-add, any summation order). Comparisons are therefore bit-exact. The one exception is the
// sidechain envelope with a non-zero attack (it uses std::exp, a libm result): that case is compared to 1e-6.
// FX returns are exact too, via 1 ms delay-line slots fed a constant (see Rig); the reverb return is checked
// by scaling against its own unity-gain render.
//
// Block sizes: 1, 7, 64 and 512 are exercised against a mixer prepared for 512. MixerEngine sizes its channel
// and send buffers once in prepare(); a block LARGER than the prepared size runs off those buffers (the product
// shells split oversize host blocks before calling it), so nothing here renders one.

#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>

#include "Audio/MixerEngine.h"
#include "Audio/FX/Slots/FXChain.h"
#include "Audio/FX/Insert/NoneInsert.h"
#include "Audio/FX/Insert/SoftClipInsert.h"
#include "Audio/FX/Insert/HardClipInsert.h"
#include "Audio/FX/Insert/FoldInsert.h"
#include "Audio/FX/Insert/BitcrusherInsert.h"
#include "Audio/FX/Insert/ClipperInsert.h"
#include "Audio/FX/Insert/EqInsert.h"
#include "Audio/FX/Insert/CompressorLimiterInsert.h"
#include "Audio/FX/Insert/RingModInsert.h"
#include "Audio/FX/Insert/TapeSatInsert.h"
#include "Audio/FX/Insert/KarplusStrongInsert.h"
#include "Audio/FX/Insert/VocoderInsert.h"
#include "Audio/InsertProcessor.h"
#include "Audio/InsertSlotConfig.h"
#include "Audio/AlgorithmNames.h"
#include "Plugin/ProcessorBase.h"
#include "Plugin/MixerFxParams.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace
{
    constexpr double kRate     = 48000.0;
    constexpr int    kPrepared = 512;
    constexpr int    kSizes[]  = { 1, 7, 64, 512 };

    // A repeating ramp of dyadic values 1/16 .. 1, so neighbouring samples differ and a wrong sample
    // order or offset cannot hide behind a constant.
    float pat(int i) { return (float) ((i * 5) % 16 + 1) / 16.0f; }

    // What a channel renders: a patterned stereo ramp (right = -left / 2) or a constant. A constant is
    // scale on the left and scale x rightMul on the right (1 = the same on both sides).
    struct Src
    {
        float scale    = 0.0f;
        bool  constant = false;
        float rightMul = 1.0f;
        float left(int i)  const { return constant ? scale : scale * pat(i); }
        float right(int i) const { return constant ? scale * rightMul : -0.5f * scale * pat(i); }
    };

    // Samples a constant send must have been running before a 1 ms delay line returns it exactly: the
    // 48-sample delay plus the four Hermite taps (about 52), rounded up.
    constexpr int kSettle = 256;

    // A mixer whose three FX slots are all DISABLED by default, and a render hook that plays the configured
    // sources. A disabled slot returns silence (its send is already in the main bus), so with the defaults a
    // send reaches the output only through the dry path and every return is silent.
    //
    // A test that needs a known FX return asks for one with effectReturn / delayReturn. That enables the
    // Effect slot (Echo algorithm) or the Delay slot as a 1 ms (48-sample), zero-feedback, no-dirt delay.
    // Fed a CONSTANT send that has run for longer than kSettle samples, such a delay returns the constant
    // exactly: Hermite interpolation of four equal taps has c1, c2 and c3 exactly zero, so the result is c0
    // (the constant) wherever the sub-sample glide position sits. A return is then the send times the return
    // fader and pan, bit for bit. Use settle() before the block that is checked. The reverb has no such
    // exact form; it is enabled directly where a test needs it.
    struct Rig
    {
        MixerEngine mixer;
        FXChain     fx;
        juce::AudioBuffer<float> out { 2, kPrepared };
        std::array<Src, MixerEngine::MaxChannels> src {};
        MixerEngine::RenderChannelFn render;

        explicit Rig(bool effectReturn = false, bool delayReturn = false)
        {
            mixer.prepare(kRate, kPrepared);

            // Both delay-style slots: 1 ms, no feedback, no dirt, no spread. Set BEFORE prepare so the 50 ms
            // time glide starts at the target instead of ramping towards it.
            auto shape = [](DelaySlot& d)
            {
                d.setTimeMode(DelaySlot::TimeMode::Free);
                d.setDelayMs(1.0f);
                d.setFeedback(0.0f);
                d.setSpread(0.0f);
                d.setDirt(0.0f);
            };
            fx.effectSlot().setAlgorithm(EffectSlot::kEchoAlgoIndex);
            shape(fx.effectSlot().getEchoDelay());
            shape(fx.delaySlot());
            fx.prepare(kRate, kPrepared);

            fx.effectSlot().setEnabled(effectReturn);
            fx.delaySlot().setEnabled(delayReturn);
            fx.reverbSlot().setEnabled(false);
            render = [this](int r, juce::AudioBuffer<float>& buf, int n)
            {
                for (int i = 0; i < n; ++i)
                {
                    buf.setSample(0, i, src[(size_t) r].left(i));
                    buf.setSample(1, i, src[(size_t) r].right(i));
                }
            };
        }

        void run(int channels, int n,
                 std::array<juce::AudioBuffer<float>*, MixerEngine::MaxChannels>* directOuts = nullptr,
                 juce::AudioBuffer<float>* fxReturns = nullptr)
        {
            mixer.processBlock(out, channels, nullptr, fx, n, directOuts, fxReturns, nullptr, &render);
        }

        // Plays blocks of n samples until kSettle samples have passed, so the 1 ms delay lines are full of the
        // constant send and the next block is in steady state.
        void settle(int channels, int n)
        {
            for (int done = 0; done < kSettle; done += n)
                run(channels, n);
        }
    };

    // The fader/pan law of the mixer: left falls as pan moves right, right falls as pan moves left.
    float gainL(float level, float pan) { return level * (1.0f - std::max(0.0f, pan)); }
    float gainR(float level, float pan) { return level * (1.0f + std::min(0.0f, pan)); }

    // Post-trim, pre-fader channel sample (the sidechain tap) and post-fader sample.
    float trimmed(float x) { return x * MixerEngine::kHeadroomTrim; }

    // The post-trim source level at which the sidechain duck is full depth (kScFullDuckLevel in
    // MixerEngine.cpp, which keeps it private). Below it the duck follows the level in proportion. A power
    // of two, so dividing a dyadic source by it is exact. If the mixer retunes it, retune this and the
    // sidechain expectations together.
    constexpr float kFullDuck = 0.25f;

    // The follower target for a post-trim source level: proportional up to kFullDuck, then clamped to 1.
    float duckTarget(float postTrimSource) { return std::min(1.0f, std::abs(postTrimSource) / kFullDuck); }
}

struct MixerBlockValueTests : juce::UnitTest
{
    MixerBlockValueTests() : juce::UnitTest("Mixer block values (level, pan, sends, mute/solo, sidechain)", "mu-core") {}

    // Bit-exact comparison of one output channel against an expected function of the sample index.
    template <class Fn>
    void expectChannel(const juce::AudioBuffer<float>& buf, int ch, int n, Fn expected, const juce::String& what)
    {
        for (int i = 0; i < n; ++i)
        {
            const float got = buf.getSample(ch, i), want = expected(i);
            if (got != want)
            {
                expect(false, what + " ch" + juce::String(ch) + " sample " + juce::String(i) + " of " + juce::String(n)
                                  + ": got " + juce::String(got, 9) + " want " + juce::String(want, 9));
                return;
            }
        }
        expect(true);
    }

    void runTest() override
    {
        levelPanTrim();
        masterAndDirect();
        sends();
        disabledSlots();
        echoRepeatsReachReverb();
        reverbReturnStrip();
        muteSoloTruthTable();
        sidechainTap();
        sidechainOrder();
        sidechainEnvelope();
        sidechainLevelFollow();
    }

    // Fader, pan law, headroom trim and pan modulation, for every block size.
    void levelPanTrim()
    {
        beginTest("unity strip: the output is the input times the fixed headroom trim, at every block size");
        for (int n : kSizes)
        {
            Rig r;
            r.src[0] = { 1.0f, false };
            r.run(1, n);
            expectChannel(r.out, 0, n, [&](int i) { return trimmed(r.src[0].left(i)); }, "L");
            expectChannel(r.out, 1, n, [&](int i) { return trimmed(r.src[0].right(i)); }, "R");
        }

        beginTest("fader and linear pan law: gains per side, at every block size");
        for (int n : kSizes)
        {
            for (float level : { 1.0f, 0.5f, 0.25f, 0.0f })
            {
                for (float pan : { -1.0f, -0.5f, 0.0f, 0.5f, 1.0f })
                {
                    Rig r;
                    r.src[0] = { 0.5f, false };
                    r.mixer.channels[0].level.store(level);
                    r.mixer.channels[0].pan.store(pan);
                    r.run(1, n);
                    const juce::String tag = "level " + juce::String(level) + " pan " + juce::String(pan);
                    expectChannel(r.out, 0, n, [&](int i) { return trimmed(r.src[0].left(i)) * gainL(level, pan); }, tag + " L");
                    expectChannel(r.out, 1, n, [&](int i) { return trimmed(r.src[0].right(i)) * gainR(level, pan); }, tag + " R");
                }
            }
        }

        beginTest("pan modulation replaces the knob for the block; NaN means unmodulated");
        {
            Rig r;
            r.src[0] = { 0.5f, true };
            r.mixer.channels[0].pan.store(1.0f);
            r.mixer.channels[0].panMod.store(-0.5f);
            r.run(1, 64);
            expectChannel(r.out, 0, 64, [&](int) { return trimmed(0.5f) * gainL(1.0f, -0.5f); }, "modulated L");
            expectChannel(r.out, 1, 64, [&](int) { return trimmed(0.5f) * gainR(1.0f, -0.5f); }, "modulated R");
            r.mixer.channels[0].panMod.store(MixerEngine::ChannelState::kNoMod);
            r.run(1, 64);
            expectChannel(r.out, 0, 64, [&](int) { return trimmed(0.5f) * gainL(1.0f, 1.0f); }, "knob L");
            expectChannel(r.out, 1, 64, [&](int) { return trimmed(0.5f) * gainR(1.0f, 1.0f); }, "knob R");
        }

        beginTest("channels sum in the master bus; channels above the active count are not rendered");
        for (int n : kSizes)
        {
            Rig r;
            r.src[0] = { 1.0f, false };
            r.src[1] = { 0.5f, false };
            r.src[2] = { 0.25f, true };
            r.src[3] = { 1.0f, true };           // above the active count
            r.mixer.channels[1].level.store(0.5f);
            r.run(3, n);
            expectChannel(r.out, 0, n, [&](int i) {
                return (trimmed(r.src[0].left(i)) + trimmed(r.src[1].left(i)) * 0.5f) + trimmed(r.src[2].left(i)); }, "L");
            expectChannel(r.out, 1, n, [&](int i) {
                return (trimmed(r.src[0].right(i)) + trimmed(r.src[1].right(i)) * 0.5f) + trimmed(r.src[2].right(i)); }, "R");
            expectEquals(r.mixer.channelPeaks[3].load(), 0.0f, "an inactive strip reads silent");
        }
    }

    // Master fader and pan, the strip's direct output bus, and the FX-return fan-out.
    void masterAndDirect()
    {
        beginTest("master fader and pan scale the summed bus after the strips");
        for (int n : kSizes)
        {
            Rig r;
            r.src[0] = { 0.5f, false };
            r.mixer.masterLevel.store(0.5f);
            r.mixer.masterPan.store(0.5f);
            r.run(1, n);
            expectChannel(r.out, 0, n, [&](int i) { return trimmed(r.src[0].left(i)) * gainL(0.5f, 0.5f); }, "L");
            expectChannel(r.out, 1, n, [&](int i) { return trimmed(r.src[0].right(i)) * gainR(0.5f, 0.5f); }, "R");
            expectEquals(r.mixer.masterPeakL.load(), r.out.getMagnitude(0, 0, n), "master peak L is the output peak");
        }

        beginTest("a strip routed to a direct bus leaves the master and the sends, and gets the post-fader signal");
        {
            Rig r;
            r.src[0] = { 0.5f, false };
            r.src[1] = { 0.25f, true };
            r.mixer.channels[0].outputBus.store(1);
            r.mixer.channels[0].level.store(0.5f);
            r.mixer.channels[0].sendEffect.store(1.0f);     // ignored on a direct bus
            juce::AudioBuffer<float> bus(2, kPrepared);
            bus.clear();
            std::array<juce::AudioBuffer<float>*, MixerEngine::MaxChannels> outs {};
            outs[0] = &bus;
            r.run(2, 64, &outs);
            expectChannel(bus, 0, 64, [&](int i) { return trimmed(r.src[0].left(i)) * 0.5f; }, "bus L");
            expectChannel(bus, 1, 64, [&](int i) { return trimmed(r.src[0].right(i)) * 0.5f; }, "bus R");
            expectChannel(r.out, 0, 64, [&](int) { return trimmed(0.25f); }, "master has only the other strip, L");
            expectChannel(r.out, 1, 64, [&](int) { return trimmed(0.25f); }, "master has only the other strip, R");
        }

        beginTest("a strip on a bus nobody supplied is silent (no master, no send)");
        {
            Rig r;
            r.src[0] = { 0.5f, true };
            r.mixer.channels[0].outputBus.store(3);
            r.mixer.channels[0].sendEffect.store(1.0f);
            r.run(1, 64);
            expectChannel(r.out, 0, 64, [](int) { return 0.0f; }, "master L");
            expectChannel(r.out, 1, 64, [](int) { return 0.0f; }, "master R");
        }
    }

    // Effect/delay/reverb sends and returns. The Effect and Delay slots run as exact 1 ms delays (see Rig), so
    // a steady return is the send times the return fader and pan. The reverb slot is disabled here: it
    // returns silence, which the first test pins; its return strip is covered by reverbReturnStrip().
    void sends()
    {
        beginTest("sends are post-fader, post-pan; a return is the (delayed) send bus times the return fader and pan; a disabled reverb adds nothing");
        for (int n : kSizes)
        {
            Rig r(true, true);
            r.src[0] = { 0.5f, true, -0.5f };
            r.mixer.channels[0].level.store(0.5f);
            r.mixer.channels[0].pan.store(0.5f);
            r.mixer.channels[0].sendEffect.store(0.5f);
            r.mixer.channels[0].sendDelay.store(0.25f);
            r.mixer.channels[0].sendReverb.store(1.0f);
            r.mixer.returns[0].level.store(1.0f);
            r.mixer.returns[1].level.store(0.5f);
            r.mixer.returns[2].level.store(0.25f);
            r.mixer.returns[2].pan.store(-0.5f);
            r.settle(1, n);
            juce::AudioBuffer<float> fxOut(2, kPrepared);
            fxOut.clear();
            r.run(1, n, nullptr, &fxOut);

            // The source is constant, so every term below is a constant of the block.
            const float dryL = trimmed(r.src[0].left(0))  * gainL(0.5f, 0.5f);
            const float dryR = trimmed(r.src[0].right(0)) * gainR(0.5f, 0.5f);
            const float retL = (dryL * 0.5f) * gainL(1.0f, 0.0f);
            const float retR = (dryR * 0.5f) * gainR(1.0f, 0.0f);
            const float dlyL = (dryL * 0.25f) * gainL(0.5f, 0.0f);
            const float dlyR = (dryR * 0.25f) * gainR(0.5f, 0.0f);
            // The reverb send is 1.0 and its return fader/pan are set, but the slot is disabled: no wet at all.
            expectChannel(r.out, 0, n, [&](int) { return (dryL + retL) + dlyL; }, "master L");
            expectChannel(r.out, 1, n, [&](int) { return (dryR + retR) + dlyR; }, "master R");
            expectChannel(fxOut, 0, n, [&](int) { return retL + dlyL; }, "fx-returns bus L");
            expectChannel(fxOut, 1, n, [&](int) { return retR + dlyR; }, "fx-returns bus R");
            expectEquals(r.mixer.returnPeaks[2].load(), 0.0f, "disabled reverb: return peak is zero");
        }

        beginTest("send modulation replaces the knob; several strips add into one send bus");
        {
            Rig r(true, false);
            r.src[0] = { 0.5f, true };
            r.src[1] = { 0.25f, true };
            r.mixer.channels[0].sendEffect.store(0.0f);
            r.mixer.channels[0].sendEffectMod.store(0.5f);   // modulated on
            r.mixer.channels[1].sendEffect.store(1.0f);
            r.mixer.channels[1].sendEffectMod.store(0.0f);   // modulated off (0 is a value, not "unmodulated")
            r.mixer.returns[0].level.store(1.0f);
            r.settle(2, 64);
            r.run(2, 64);
            expectChannel(r.out, 0, 64, [&](int) { return (trimmed(0.5f) + trimmed(0.25f)) + (trimmed(0.5f) * 0.5f); }, "L");
        }

        beginTest("a muted return is not heard (the same return is heard when unmuted); a muted strip feeds no send");
        {
            Rig r(false, true);                              // the delay return is the exact one
            r.src[0] = { 0.5f, true };
            r.mixer.channels[0].sendDelay.store(1.0f);
            r.mixer.returns[1].level.store(1.0f);
            r.settle(1, 64);
            r.run(1, 64);
            expectChannel(r.out, 0, 64, [&](int) { return trimmed(0.5f) + trimmed(0.5f); }, "return heard (control)");
            r.mixer.returns[1].mute.store(true);
            r.run(1, 64);
            expectChannel(r.out, 0, 64, [&](int) { return trimmed(0.5f); }, "return muted");
            expectEquals(r.mixer.returnPeaks[1].load(), 0.0f, "return peak cleared");
        }

        beginTest("a muted strip feeds no send: nothing reaches the output, and no wet appears after the delay time");
        {
            // A fresh rig whose strip is muted from the first sample: if the mute did not gate the send, the
            // wet would appear once the 48-sample delay has elapsed, so the whole 512-sample block is checked.
            Rig r(false, true);
            r.src[0] = { 0.5f, true };
            r.mixer.channels[0].sendDelay.store(1.0f);
            r.mixer.returns[1].level.store(1.0f);
            r.mixer.channels[0].mute.store(true);
            r.run(1, 512);
            expectChannel(r.out, 0, 512, [](int) { return 0.0f; }, "strip muted: no dry and no wet");
        }
    }

    // Brings the rig's slots to the product default (all enabled, Chorus on the Effect slot, Echo delay on).
    static void enableAllSlots(Rig& r)
    {
        r.fx.effectSlot().setAlgorithm(0);
        r.fx.effectSlot().setEnabled(true);
        r.fx.effectSlot().getEchoDelay().setEnabled(true);
        r.fx.delaySlot().setEnabled(true);
        r.fx.reverbSlot().setEnabled(true);
    }

    // A disabled FX slot returns silence. The send bus it is handed holds the strips' dry signal, which is
    // already in the main bus; passing it on would add the dry signal a second time through the return.
    // Each case disables one slot (others stay enabled but get no send) or all, sends the strips into the
    // disabled slot's bus at full level, and compares the output bit for bit with a reference rig that has
    // the same slot state and no sends at all, i.e. the dry-only render.
    void disabledSlots()
    {
        enum Case { EffectOff, EffectOffEcho, EchoOff, DelayOff, ReverbOff, AllOff, kCases };
        const char* names[kCases] = { "Effect slot (Chorus)", "Effect slot (Echo algorithm)", "Echo delay inside an enabled Effect slot",
                                      "Delay slot", "Reverb slot", "all three slots together" };

        for (int c = 0; c < kCases; ++c)
        {
            beginTest(juce::String("a disabled ") + names[c] + ": the output is the dry-only render, the returns are silent");
            for (int n : kSizes)
            {
                Rig dry, wet;
                for (Rig* r : { &dry, &wet })
                {
                    enableAllSlots(*r);
                    r->src[0] = { 0.5f, false };
                    r->src[1] = { 0.25f, false };
                    r->mixer.channels[0].pan.store(0.5f);
                    for (int ri = 0; ri < 3; ++ri) r->mixer.returns[(size_t) ri].level.store(1.0f);
                }
                for (Rig* r : { &dry, &wet })
                {
                    switch (c)
                    {
                        case EffectOff:     r->fx.effectSlot().setEnabled(false); break;
                        case EffectOffEcho: r->fx.effectSlot().setAlgorithm(EffectSlot::kEchoAlgoIndex);
                                            r->fx.effectSlot().setEnabled(false); break;
                        case EchoOff:       r->fx.effectSlot().setAlgorithm(EffectSlot::kEchoAlgoIndex);
                                            r->fx.effectSlot().getEchoDelay().setEnabled(false); break;
                        case DelayOff:      r->fx.delaySlot().setEnabled(false); break;
                        case ReverbOff:     r->fx.reverbSlot().setEnabled(false); break;
                        default:            r->fx.effectSlot().setEnabled(false);
                                            r->fx.delaySlot().setEnabled(false);
                                            r->fx.reverbSlot().setEnabled(false); break;
                    }
                }
                // Full-level send into the disabled slot's bus (all three for the combined case); the
                // reference gets none.
                for (int s = 0; s < 2; ++s)
                {
                    auto& ch = wet.mixer.channels[(size_t) s];
                    if (c == EffectOff || c == EffectOffEcho || c == EchoOff || c == AllOff) ch.sendEffect.store(1.0f);
                    if (c == DelayOff  || c == AllOff) ch.sendDelay.store(1.0f);
                    if (c == ReverbOff || c == AllOff) ch.sendReverb.store(1.0f);
                }

                // Several blocks, so a leak that only shows once the slot's own state has filled is seen too.
                for (int b = 0; b < 4; ++b)
                {
                    juce::AudioBuffer<float> dryBus(2, kPrepared), wetBus(2, kPrepared);
                    dryBus.clear();
                    wetBus.clear();
                    dry.run(2, n, nullptr, &dryBus);
                    wet.run(2, n, nullptr, &wetBus);
                    const juce::String tag = juce::String(names[c]) + " block " + juce::String(b) + " size " + juce::String(n);
                    for (int ch = 0; ch < 2; ++ch)
                    {
                        expectChannel(wet.out, ch, n, [&](int i) { return dry.out.getSample(ch, i); }, tag + " main output");
                        expectChannel(wetBus, ch, n, [&](int i) { return dryBus.getSample(ch, i); }, tag + " fx-returns bus");
                    }
                }
                // The disabled slot's own return meter reads zero (the other slots had no send).
                if (c == EffectOff || c == EffectOffEcho || c == EchoOff || c == AllOff)
                    expectEquals(wet.mixer.returnPeaks[0].load(), 0.0f, juce::String(names[c]) + ": effect return peak");
                if (c == DelayOff || c == AllOff)
                    expectEquals(wet.mixer.returnPeaks[1].load(), 0.0f, juce::String(names[c]) + ": delay return peak");
                if (c == ReverbOff || c == AllOff)
                    expectEquals(wet.mixer.returnPeaks[2].load(), 0.0f, juce::String(names[c]) + ": reverb return peak");
            }
        }

        beginTest("a disabled Effect slot passes nothing on through Effect->Delay and Effect->Reverb routing");
        for (int n : kSizes)
        {
            Rig dry, wet;
            for (Rig* r : { &dry, &wet })
            {
                enableAllSlots(*r);                          // Delay and Reverb run, with no send of their own
                r->fx.effectSlot().setEnabled(false);
                r->fx.setEffectToDelaySend(1.0f);
                r->fx.setEffectToReverbSend(1.0f);
                r->fx.setDelayToReverbSend(1.0f);
                r->src[0] = { 0.5f, false };
                for (int ri = 0; ri < 3; ++ri) r->mixer.returns[(size_t) ri].level.store(1.0f);
            }
            wet.mixer.channels[0].sendEffect.store(1.0f);
            for (int b = 0; b < 4; ++b)
            {
                dry.run(1, n);
                wet.run(1, n);
                for (int ch = 0; ch < 2; ++ch)
                    expectChannel(wet.out, ch, n, [&](int i) { return dry.out.getSample(ch, i); },
                                  "routing from a disabled Effect, block " + juce::String(b) + " size " + juce::String(n));
            }
            expectEquals(wet.mixer.returnPeaks[1].load(), 0.0f, "delay return stays silent");
            expectEquals(wet.mixer.returnPeaks[2].load(), 0.0f, "reverb return stays silent");
        }
    }

    // An Echo tail keeps feeding the reverb through Effect->Reverb after the channel send has gone silent:
    // the routing must not be gated on the channel sends having signal this block.
    void echoRepeatsReachReverb()
    {
        beginTest("Echo with Effect->Reverb routing: the repeats reach the reverb while the channel sends are silent");
        {
            constexpr int kBlock = 512, kBlocks = 24;
            Rig r;
            r.fx.effectSlot().setEnabled(true);              // the Rig leaves the algorithm on Echo
            r.fx.reverbSlot().setEnabled(true);
            r.fx.effectSlot().getEchoDelay().setDelayMs(20.0f);   // 960 samples: the first repeat is after block 0
            r.fx.effectSlot().getEchoDelay().setFeedback(0.5f);
            r.fx.setEffectToReverbSend(1.0f);
            // Re-prepare so the 50 ms time glide starts at 20 ms rather than gliding from the rig's 1 ms
            // (which would let the first repeat land inside block 0). Slot enable flags survive prepare().
            r.fx.prepare(kRate, kPrepared);

            // Source: a constant in block 0 only, then silence, so every later signal is an echo repeat.
            int blockIndex = 0;
            r.render = [&](int, juce::AudioBuffer<float>& buf, int n)
            {
                const float v = blockIndex == 0 ? 0.5f : 0.0f;
                for (int i = 0; i < n; ++i) { buf.setSample(0, i, v); buf.setSample(1, i, v); }
            };
            r.mixer.channels[0].sendEffect.store(1.0f);      // no reverb send and no delay send anywhere
            r.mixer.returns[0].level.store(1.0f);
            r.mixer.returns[2].level.store(1.0f);

            float echoBlock0 = -1.0f, reverbBlock0 = -1.0f, echoLater = 0.0f, reverbLater = 0.0f;
            for (int b = 0; b < kBlocks; ++b)
            {
                blockIndex = b;
                r.run(1, kBlock);
                if (b == 0)
                {
                    echoBlock0   = r.mixer.returnPeaks[0].load();
                    reverbBlock0 = r.mixer.returnPeaks[2].load();
                }
                else
                {
                    echoLater   = juce::jmax(echoLater,   r.mixer.returnPeaks[0].load());
                    reverbLater = juce::jmax(reverbLater, r.mixer.returnPeaks[2].load());
                }
            }
            // Block 0: the repeat is not due yet, so neither the echo return nor the reverb has signal.
            expectEquals(echoBlock0, 0.0f, "no echo repeat in block 0");
            expectEquals(reverbBlock0, 0.0f, "nothing reached the reverb in block 0");
            // Later blocks: the repeats are audible, and the reverb (fed only by them) has a return.
            expect(echoLater > 0.05f, "the echo repeats are audible (" + juce::String(echoLater, 5) + ")");
            expect(reverbLater > 1.0e-4f, "the reverb sounds from the routed repeats (" + juce::String(reverbLater, 8) + ")");
        }
    }

    // The reverb has no exact closed form, so its return fader and pan are checked against itself: a render
    // at unity return gain is the reference, and a render with level 0.25 and pan -0.5 must be exactly that
    // reference times the per-side gains (0.25 left, 0.125 right: powers of two, so the product is exact).
    void reverbReturnStrip()
    {
        beginTest("reverb return strip: fader and pan scale the wet exactly, against a unity-gain reference render");
        {
            constexpr int kBlk = 256, kBlocks = 24;
            std::vector<float> refL, refR, again_L, again_R, scL, scR;

            // Plays a constant into the reverb send for kBlocks blocks and collects the fx-returns bus.
            auto renderWet = [&](float level, float pan, std::vector<float>& L, std::vector<float>& R)
            {
                Rig r;
                r.fx.reverbSlot().setEnabled(true);
                r.src[0] = { 0.5f, true, -0.5f };
                r.mixer.channels[0].sendReverb.store(1.0f);
                r.mixer.returns[2].level.store(level);
                r.mixer.returns[2].pan.store(pan);
                for (int b = 0; b < kBlocks; ++b)
                {
                    juce::AudioBuffer<float> bus(2, kPrepared);
                    bus.clear();
                    r.run(1, kBlk, nullptr, &bus);
                    for (int i = 0; i < kBlk; ++i) { L.push_back(bus.getSample(0, i)); R.push_back(bus.getSample(1, i)); }
                }
            };
            renderWet(1.0f, 0.0f, refL, refR);
            renderWet(1.0f, 0.0f, again_L, again_R);
            renderWet(0.25f, -0.5f, scL, scR);

            // Precondition: two reverbs built the same way render the same samples, else the comparison below
            // would blame the return strip for reverb randomness.
            expect(refL == again_L && refR == again_R, "the reverb render is reproducible across instances (precondition)");

            int compared = 0;
            float refPeak = 0.0f;
            for (int b = 0; b < kBlocks; ++b)
            {
                // Skip blocks whose reference is near the mixer's 1e-9 'has signal' gate, where the scaled
                // render may legitimately be dropped while the reference is not.
                float blockPeak = 0.0f;
                for (int i = 0; i < kBlk; ++i)
                    blockPeak = juce::jmax(blockPeak, std::abs(refL[(size_t) (b * kBlk + i)]), std::abs(refR[(size_t) (b * kBlk + i)]));
                refPeak = juce::jmax(refPeak, blockPeak);
                if (blockPeak < 1.0e-6f) continue;
                ++compared;
                for (int i = 0; i < kBlk; ++i)
                {
                    const size_t k = (size_t) (b * kBlk + i);
                    if (scL[k] != refL[k] * gainL(0.25f, -0.5f) || scR[k] != refR[k] * gainR(0.25f, -0.5f))
                    {
                        expect(false, "block " + juce::String(b) + " sample " + juce::String(i) + ": scaled L " + juce::String(scL[k], 9)
                                          + " vs ref L x gain " + juce::String(refL[k] * gainL(0.25f, -0.5f), 9)
                                          + ", scaled R " + juce::String(scR[k], 9) + " vs ref R x gain "
                                          + juce::String(refR[k] * gainR(0.25f, -0.5f), 9));
                        return;
                    }
                }
            }
            expect(refPeak > 1.0e-4f, "the reference reverb return is audible (peak " + juce::String(refPeak, 6) + ")");
            expect(compared >= 8, "at least 8 blocks carried signal and were compared (" + juce::String(compared) + ")");
        }
    }

    // Truth table: every mute x solo combination of three strips, against each return-solo state.
    void muteSoloTruthTable()
    {
        beginTest("mute / solo / return-solo truth table (3 strips, 4 return states, 64 mute-solo combinations)");
        enum RetState { None, EffectSolo, DelaySolo, EffectMute, kStates };
        const float scale[3] = { 1.0f, 0.5f, 0.25f };
        int combos = 0, failures = 0;
        for (int st = 0; st < kStates; ++st)
        {
            for (int ms = 0; ms < 64; ++ms)
            {
                Rig r(true, true);   // Effect and Delay returns both live; only the Effect return is fed
                bool mute[3], solo[3];
                for (int c = 0; c < 3; ++c)
                {
                    mute[c] = (ms >> c) & 1;
                    solo[c] = (ms >> (c + 3)) & 1;
                    r.src[(size_t) c] = { scale[c], true };
                    r.mixer.channels[(size_t) c].mute.store(mute[c]);
                    r.mixer.channels[(size_t) c].solo.store(solo[c]);
                    r.mixer.channels[(size_t) c].sendEffect.store(0.5f);
                }
                r.mixer.returns[0].level.store(1.0f);
                r.mixer.returns[1].level.store(1.0f);
                r.mixer.returns[0].solo.store(st == EffectSolo);
                r.mixer.returns[1].solo.store(st == DelaySolo);
                r.mixer.returns[0].mute.store(st == EffectMute);
                r.settle(3, 64);     // fill the 1 ms delay line so the checked block is in steady state
                r.run(3, 64);

                const bool anySolo = solo[0] || solo[1] || solo[2];
                const bool anyRetSolo = st == EffectSolo || st == DelaySolo;
                float dry = 0.0f, wet = 0.0f;
                for (int c = 0; c < 3; ++c)
                {
                    const bool audible = ! mute[c] && ! (anySolo && ! solo[c]);
                    if (! audible) continue;               // muted or un-soloed: no dry and no send
                    if (! anyRetSolo) dry += trimmed(scale[c]);
                    wet += trimmed(scale[c]) * 0.5f;       // a return solo mutes the dry path, not the send
                }
                const bool effectHeard = st != EffectMute && st != DelaySolo;
                const float want = dry + (effectHeard ? wet : 0.0f);
                ++combos;
                if (r.out.getSample(0, 0) != want || r.out.getSample(1, 63) != want)
                {
                    if (++failures <= 5)
                        expect(false, "state " + juce::String(st) + " mute/solo bits " + juce::String(ms)
                                          + ": got " + juce::String(r.out.getSample(0, 0), 9) + " want " + juce::String(want, 9));
                }
            }
        }
        expectEquals(failures, 0, "of " + juce::String(combos) + " combinations");
    }

    // The sidechain tap: post-trim, pre-fader, post-pan-irrelevant; source is judged audible by mute/solo only.
    void sidechainTap()
    {
        const float duckDepth = 0.5f;

        auto duckPair = [&](Rig& r)
        {
            // Strip 0 ducks from strip 1 with an instant attack (a release of 0 is floored to 30 ms inside the
            // mixer, which a constant source never exercises). Both strips play 0.5, post-trim 0.25 = kFullDuck:
            // the follower target is exactly 1, so the first sample is a full duck and it then holds at 1.
            r.src[0] = { 0.5f, true };
            r.src[1] = { 0.5f, true };
            r.mixer.channels[0].sidechainSource.store(1);
            r.mixer.channels[0].sidechainAmount.store(duckDepth);
            r.mixer.channels[0].sidechainAttackMs.store(0.0f);
            r.mixer.channels[0].sidechainReleaseMs.store(0.0f);
        };

        beginTest("sidechain ducks the target by the full amount when the source is at the full-duck level, whatever the source fader");
        for (int n : kSizes)
        {
            Rig r;
            duckPair(r);
            r.mixer.channels[1].level.store(0.0f);      // the tap is pre-fader: a silent fader still ducks
            r.run(2, n);
            expectChannel(r.out, 0, n, [&](int) { return trimmed(0.5f) * (1.0f - duckDepth); }, "ducked target");
            expectEquals(r.mixer.sidechainGR[0].load(), duckDepth, "GR meter");
            expectEquals(r.mixer.sidechainGR[1].load(), 0.0f, "source GR");
        }

        beginTest("sidechain source below the full-duck level ducks in proportion to its level, not fully");
        for (int n : kSizes)
        {
            // Pre-trim source 0.125 / 0.25 / 0.5 / 1.0 is post-trim 1/16, 1/8, 1/4, 1/2: the follower target is
            // the post-trim level over kFullDuck = 1/4, 1/2, 1, then clamped to 1. All dyadic, so exact.
            for (float srcAmp : { 0.125f, 0.25f, 0.5f, 1.0f })
            {
                Rig r;
                duckPair(r);
                r.src[1] = { srcAmp, true };
                r.run(2, n);
                const float follow = duckTarget(trimmed(srcAmp));
                const float gr     = duckDepth * follow;
                const juce::String tag = "source " + juce::String(srcAmp) + " block " + juce::String(n);
                expectChannel(r.out, 0, n, [&](int) { return trimmed(0.5f) * (1.0f - gr) + trimmed(srcAmp); }, tag + " L");
                expectEquals(r.mixer.sidechainGR[0].load(), gr, tag + " GR meter");
                expectEquals(r.mixer.getSidechainEnv(0), follow, tag + " follower");
            }
        }

        beginTest("a source that is not dyadic (post-trim 0.05) ducks to a fifth of the amount, not fully");
        {
            // 0.05 / 0.25 = 0.2 is not exactly representable, and a follower held at a non-power-of-two level
            // re-rounds each sample (a few ulp over 64 samples), so this one is compared to a tolerance.
            Rig r;
            duckPair(r);
            r.src[1] = { 0.1f, true };
            r.run(2, 64);
            expectWithinAbsoluteError(r.mixer.getSidechainEnv(0), 0.2f, 1.0e-5f);
            expectWithinAbsoluteError(r.mixer.sidechainGR[0].load(), duckDepth * 0.2f, 1.0e-5f);
            expectWithinAbsoluteError(r.out.getSample(0, 63), trimmed(0.5f) * (1.0f - duckDepth * 0.2f) + trimmed(0.1f), 1.0e-5f);
        }

        beginTest("a silent source does not duck, and leaves the follower at rest");
        {
            Rig r;
            duckPair(r);
            r.src[1] = { 0.0f, true };
            r.run(2, 64);
            expectChannel(r.out, 0, 64, [&](int) { return trimmed(0.5f); }, "target undisturbed");
            expectEquals(r.mixer.sidechainGR[0].load(), 0.0f, "GR meter");
            expectEquals(r.mixer.getSidechainEnv(0), 0.0f, "follower");
        }

        beginTest("a muted source does not duck (isAudible rule); the target still plays");
        {
            Rig r;
            duckPair(r);
            r.mixer.channels[1].mute.store(true);
            r.run(2, 64);
            expectChannel(r.out, 0, 64, [&](int) { return trimmed(0.5f); }, "target undisturbed, source silent in the mix");
            expectEquals(r.mixer.sidechainGR[0].load(), 0.0f, "GR meter");
        }

        beginTest("a source that is not soloed while another strip is does not duck; a soloed source does");
        {
            Rig r;
            duckPair(r);
            r.src[2] = { 0.25f, true };
            r.mixer.channels[0].solo.store(true);       // target soloed, source not
            r.run(3, 64);
            expectChannel(r.out, 0, 64, [&](int) { return trimmed(0.5f); }, "un-soloed source: no duck");
            expectEquals(r.mixer.sidechainGR[0].load(), 0.0f, "GR meter");
            r.mixer.channels[1].solo.store(true);       // now the source is soloed too
            r.mixer.channels[0].solo.store(true);
            r.mixer.setSidechainEnv(0, 0.0f);
            r.run(3, 64);
            expectChannel(r.out, 0, 64, [&](int) { return trimmed(0.5f) * (1.0f - duckDepth) + trimmed(0.5f); }, "soloed source ducks");
        }

        beginTest("the DAW sidechain input always counts as active, mute and solo state aside");
        {
            Rig r;
            r.src[0] = { 0.5f, true };
            r.mixer.channels[0].sidechainSource.store(MixerEngine::kExtSidechainSrc);
            r.mixer.channels[0].sidechainAmount.store(duckDepth);
            r.mixer.channels[0].sidechainAttackMs.store(0.0f);
            r.mixer.channels[0].sidechainReleaseMs.store(0.0f);
            std::vector<float> ext(kPrepared, 0.5f);
            r.mixer.setExternalSidechain(ext.data(), nullptr);
            r.run(1, 64);
            expectChannel(r.out, 0, 64, [&](int) { return trimmed(0.5f) * (1.0f - duckDepth); }, "ducked by the DAW bus");
            r.mixer.setExternalSidechain(nullptr, nullptr);
            r.run(1, 64);
            expectChannel(r.out, 0, 64, [&](int) { return trimmed(0.5f); }, "no bus supplied: no duck");
        }

        beginTest("a return ducks from a channel's post-trim signal and its source's mute/solo state");
        {
            Rig r(true, false);
            r.src[0] = { 0.5f, true };
            r.mixer.channels[0].sendEffect.store(1.0f);
            r.mixer.returns[0].level.store(1.0f);
            r.mixer.returns[0].sidechainSource.store(0);
            r.mixer.returns[0].sidechainAmount.store(0.5f);
            r.mixer.returns[0].sidechainAttackMs.store(0.0f);
            r.mixer.returns[0].sidechainReleaseMs.store(0.0f);
            r.settle(1, 64);
            r.run(1, 64);
            // dry 0.25; the send is the post-fader strip (0.25) ducked by half before the return fader: +0.125.
            expectChannel(r.out, 0, 64, [&](int) { return trimmed(0.5f) + trimmed(0.5f) * 0.5f; }, "ducked return");
            expectEquals(r.mixer.returnSidechainGR[0].load(), 0.5f, "return GR meter");
        }
    }

    // PINS TODAY'S ORDER. The mixer ducks strips in index order, IN PLACE, in channelBufs. A strip whose
    // source has a lower index than its own therefore reads a source that was already ducked this block;
    // a source with a higher index is read un-ducked. Mutual ducking (A ducks B and B ducks A) is order
    // dependent. If a later change takes the tap from a pre-duck snapshot, the "strip 1" expectations below
    // change deliberately (strip 1 would then duck too): update them in a signed golden-style commit.
    void sidechainOrder()
    {
        constexpr float kQuiet = 0.00390625f;   // 2^-8
        const float depth = 0.5f;

        auto mutual = [&](Rig& r, float amp0, float amp1)
        {
            r.src[0] = { amp0, true };
            r.src[1] = { amp1, true };
            for (int c = 0; c < 2; ++c)
            {
                r.mixer.channels[(size_t) c].sidechainSource.store(1 - c);
                r.mixer.channels[(size_t) c].sidechainAmount.store(depth);
                r.mixer.channels[(size_t) c].sidechainAttackMs.store(0.0f);
                r.mixer.channels[(size_t) c].sidechainReleaseMs.store(0.0f);
            }
        };

        // All amplitudes below are powers of two, so every follower target and gain is exact and a follower held at
        // its target stays bit-exact (rel x e + (1 - rel) x e = e when e is a power of two).
        beginTest("mutual ducking, quiet strip first: strip 1 reads strip 0 AFTER it was ducked, so it ducks only slightly");
        {
            Rig r;
            mutual(r, kQuiet, 0.5f);
            r.run(2, 64);
            // Strip 0 (post-trim 2^-9) ducks from strip 1 (post-trim 1/4 = kFullDuck, full): 2^-10. Strip 1 then
            // reads strip 0 at 2^-10, a follower target of 2^-10 / 2^-2 = 2^-8 and a GR of 2^-9. A pre-duck
            // snapshot would read 2^-9 and duck strip 1 twice as deep (2^-8).
            const float s0  = trimmed(kQuiet) * (1.0f - depth);
            const float gr1 = depth * duckTarget(s0);
            expectChannel(r.out, 0, 64, [&](int) { return s0 + trimmed(0.5f) * (1.0f - gr1); }, "mix");
            expectEquals(r.mixer.sidechainGR[0].load(), depth, "strip 0 GR");
            expectEquals(r.mixer.sidechainGR[1].load(), gr1, "strip 1 GR (order dependent)");
            expectEquals(gr1, 0.001953125f, "the arithmetic above: 2^-9");
        }

        beginTest("mutual ducking, loud strip first: both are ducked (the source stays at full-duck level after its own duck)");
        {
            Rig r;
            mutual(r, 1.0f, kQuiet);
            r.run(2, 64);
            // Strip 0 (post-trim 1/2) ducks from strip 1 (post-trim 2^-9): follower 2^-7, GR 2^-8, so strip 0 is
            // 1/2 x (1 - 2^-8), still well above kFullDuck. Strip 1 reads that: follower 1, GR = depth.
            const float gr0 = depth * duckTarget(trimmed(kQuiet));
            const float s0  = trimmed(1.0f) * (1.0f - gr0);
            expectChannel(r.out, 0, 64, [&](int) { return s0 + trimmed(kQuiet) * (1.0f - depth); }, "mix");
            expectEquals(r.mixer.sidechainGR[0].load(), gr0, "strip 0 GR");
            expectEquals(r.mixer.sidechainGR[1].load(), depth, "strip 1 GR");
            expectEquals(gr0, 0.00390625f, "the arithmetic above: 2^-8");
        }

        beginTest("a source with a higher index is read un-ducked; one with a lower index is read already ducked");
        {
            // Strip 0 ducks from 2 (higher: un-ducked) and strip 2 ducks from 0 (lower: already ducked). Both
            // are quiet-then-loud so the lower strip's duck decides how deep the higher strip's duck is.
            Rig r;
            r.src[0] = { kQuiet, true };
            r.src[1] = { 0.0f, true };
            r.src[2] = { 0.5f, true };
            for (int c : { 0, 2 })
            {
                r.mixer.channels[(size_t) c].sidechainSource.store(2 - c);
                r.mixer.channels[(size_t) c].sidechainAmount.store(depth);
                r.mixer.channels[(size_t) c].sidechainAttackMs.store(0.0f);
                r.mixer.channels[(size_t) c].sidechainReleaseMs.store(0.0f);
            }
            r.run(3, 64);
            // Strip 0 reads strip 2 at post-trim 1/4 (full): GR = depth, strip 0 becomes 2^-10. Strip 2 reads that:
            // follower 2^-8, GR 2^-9 (a read of the un-ducked 2^-9 would give GR 2^-8).
            expectEquals(r.mixer.sidechainGR[0].load(), depth, "strip 0 (reads strip 2 un-ducked)");
            expectEquals(r.mixer.sidechainGR[2].load(), 0.001953125f, "strip 2 (reads strip 0 already ducked)");
        }

        beginTest("a muted or un-soloed source in a mutual pair leaves both unducked; a muted target is silent but its source still ducks nothing");
        {
            Rig r;
            mutual(r, 0.5f, 0.5f);
            r.mixer.channels[1].mute.store(true);
            r.run(2, 64);
            // Strip 0 sees an inactive source (strip 1 muted): no duck. Strip 1 reads strip 0 (audible, loud):
            // it is ducked in its buffer, but its mute keeps it out of the mix.
            expectChannel(r.out, 0, 64, [&](int) { return trimmed(0.5f); }, "mix is strip 0 alone, unducked");
            expectEquals(r.mixer.sidechainGR[0].load(), 0.0f, "strip 0 GR");
            expectEquals(r.mixer.sidechainGR[1].load(), depth, "strip 1 GR still metered while muted");
        }

        beginTest("ducking is deterministic across block sizes when the attack is instant and the levels are constant");
        for (int n : kSizes)
        {
            Rig r;
            mutual(r, kQuiet, 0.5f);
            r.run(2, n);
            const float s0  = trimmed(kQuiet) * (1.0f - depth);
            const float gr1 = depth * duckTarget(s0);
            expectChannel(r.out, 0, n, [&](int) { return s0 + trimmed(0.5f) * (1.0f - gr1); }, "mix");
        }
    }

    // A non-zero attack uses std::exp, so these values are pinned to 1e-6 rather than bit-exact.
    void sidechainEnvelope()
    {
        beginTest("envelope: with a 5 ms attack the first sample ducks by amount x (1 - exp(-1/(0.005 x fs)))");
        {
            Rig r;
            r.src[0] = { 0.5f, true };
            r.src[1] = { 0.5f, true };
            r.mixer.channels[0].sidechainSource.store(1);
            r.mixer.channels[0].sidechainAmount.store(1.0f);
            r.mixer.channels[0].sidechainAttackMs.store(5.0f);
            r.mixer.channels[0].sidechainReleaseMs.store(100.0f);
            r.run(2, 1);
            const float atk = std::exp(-1.0f / (5.0f * 0.001f * (float) kRate));
            const float env = (1.0f - atk);
            expectWithinAbsoluteError(r.out.getSample(0, 0), trimmed(0.5f) * (1.0f - env) + trimmed(0.5f), 1.0e-6f);
            expectWithinAbsoluteError(r.mixer.sidechainGR[0].load(), env, 1.0e-6f);
            // The follower is plain state carried to the next block.
            expectWithinAbsoluteError(r.mixer.getSidechainEnv(0), env, 1.0e-6f);
        }
    }

    // The duck follows the source level and recovers between hits; the release has a 30 ms floor.
    void sidechainLevelFollow()
    {
        beginTest("a decaying kick (55 Hz, peak 0.8, 180 ms decay, every 0.5 s) lets the duck recover before the next hit");
        {
            // Strip 0 (a constant pad) ducks from strip 1 (the kick) at full amount, 5 ms attack, 100 ms release.
            // A gate held the duck at full depth for as long as the kick tail stayed above -60 dBFS, so the
            // follower never fell; the level follower must fall well below 0.3 before each next hit.
            constexpr int period = 24000;                 // 0.5 s at 48 kHz: 120 BPM, one hit per beat
            constexpr int beats  = 12;
            constexpr int block  = 64;
            Rig r;
            long long pos = 0;                            // absolute sample index of the next block
            r.render = [&](int ch, juce::AudioBuffer<float>& buf, int n)
            {
                for (int i = 0; i < n; ++i)
                {
                    float v = 0.5f;                       // the ducked pad
                    if (ch == 1)
                    {
                        // Exponentially decaying sine, restarted at every beat: peak 0.8 pre-trim = 0.4 post-trim.
                        const double t = (double) ((pos + i) % period) / kRate;
                        v = (float) (0.8 * std::exp(-t / 0.18) * std::sin(2.0 * juce::MathConstants<double>::pi * 55.0 * t));
                    }
                    buf.setSample(0, i, v);
                    buf.setSample(1, i, v);
                }
            };
            r.mixer.channels[0].sidechainSource.store(1);
            r.mixer.channels[0].sidechainAmount.store(1.0f);
            r.mixer.channels[0].sidechainAttackMs.store(5.0f);
            r.mixer.channels[0].sidechainReleaseMs.store(100.0f);

            std::array<float, beats> cycleMin, cycleMax;
            cycleMin.fill(1.0e9f);
            cycleMax.fill(0.0f);
            for (; pos < (long long) period * beats; )
            {
                r.run(2, block);
                pos += block;
                // Fold the follower at the end of each block into the cycle (beat) it belongs to.
                const int   cycle = (int) ((pos - 1) / period);
                const float env   = r.mixer.getSidechainEnv(0);
                cycleMin[(size_t) cycle] = std::min(cycleMin[(size_t) cycle], env);
                cycleMax[(size_t) cycle] = std::max(cycleMax[(size_t) cycle], env);
            }
            for (int cycle = 4; cycle < beats; ++cycle)   // beats 4 to 12: steady state, the follower's start-up long gone
            {
                const juce::String tag = "beat " + juce::String(cycle);
                expect(cycleMin[(size_t) cycle] < 0.3f, tag + ": the duck recovers below 0.3 before the next hit (min "
                                                           + juce::String(cycleMin[(size_t) cycle], 4) + ")");
                expect(cycleMax[(size_t) cycle] > 0.5f, tag + ": the hit does duck (max "
                                                           + juce::String(cycleMax[(size_t) cycle], 4) + ")");
            }
        }

        beginTest("a release below 30 ms behaves as 30 ms; a longer one is honoured");
        {
            // Hold a full duck (follower exactly 1), silence the source, and read the follower after 1440 samples
            // (30 ms): one time constant of a 30 ms release, e^-1. Float accumulation over 1440 steps is far
            // inside the 1e-3 tolerance; the floored cases are also bit-identical to a plain 30 ms release.
            auto envAfterSilence = [&](float releaseMs)
            {
                Rig r;
                r.src[0] = { 0.5f, true };
                r.src[1] = { 0.5f, true };                // post-trim 0.25 = kFullDuck: target 1
                r.mixer.channels[0].sidechainSource.store(1);
                r.mixer.channels[0].sidechainAmount.store(1.0f);
                r.mixer.channels[0].sidechainAttackMs.store(0.0f);
                r.mixer.channels[0].sidechainReleaseMs.store(releaseMs);
                r.run(2, 64);
                r.src[1] = { 0.0f, true };
                for (int k = 0; k < 3; ++k)
                    r.run(2, 480);
                return r.mixer.getSidechainEnv(0);
            };
            const float at30 = envAfterSilence(30.0f);
            expectWithinAbsoluteError(at30, std::exp(-1.0f), 1.0e-3f);
            // Every release under 30 ms is raised to exactly 30 ms, so the same coefficient gives the same bits.
            for (float rel : { 0.0f, 1.0f, 10.0f, 29.9f })
                expectEquals(envAfterSilence(rel), at30, "release " + juce::String(rel) + " ms is exactly the 30 ms floor");
            // A release above the floor is used as set: 1440 samples is half a 60 ms time constant, so e^-0.5.
            expectWithinAbsoluteError(envAfterSilence(60.0f), std::exp(-0.5f), 1.0e-3f);
        }
    }
};

static MixerBlockValueTests sMixerBlockValueTests;

// ─── C14: every insert algorithm reports zero latency ────────────────────────────────────────────
//
// The insert interface (InsertAlgorithmBase) has NO latency API today, and the plug-in reports no latency to
// the host. With buses added (the detachable-layer design), any latency in an insert would make delay
// compensation a standing constraint, so "zero for all" is pinned. Two guards:
//  1. a registry-level one: if any concrete algorithm (or the base) grows a getLatencySamples(), this test
//     calls it on each and requires 0, so a latency-reporting algorithm trips it;
//  2. a behavioural one: a step through every algorithm slot of the real InsertProcessor starts at sample 0.
namespace
{
    template <class T, class = void> struct HasLatencyApi : std::false_type {};
    template <class T>
    struct HasLatencyApi<T, std::void_t<decltype(std::declval<const T&>().getLatencySamples())>> : std::true_type {};

    template <class T>
    int declaredLatency(const T& algo)
    {
        if constexpr (HasLatencyApi<T>::value) return (int) algo.getLatencySamples();
        else { juce::ignoreUnused(algo); return 0; }
    }
}

struct InsertLatencyTests : juce::UnitTest
{
    InsertLatencyTests() : juce::UnitTest("Insert algorithms report zero latency", "mu-core") {}

    template <class T>
    void checkAlgorithm(const char* name, T& algo)
    {
        algo.prepare(kRate, kPrepared);
        expectEquals(declaredLatency(algo), 0, juce::String(name) + " reports latency");
    }

    void runTest() override
    {
        beginTest("the insert base interface has no latency API, and each concrete algorithm reports none or zero");
        {
            // The base has no latency API today; if one is added this expectation flags it so the next line
            // (the per-algorithm zero check, which then calls it) is known to be meaningful.
            expect(! HasLatencyApi<InsertAlgorithmBase>::value,
                   "InsertAlgorithmBase gained a latency API: C14 now asserts each algorithm returns 0 through it");

            NoneInsert a0; SoftClipInsert a1; HardClipInsert a2; FoldInsert a3; BitcrusherInsert a4; ClipperInsert a5;
            EqInsert a6; CompressorLimiterInsert a7; RingModInsert a9; TapeSatInsert a10; KarplusStrongInsert a11;
            VocoderInsert a12(false), a13(true);
            checkAlgorithm("None", a0);           checkAlgorithm("SoftClip", a1);   checkAlgorithm("HardClip", a2);
            checkAlgorithm("Fold", a3);           checkAlgorithm("Bitcrusher", a4); checkAlgorithm("Clipper", a5);
            checkAlgorithm("EQ", a6);             checkAlgorithm("Compressor/Limiter", a7);
            checkAlgorithm("RingMod", a9);        checkAlgorithm("TapeSat", a10);   checkAlgorithm("Karplus", a11);
            checkAlgorithm("Vocoder", a12);       checkAlgorithm("VocoderSt", a13);

            // The registry has 14 slots (Compressor and Limiter share one instance); the construction list
            // above must cover every name in the algorithm-name table, so a new algorithm cannot be skipped.
            int named = 0;
            while (mu_audio::kInsertAlgorithmNames[named] != nullptr) ++named;
            expectEquals(named, InsertProcessor::kNumInsertAlgos, "name table and dispatch table agree on the algorithm count");
            expectEquals(named, 14, "this list constructs 13 classes for 14 slots; a new algorithm means extending it");
        }

        beginTest("a step through every insert slot of the real processor shows its first output at sample 0");
        {
            // Instrumentation, not DSP quality: the first sample at which |out| exceeds 1e-6 on a 0.5 step
            // that starts at sample 0. Zero means no algorithm delays the signal by whole samples.
            for (int algo = 0; algo < InsertProcessor::kNumInsertAlgos; ++algo)
            {
                InsertProcessor proc;
                proc.prepare(kRate, kPrepared);
                VoiceParams p;
                p.insertAlgo = algo;
                for (int s = 0; s < VoiceParams::kInsertSlotCount; ++s) p.insertParam[s] = 0.5f;
                juce::AudioBuffer<float> buf(2, kPrepared);
                for (int c = 0; c < 2; ++c)
                    for (int i = 0; i < kPrepared; ++i) buf.setSample(c, i, 0.5f);
                proc.process(buf, kPrepared, 2, p);
                int onset = -1;
                for (int i = 0; i < kPrepared && onset < 0; ++i)
                    if (std::abs(buf.getSample(0, i)) > 1.0e-6f) onset = i;
                logMessage("insert algo " + juce::String(algo) + " (" + mu_audio::kInsertAlgorithmNames[algo]
                           + ") step onset sample " + juce::String(onset));
                // -1 (silent for the whole block) is allowed: a generator insert can need a trigger.
                // The Bitcrusher is the one known exception: its sample-rate reducer starts with an empty
                // hold cell and outputs zero until the first cell boundary (21 samples at these settings).
                // That is a cold-start artefact of the hold, not a delay line (a running signal is aligned),
                // so it is bounded here instead of excluded.
                const bool crusher = algo == 4;
                if (crusher)
                    expect(onset >= 0 && onset <= 64, "Bitcrusher cold-start hold is bounded (" + juce::String(onset) + ")");
                else
                    expect(onset <= 0, juce::String(mu_audio::kInsertAlgorithmNames[algo]) + " delays a step by " + juce::String(onset) + " samples");
            }
        }
    }

    static constexpr double kRate = 48000.0;
    static constexpr int kPrepared = 512;
};

static InsertLatencyTests sInsertLatencyTests;

// ─── Lock probe and two-thread stress ────────────────────────────────────────────────────────────
//
// The shared structural-edit path (ProcessorBase::addLayer / removeLayer / swapLayers, the strip setters)
// runs on the message thread while a product's audio thread renders under a try-lock on layersLock. This
// test reproduces that pairing with no product: a writer thread does the edits at full speed while an
// "audio" thread runs the product pattern (skip on a missed lock or an edit in flight, otherwise render
// every live layer through MixerEngine::processBlock). Fixed iteration counts, no timing assertions.
//
// Asserted: no crash, every output sample finite, every audio block completes, the layer count seen under the
// lock is always in range with a non-null layer in each live slot. Reported, NOT asserted (today's value is
// information, a later bus/render design may change it): how many blocks were silent because the lock was
// missed or an edit was in flight.
namespace
{
    struct StressLayer : Layer {};

    struct StressProcessor : ProcessorBase
    {
        StressProcessor()
            : ProcessorBase(BusesProperties().withOutput("Out", juce::AudioChannelSet::stereo()), makeLayout())
        {
            keepSpareLayers = true;
            juce::StringArray prefixes;
            for (int i = 0; i < mu_limits::kMaxLayers; ++i) prefixes.add("v" + juce::String(i) + "_");
            initLayerState(prefixes);
            for (int i = 0; i < mu_limits::kMaxLayers; ++i) layers[(size_t) i] = std::make_unique<StressLayer>();
            numLayers.store(2);
        }
        ~StressProcessor() override { for (auto& l : layers) l.reset(); }

        static juce::AudioProcessorValueTreeState::ParameterLayout makeLayout()
        {
            juce::AudioProcessorValueTreeState::ParameterLayout layout;
            for (int i = 0; i < mu_limits::kMaxLayers; ++i)
            {
                layout.add(std::make_unique<juce::AudioParameterFloat>(
                    juce::ParameterID { "v" + juce::String(i) + "_a", 1 }, "A",
                    juce::NormalisableRange<float>(0.0f, 1.0f), 0.25f));
                mu_mixfx::addChannelStripParams(layout, "ch" + juce::String(i) + "_", "Ch ");
            }
            return layout;
        }

        using ProcessorCore::layers;
        using ProcessorCore::layersLock;
        bool canEditLayers() const override { return true; }
        std::unique_ptr<Layer> createLayer(int) override { return std::make_unique<StressLayer>(); }

        juce::String getLayerName(int) const override { return {}; }
        int getLayerColourIndex(int i) const override { return layers[(size_t) i] ? layers[(size_t) i]->colourIndex : 0; }
        juce::File   getLayerPresetDir()       const override { return {}; }
        juce::String getLayerPresetExtension() const override { return ".muTest"; }
        juce::String getFullPresetExtension()  const override { return ".muTest"; }
        void applyFullMidiPreset(const juce::File&) override {}

        const juce::String getName() const override          { return "stress-test"; }
        void prepareToPlay(double, int) override             {}
        void releaseResources() override                      {}
        void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override {}
        juce::AudioProcessorEditor* createEditor() override   { return nullptr; }
        bool hasEditor() const override                       { return false; }
        bool acceptsMidi() const override                     { return false; }
        bool producesMidi() const override                    { return false; }
        double getTailLengthSeconds() const override          { return 0.0; }
        int getNumPrograms() override                         { return 1; }
        int getCurrentProgram() override                      { return 0; }
        void setCurrentProgram(int) override                  {}
        const juce::String getProgramName(int) override       { return {}; }
        void changeProgramName(int, const juce::String&) override {}
        void getStateInformation(juce::MemoryBlock&) override {}
        void setStateInformation(const void*, int) override   {}
    };
}

struct LockProbeStressTests : juce::UnitTest
{
    LockProbeStressTests() : juce::UnitTest("Structural edits against a rendering thread (lock probe, stress)", "mu-core") {}

    void runTest() override
    {
        beginTest("the render pattern's try-lock misses while an edit holds layersLock, and succeeds otherwise");
        {
            StressProcessor proc;
            std::atomic<bool> holding { false }, release { false };
            std::thread holder([&]
            {
                const juce::ScopedLock sl(proc.layersLock);
                holding.store(true);
                while (! release.load()) std::this_thread::yield();
            });
            while (! holding.load()) std::this_thread::yield();
            bool missed = false;
            {
                const juce::ScopedTryLock tl(proc.layersLock);
                missed = ! tl.isLocked();
            }
            release.store(true);
            holder.join();
            expect(missed, "a second thread cannot take layersLock while it is held");
            const juce::ScopedTryLock tl(proc.layersLock);
            expect(tl.isLocked(), "free again after the edit");
        }

        beginTest("fixed-count structural edits against a rendering thread: no crash, finite audio, every block completes");
        {
            constexpr int kBlocks = 6000;     // audio-thread blocks (64 samples each)
            constexpr int kEdits  = 240;      // writer operations

            StressProcessor proc;
            FXChain fx;
            proc.mixerEngine.prepare(48000.0, 64);
            fx.prepare(48000.0, 64);
            fx.effectSlot().setEnabled(false);
            fx.delaySlot().setEnabled(false);
            fx.reverbSlot().setEnabled(false);

            std::atomic<bool> go { false };
            std::atomic<int>  blocksDone { 0 }, rendered { 0 }, lockMisses { 0 }, editSkips { 0 };
            std::atomic<int>  badLayerCount { 0 }, nullLive { 0 }, nonFinite { 0 }, editsDone { 0 };

            std::thread audio([&]
            {
                juce::AudioBuffer<float> out(2, 64);
                MixerEngine::RenderChannelFn hook = [&](int r, juce::AudioBuffer<float>& buf, int n)
                {
                    // Reads the layer under the lock, as a product's render does.
                    const float amp = proc.layers[(size_t) r] != nullptr ? 0.125f * (float) (1 + (proc.layers[(size_t) r]->colourIndex & 3)) : -1.0f;
                    if (amp < 0.0f) { ++nullLive; return; }
                    for (int i = 0; i < n; ++i) { buf.setSample(0, i, amp * pat(i)); buf.setSample(1, i, -amp * pat(i)); }
                };
                while (! go.load()) std::this_thread::yield();
                for (int b = 0; b < kBlocks; ++b)
                {
                    out.clear();
                    {
                        const juce::ScopedTryLock tl(proc.layersLock);
                        if (! tl.isLocked()) { ++lockMisses; }
                        else if (proc.layerEditInFlight.load(std::memory_order_acquire)) { ++editSkips; }
                        else
                        {
                            const int n = proc.getNumLayers();
                            if (n < 1 || n > mu_limits::kMaxLayers) ++badLayerCount;
                            else
                            {
                                proc.mixerEngine.processBlock(out, n, nullptr, fx, 64, nullptr, nullptr, nullptr, &hook);
                                ++rendered;
                            }
                        }
                    }
                    for (int c = 0; c < 2; ++c)
                        for (int i = 0; i < 64; ++i)
                            if (! std::isfinite(out.getSample(c, i))) ++nonFinite;
                    ++blocksDone;
                }
            });

            std::thread writer([&]
            {
                while (! go.load()) std::this_thread::yield();
                for (int e = 0; e < kEdits; ++e)
                {
                    const int n = proc.getNumLayers();
                    switch (e % 5)
                    {
                        case 0: case 1: if (n < 6) proc.addLayer(); else proc.removeLayer(n - 1); break;
                        case 2:         if (n >= 2) proc.swapLayers(0, n - 1); break;
                        case 3:         if (n >= 2) proc.removeLayer(0); break;
                        default:
                            // The strip setters the message thread uses between structural edits.
                            for (int c = 0; c < mu_limits::kMaxLayers; ++c)
                            {
                                proc.mixerEngine.channels[(size_t) c].level.store(0.25f * (float) (e % 4));
                                proc.mixerEngine.channels[(size_t) c].pan.store(((float) (e % 3) - 1.0f) * 0.5f);
                                proc.mixerEngine.channels[(size_t) c].mute.store((e % 7) == 0);
                                proc.mixerEngine.channels[(size_t) c].sidechainSource.store((c + 1) % 3);
                                proc.mixerEngine.channels[(size_t) c].sidechainAmount.store(0.5f);
                            }
                            break;
                    }
                    ++editsDone;
                }
            });

            go.store(true);
            audio.join();
            writer.join();

            expectEquals(blocksDone.load(), kBlocks, "every audio block completed");
            expectEquals(editsDone.load(), kEdits, "every edit completed");
            expectEquals(nonFinite.load(), 0, "non-finite output samples");
            expectEquals(badLayerCount.load(), 0, "blocks that saw a layer count outside 1..8 under the lock");
            expectEquals(nullLive.load(), 0, "live slots found empty under the lock");
            expectEquals(rendered.load() + lockMisses.load() + editSkips.load() + badLayerCount.load(), kBlocks,
                         "each block was either rendered or accounted for as skipped");
            logMessage("lock probe: " + juce::String(kBlocks) + " blocks, " + juce::String(rendered.load()) + " rendered, "
                       + juce::String(lockMisses.load()) + " silent from a lock miss, " + juce::String(editSkips.load())
                       + " silent from an edit in flight (information, not asserted)");
            const int count = proc.getNumLayers();
            expect(count >= 1 && count <= mu_limits::kMaxLayers, "the final layer count is in range");
        }
    }
};

static LockProbeStressTests sLockProbeStressTests;
