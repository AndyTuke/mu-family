#pragma once

// The retire isolation twin test (docs/mu-tronic/design.md 2.1.4, C3), written once for every layer type or engine
// tail that retires (mu-Tant's Pattern, mu-Clid's EngineTail, the contract's fake type). Two identical processors from
// one factory render the same blocks under a mock host; the product retires the same layer (or engine) in both; then
// on ONE twin every parameter of the processor is set to a legal random value and the product's own scramble replaces
// whatever else a tail might wrongly read (modulation matrices, control sequences, envelopes). The retiring slot's
// stream as it reaches the mixer (the approved channel tap: the tail's own output before the strip, N6) must stay
// bit-identical in both twins for as long as the case renders. The product arranges that nothing else sounds in that
// channel after the retire (the slot's new layer silent, or no live layer). Header-only and product-free.

#include "GoldenRender.h"        // MockHost
#include "LayerEquivalence.h"    // ChannelStreamTap, firstDifference

#include <cstring>
#include <functional>
#include <memory>

namespace mu_contract_test
{
    struct RetireIsolationCase
    {
        std::function<std::unique_ptr<ProcessorBase>()> make;   // a fresh processor, not yet prepared
        std::function<void(ProcessorBase&)> setup;              // after prepare, before the first block (both twins)
        std::function<void(ProcessorBase&)> retire;             // the product's retire of the slot's layer or engine (both twins)
        std::function<void(ProcessorBase&)> scramble;           // optional: what else the tail must not read (one twin only)
        int      slot = 0;              // the retiring slot, whose channel stream is compared
        int      blockSize = 256;       // prepared and rendered
        double   sampleRate = 48000.0;
        double   bpm = 120.0;
        int      blocksBefore = 8;      // rendered before the retire
        int      blocksAfter = 8;       // rendered after it (the tail's life, or as much as the case needs)
        uint32_t seed = 0xC3;           // the parameter scramble's generator
    };

    struct RetireIsolationResult
    {
        bool ok = false;
        bool sounded = false;        // the slot's stream after the retire holds a non-zero sample
        int  tailFrames = 0;         // frames of the slot's stream after the retire
        juce::String message;        // what differed, when ! ok
    };

    // Every parameter of the processor to a legal random value (seeded, so a failure repeats).
    inline void scrambleParameters(ProcessorBase& p, uint32_t seed)
    {
        juce::Random r((juce::int64) seed);
        for (auto* param : p.getParameters())
            param->setValueNotifyingHost(r.nextFloat());
    }

    inline RetireIsolationResult checkRetireIsolation(const RetireIsolationCase& c)
    {
        // One twin: the processor, its playhead and its channel tap, driven block by block.
        struct Twin
        {
            Twin(const RetireIsolationCase& rc)
                : proc(rc.make()), tap((rc.blocksBefore + rc.blocksAfter) * rc.blockSize), n(rc.blockSize), rate(rc.sampleRate)
            {
                host.bpm = rc.bpm;
                proc->setPlayHead(&host);
                proc->setRateAndBufferSizeDetails(rc.sampleRate, rc.blockSize);
                proc->prepareToPlay(rc.sampleRate, rc.blockSize);
                proc->setChannelTapForTest(&tap);
                if (rc.setup) rc.setup(*proc);
            }
            ~Twin()
            {
                proc->setChannelTapForTest(nullptr);
                proc->setPlayHead(nullptr);
                proc->releaseResources();
            }
            void block()
            {
                juce::AudioBuffer<float> buf(juce::jmax(2, proc->getTotalNumOutputChannels()), n);
                juce::MidiBuffer midi;
                buf.clear();
                proc->processBlock(buf, midi);
                proc->flushPendingAsyncUpdates();   // the deferred work a host's message thread would run
                if (host.playing) host.ppq += (double) n * host.bpm / (60.0 * rate);
            }

            std::unique_ptr<ProcessorBase> proc;
            mu_golden::MockHost host;
            mu_layer_equiv::ChannelStreamTap tap;
            int n;
            double rate;
        };

        RetireIsolationResult res;
        auto a = std::make_unique<Twin>(c), b = std::make_unique<Twin>(c);
        const auto s = (size_t) c.slot;
        for (int i = 0; i < c.blocksBefore; ++i) { a->block(); b->block(); }
        const int before = a->tap.written[s];
        if (b->tap.written[s] != before) { res.message = "the twins rendered the slot differently before the retire"; return res; }

        // The retire on both; the scramble on one, after the retire and before the first tail block.
        c.retire(*a->proc);
        c.retire(*b->proc);
        scrambleParameters(*b->proc, c.seed);
        if (c.scramble) c.scramble(*b->proc);
        for (int i = 0; i < c.blocksAfter; ++i) { a->block(); b->block(); }

        res.tailFrames = a->tap.written[s] - before;
        if (b->tap.written[s] != a->tap.written[s])
        {
            res.message = "the twins rendered different tail lengths (" + juce::String(res.tailFrames) + " and "
                        + juce::String(b->tap.written[s] - before) + " frames)";
            return res;
        }
        if (a->tap.overflow || b->tap.overflow) { res.message = "a channel tap overflowed"; return res; }

        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < res.tailFrames; ++i)
                res.sounded = res.sounded || a->tap.bufs[s].getSample(ch, before + i) != 0.0f;

        for (int ch = 0; ch < 2; ++ch)
        {
            const float* x = a->tap.bufs[s].getReadPointer(ch) + before;
            const float* y = b->tap.bufs[s].getReadPointer(ch) + before;
            bool zeroSign = false;
            const int d = mu_layer_equiv::firstDifference(x, y, res.tailFrames, zeroSign);
            if (d >= 0)
            {
                res.message = "side " + juce::String(ch) + " differs " + juce::String(d) + " frames into the tail"
                            + (zeroSign ? " (sign of zero only)" : "");
                return res;
            }
        }
        res.ok = true;
        return res;
    }
}
