// The Phrase layer through the shared render contract: bound to real parameters, rendered from a hand-built
// RenderContext. The loop data is swapped in from the message thread's side, a replaced loop drops the voices
// that point into the old one, a layer with no loop is silent, and the block allocates nothing.

#include "AllocationProbe.h"   // mu-core/Tests

#include "Sequencer/Phrase.h"
#include "Tests/ParamHost.h"

#include <juce_core/juce_core.h>

using namespace mu_tilate;

namespace
{
constexpr double kSr = 48000.0;
constexpr int kBlock = 256;

// A one-bar loop of four one-beat slices (at 120 BPM), slice k holding the constant (k + 1) / 10.
std::shared_ptr<PhraseData> makeData(std::initializer_list<int> placedSlices)
{
    auto d = std::make_shared<PhraseData>();
    auto audio = std::make_shared<juce::AudioBuffer<float>>(1, 96000);
    for (int k = 0; k < 4; ++k)
        for (int i = 0; i < 24000; ++i) audio->setSample(0, k * 24000 + i, (float) (k + 1) / 10.0f);
    d->audio = audio;
    d->fileSampleRate = kSr;
    d->slices = SliceList(96000);
    d->slices.divideEvenly(4);
    d->timeline = Timeline(4.0);
    double beat = 0.0;
    for (int s : placedSlices) { d->timeline.place(s, beat, 0.0); beat += 1.0; }
    return d;
}

mu_core::RenderContext contextAt(double startBeat, bool playing = true)
{
    auto c = mu_core::makeRenderContext();
    c.sampleRate = kSr; c.blockSize = kBlock; c.preparedBlockSize = kBlock;
    c.bpm = 120.0;
    c.beatsPerSample = 120.0 / 60.0 / kSr;
    c.startBeat = startBeat;
    c.callBeats = playing ? kBlock * c.beatsPerSample : 0.0;
    c.playing = playing ? 1 : 0;
    c.slot = 0;
    return c;
}

struct Rig
{
    juce::AudioProcessorValueTreeState::ParameterLayout layoutOf() { juce::AudioProcessorValueTreeState::ParameterLayout l; addLayerParams(l, 0); return l; }
    ParamHost host { layoutOf() };
    Phrase layer;
    juce::AudioBuffer<float> out { 2, kBlock };
    double beat = 0.0;

    Rig()
    {
        layer.bindParams(host.apvts, 0);
        layer.prepare(kSr, kBlock);
    }
    void setFlag(const char* suffix, float v)
    {
        auto* p = host.apvts.getParameter(layerParamPrefix(0) + suffix);
        p->setValueNotifyingHost(p->convertTo0to1(v));
    }
    // Renders one block at the running beat and advances it.
    void block(bool playing = true)
    {
        out.clear();
        layer.render(contextAt(beat, playing), out, kBlock);
        if (playing) beat += (double) kBlock * 120.0 / 60.0 / kSr;
    }
};
}

struct PhraseLayerTests : public juce::UnitTest
{
    PhraseLayerTests() : juce::UnitTest("Phrase layer", "mu-tilate") {}

