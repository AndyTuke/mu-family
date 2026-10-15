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

    // What a channel renders: a patterned stereo ramp (right = -left / 2) or a constant on both sides.
    struct Src
    {
        float scale    = 0.0f;
        bool  constant = false;
        float left(int i)  const { return constant ? scale : scale * pat(i); }
        float right(int i) const { return constant ? scale : -0.5f * scale * pat(i); }
    };

    // A mixer with the three FX slots disabled (they pass a send bus through untouched, so a return is the
    // send times the return fader and pan) and a render hook that plays the configured sources.
    struct Rig
    {
        MixerEngine mixer;
        FXChain     fx;
        juce::AudioBuffer<float> out { 2, kPrepared };
        std::array<Src, MixerEngine::MaxChannels> src {};
        MixerEngine::RenderChannelFn render;

        Rig()
        {
            mixer.prepare(kRate, kPrepared);
            fx.prepare(kRate, kPrepared);
            fx.effectSlot().setEnabled(false);
            fx.delaySlot().setEnabled(false);
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
    };

    // The fader/pan law of the mixer: left falls as pan moves right, right falls as pan moves left.
    float gainL(float level, float pan) { return level * (1.0f - std::max(0.0f, pan)); }
    float gainR(float level, float pan) { return level * (1.0f + std::min(0.0f, pan)); }

    // Post-trim, pre-fader channel sample (the sidechain tap) and post-fader sample.
    float trimmed(float x) { return x * MixerEngine::kHeadroomTrim; }
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
        muteSoloTruthTable();
        sidechainTap();
        sidechainOrder();
        sidechainEnvelope();
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

    // Effect/delay/reverb sends and returns. The slots are disabled, so a return is send x return fader/pan.
    void sends()
    {
        beginTest("sends are post-fader, post-pan; a return is the send bus times the return fader and pan");
        for (int n : kSizes)
        {
            Rig r;
            r.src[0] = { 0.5f, false };
            r.mixer.channels[0].level.store(0.5f);
            r.mixer.channels[0].pan.store(0.5f);
            r.mixer.channels[0].sendEffect.store(0.5f);
            r.mixer.channels[0].sendDelay.store(0.25f);
            r.mixer.channels[0].sendReverb.store(1.0f);
            r.mixer.returns[0].level.store(1.0f);
            r.mixer.returns[1].level.store(0.5f);
            r.mixer.returns[2].level.store(0.25f);
            r.mixer.returns[2].pan.store(-0.5f);
            juce::AudioBuffer<float> fxOut(2, kPrepared);
            fxOut.clear();
            r.run(1, n, nullptr, &fxOut);

            auto dryL = [&](int i) { return trimmed(r.src[0].left(i))  * gainL(0.5f, 0.5f); };
            auto dryR = [&](int i) { return trimmed(r.src[0].right(i)) * gainR(0.5f, 0.5f); };
            auto retL = [&](int i) { return (dryL(i) * 0.5f) * gainL(1.0f, 0.0f); };
            auto retR = [&](int i) { return (dryR(i) * 0.5f) * gainR(1.0f, 0.0f); };
            auto dlyL = [&](int i) { return (dryL(i) * 0.25f) * gainL(0.5f, 0.0f); };
            auto dlyR = [&](int i) { return (dryR(i) * 0.25f) * gainR(0.5f, 0.0f); };
            auto revL = [&](int i) { return (dryL(i) * 1.0f) * gainL(0.25f, -0.5f); };
            auto revR = [&](int i) { return (dryR(i) * 1.0f) * gainR(0.25f, -0.5f); };
            expectChannel(r.out, 0, n, [&](int i) { return ((dryL(i) + retL(i)) + dlyL(i)) + revL(i); }, "master L");
            expectChannel(r.out, 1, n, [&](int i) { return ((dryR(i) + retR(i)) + dlyR(i)) + revR(i); }, "master R");
            expectChannel(fxOut, 0, n, [&](int i) { return (retL(i) + dlyL(i)) + revL(i); }, "fx-returns bus L");
            expectChannel(fxOut, 1, n, [&](int i) { return (retR(i) + dlyR(i)) + revR(i); }, "fx-returns bus R");
        }

        beginTest("send modulation replaces the knob; several strips add into one send bus");
        {
            Rig r;
            r.src[0] = { 0.5f, true };
            r.src[1] = { 0.25f, true };
            r.mixer.channels[0].sendEffect.store(0.0f);
            r.mixer.channels[0].sendEffectMod.store(0.5f);   // modulated on
            r.mixer.channels[1].sendEffect.store(1.0f);
            r.mixer.channels[1].sendEffectMod.store(0.0f);   // modulated off (0 is a value, not "unmodulated")
            r.mixer.returns[0].level.store(1.0f);
            r.run(2, 64);
            expectChannel(r.out, 0, 64, [&](int) { return (trimmed(0.5f) + trimmed(0.25f)) + (trimmed(0.5f) * 0.5f); }, "L");
        }

        beginTest("a muted return is not heard; a muted strip feeds no send");
        {
            Rig r;
            r.src[0] = { 0.5f, true };
            r.mixer.channels[0].sendReverb.store(1.0f);
            r.mixer.returns[2].level.store(1.0f);
            r.mixer.returns[2].mute.store(true);
            r.run(1, 64);
            expectChannel(r.out, 0, 64, [&](int) { return trimmed(0.5f); }, "return muted");
            expectEquals(r.mixer.returnPeaks[2].load(), 0.0f, "return peak cleared");
            r.mixer.returns[2].mute.store(false);
            r.mixer.channels[0].mute.store(true);
            r.run(1, 64);
            expectChannel(r.out, 0, 64, [](int) { return 0.0f; }, "strip muted: no dry and no wet");
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
                Rig r;
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
            // Strip 0 ducks from strip 1 with instant attack and release, so the follower is a gate.
            r.src[0] = { 0.5f, true };
            r.src[1] = { 0.5f, true };
            r.mixer.channels[0].sidechainSource.store(1);
            r.mixer.channels[0].sidechainAmount.store(duckDepth);
            r.mixer.channels[0].sidechainAttackMs.store(0.0f);
            r.mixer.channels[0].sidechainReleaseMs.store(0.0f);
        };

        beginTest("sidechain ducks the target by amount while the source is above the threshold, whatever the source fader");
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

        beginTest("sidechain source below the threshold does not duck");
        {
            Rig r;
            duckPair(r);
            r.src[1] = { 0.001f, true };                // post-trim 0.0005 < 0.001
            r.run(2, 64);
            expectChannel(r.out, 0, 64, [&](int) { return trimmed(0.5f) + trimmed(0.001f); }, "L");
            expectEquals(r.mixer.sidechainGR[0].load(), 0.0f, "GR meter");
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
            Rig r;
            r.src[0] = { 0.5f, true };
            r.mixer.channels[0].sendEffect.store(1.0f);
            r.mixer.returns[0].level.store(1.0f);
            r.mixer.returns[0].sidechainSource.store(0);
            r.mixer.returns[0].sidechainAmount.store(0.5f);
            r.mixer.returns[0].sidechainAttackMs.store(0.0f);
            r.mixer.returns[0].sidechainReleaseMs.store(0.0f);
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

        beginTest("mutual ducking, quiet strip first: strip 1 reads strip 0 AFTER it was ducked below threshold, so it is not ducked");
        {
            Rig r;
            mutual(r, kQuiet, 0.5f);
            r.run(2, 64);
            // Strip 0 (post-trim 2^-9, above threshold) ducks from strip 1 (loud): 2^-10. Strip 1 then reads
            // strip 0 at 2^-10 = 0.000977, under the 0.001 threshold: no duck. A pre-duck snapshot would duck it.
            expectChannel(r.out, 0, 64, [&](int) { return trimmed(kQuiet) * (1.0f - depth) + trimmed(0.5f); }, "mix");
            expectEquals(r.mixer.sidechainGR[0].load(), depth, "strip 0 GR");
            expectEquals(r.mixer.sidechainGR[1].load(), 0.0f, "strip 1 GR (order dependent)");
        }

        beginTest("mutual ducking, loud strip first: both are ducked (the source stays above threshold after its own duck)");
        {
            Rig r;
            mutual(r, 0.5f, kQuiet);
            r.run(2, 64);
            // Strip 0 ducks from strip 1 (post-trim 2^-9 > threshold): 0.25 -> 0.125. Strip 1 reads strip 0 at
            // 0.125 (above threshold): ducked 2^-9 -> 2^-10.
            expectChannel(r.out, 0, 64, [&](int) { return trimmed(0.5f) * (1.0f - depth) + trimmed(kQuiet) * (1.0f - depth); }, "mix");
            expectEquals(r.mixer.sidechainGR[0].load(), depth, "strip 0 GR");
            expectEquals(r.mixer.sidechainGR[1].load(), depth, "strip 1 GR");
        }

        beginTest("a source with a higher index is read un-ducked; one with a lower index is read already ducked");
        {
            // Strip 0 ducks from 2 (higher: un-ducked) and strip 2 ducks from 0 (lower: already ducked). Both
            // are quiet-then-loud so the lower strip's duck decides the higher strip's fate.
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
            expectEquals(r.mixer.sidechainGR[0].load(), depth, "strip 0 (reads strip 2 un-ducked)");
            expectEquals(r.mixer.sidechainGR[2].load(), 0.0f, "strip 2 (reads strip 0 already ducked below threshold)");
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

        beginTest("ducking is deterministic across block sizes when the envelope is a gate");
        for (int n : kSizes)
        {
            Rig r;
            mutual(r, kQuiet, 0.5f);
            r.run(2, n);
            expectChannel(r.out, 0, n, [&](int) { return trimmed(kQuiet) * (1.0f - depth) + trimmed(0.5f); }, "mix");
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