    void runTest() override
    {
        beginTest("the layer names its type and its loop length");
        {
            Rig r;
            expectEquals(r.layer.typeId(), (int) mu_core::LayerTypeId::Phrase);
            expect(mu_core::layerTypeFromName("Phrase") == mu_core::LayerTypeId::Phrase);
            expect(r.layer.loopBeats() < 0.0);                      // no loop yet: nothing to wait for
            r.layer.setData(makeData({ 2, 0 }));
            expectEquals(r.layer.loopBeats(), 4.0);
            expect(r.layer.isValidDest("stretch.hold"));
            expect(! r.layer.isValidDest("stretch.curve"));
        }

        beginTest("a placed slice plays through the layer, and no loop is silence");
        {
            Rig r;
            r.block();
            expectEquals(r.out.getMagnitude(0, kBlock), 0.0f);      // no data yet
            r.layer.setData(makeData({ 2, 0 }));
            r.beat = 0.0;
            r.block(); r.block();
            expectWithinAbsoluteError(r.out.getSample(0, 100), 0.3f, 1.0e-3f);   // slice 2 at beat 0 (the insert at its default is transparent)
            expectWithinAbsoluteError(r.out.getSample(1, 100), 0.3f, 1.0e-3f);   // mono source in both channels
        }

        beginTest("a loop replaced twice between two blocks never leaks the old audio into the next one");
        {
            auto constant = [](float v)
            {
                auto d = makeData({ 0, 1, 2, 3 });
                auto a = std::make_shared<juce::AudioBuffer<float>>(1, 96000);
                for (int i = 0; i < 96000; ++i) a->setSample(0, i, v);
                d->audio = a;
                return d;
            };
            Rig r;
            r.layer.setData(constant(0.9f));
            r.block(); r.block();                                    // voices are playing the 0.9 loop
            expectGreaterThan(r.out.getMagnitude(0, kBlock), 0.8f);
            r.layer.setData(constant(0.5f));                         // two swaps, no block between: the first loop is freed, and its
            r.layer.setData(constant(0.2f));                         // address may be handed to the next data
            float peak = 0.0f;                                       // nothing sounds until the next slice starts (beat 1): run past it
            for (int i = 0; i < 110; ++i) { r.block(); peak = std::max(peak, r.out.getMagnitude(0, kBlock)); }
            expectLessOrEqual(peak, 0.2f + 1.0e-3f);                 // only the newest loop is heard
            expectGreaterThan(peak, 0.1f);
        }

        beginTest("an edit of the slices leaves the sounding slice playing; a new loop does not");
        {
            Rig r;
            auto d = makeData({ 3, 3, 3, 3 });
            r.layer.setData(d);
            r.block();
            expectGreaterThan(r.out.getMagnitude(0, kBlock), 0.3f);  // slice 3 sounds
            auto cur = r.layer.getData();
            r.layer.commitEdit(cur->slices, cur->timeline);          // same audio: the slice plays on, no dropout in the next block
            r.block();
            expectWithinAbsoluteError(r.out.getSample(0, 5), r.out.getSample(0, 6), 1.0e-3f);   // continuous, not restarted or silent
            expectGreaterThan(r.out.getMagnitude(0, kBlock), 0.3f);
        }

        beginTest("level and fades come from the parameters");
        {
            Rig r;
            r.setFlag("gain", 0.5f);
            r.layer.setData(makeData({ 1 }));
            r.block(); r.block();
            expectWithinAbsoluteError(r.out.getSample(0, 100), 0.1f, 1.0e-3f);   // slice 1 = 0.2, half level
        }

        beginTest("a replaced loop drops the voices that point into the old one");
        {
            Rig r;
            r.layer.setData(makeData({ 3 }));
            r.block(); r.block();
            expectWithinAbsoluteError(r.out.getSample(0, 100), 0.4f, 1.0e-3f);
            r.layer.setData(makeData({}));                          // an empty timeline: nothing is placed
            r.block();
            expectEquals(r.out.getMagnitude(0, kBlock), 0.0f);      // the old slice stopped, no read of freed audio
        }

        beginTest("a stop fades the sounding slice out, and the next start plays again from the top");
        {
            Rig r;
            r.layer.setData(makeData({ 0 }));
            r.block(); r.block();
            r.layer.onTransportStopped(contextAt(r.beat, false));
            r.block(false); r.block(false);
            expectEquals(r.out.getMagnitude(0, kBlock), 0.0f);
            r.beat = 0.0;
            r.block();
            expectWithinAbsoluteError(r.out.getSample(0, 100), 0.1f, 1.0e-3f);
        }

        beginTest("the tail of a layer that left its slot fades and ends");
        {
            Rig r;
            r.layer.setData(makeData({ 0 }));
            r.block(); r.block();
            mu_core::RetireState rs {};
            rs.samplesLeft = 4800; rs.gainStep = 1.0f / 4800.0f; rs.exclusive = 1; rs.maxSamples = 96000;
            r.out.clear();
            expect(r.layer.renderRetiring(contextAt(r.beat), r.out, kBlock, rs));
        }

        beginTest("the tail of a layer whose loop was replaced while it was parked does not read the old audio");
        {
            auto constant = [](float v)
            {
                auto d = makeData({ 0, 1, 2, 3 });
                auto a = std::make_shared<juce::AudioBuffer<float>>(1, 96000);
                for (int i = 0; i < 96000; ++i) a->setSample(0, i, v);
                d->audio = a;
                return d;
            };
            Rig r;
            r.layer.setData(constant(0.9f));
            r.block(); r.block();                                    // voices sounding the 0.9 loop
            r.layer.onParkedBlock(contextAt(r.beat));                // the layer is parked ...
            r.layer.setData(constant(0.2f));                         // ... and its loop is replaced (the old audio is freed)
            mu_core::RetireState rs {};
            rs.samplesLeft = 4800; rs.gainStep = 1.0f / 4800.0f; rs.exclusive = 1; rs.maxSamples = 96000;
            r.out.clear();
            expect(r.layer.renderRetiring(contextAt(r.beat), r.out, kBlock, rs));
            expectLessOrEqual(r.out.getMagnitude(0, kBlock), 0.2f + 1.0e-3f);   // the dropped voices are silent; nothing of the 0.9 loop
        }

        beginTest("a block with a loop allocates nothing");
        {
            Rig r;
            r.layer.setData(makeData({ 0, 1, 2, 3 }));
            r.block();                                              // warm-up
            mu_alloc::Armed region;
            for (int i = 0; i < 200; ++i) r.block();
            region.stop();
            expectEquals(region.allocs(), 0, "the layer's render touched the heap");
        }
    }
};

static PhraseLayerTests phraseLayerTests;
