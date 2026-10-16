#pragma once

// Layer-vs-processor equivalence (tests.md section 3; extraction-plan 2.7a): while a stage moves a product's
// render chain, the old processor-side chain and Layer::render coexist behind ProcessorBase's useLayerRender
// switch. This builds two identical processors from one factory and scenario, turns the switch on in the
// second, drives both with the same block schedule, scripted events and MIDI, and requires the master output
// of every block and every channel's pre-mixer stream (the approved test-only channel tap) to be equal bit for
// bit. Both paths run the same compiled maths, so tolerance is 0 with no exceptions, a sign of zero included.
//
// After a stage the same harness is the mu-Tronic test of design section 7 (a slot of type X equals the
// product's own layer). Header-only and product-free; the scenario description is GoldenRender.h's.

#include "GoldenRender.h"

#include <array>
#include <cstring>
#include <memory>

namespace mu_layer_equiv
{
    // Appends each channel's stream as the channel hands it to the mixer. Sized before rendering, so the
    // capture on the audio thread allocates nothing.
    struct ChannelStreamTap : MixerEngine::ChannelTap
    {
        explicit ChannelStreamTap(int frames) { for (auto& b : bufs) b.setSize(2, juce::jmax(1, frames)); }
        void capture(int ch, const juce::AudioBuffer<float>& buf, int n) noexcept override
        {
            if (ch < 0 || ch >= MixerEngine::MaxChannels) return;
            auto& dst = bufs[(size_t) ch];
            const int at = written[(size_t) ch];
            const int len = juce::jmin(n, dst.getNumSamples() - at);
            if (len <= 0) { overflow = true; return; }
            for (int c = 0; c < 2 && c < buf.getNumChannels(); ++c)
                juce::FloatVectorOperations::copy(dst.getWritePointer(c) + at, buf.getReadPointer(c), len);
            written[(size_t) ch] = at + len;
        }
        std::array<juce::AudioBuffer<float>, MixerEngine::MaxChannels> bufs;
        std::array<int, MixerEngine::MaxChannels> written {};
        bool overflow = false;
    };

    struct Result
    {
        bool ok = true;
        juce::String message;   // the first difference, when !ok
        int blocks = 0;
    };

    // Where two float runs first differ (bit patterns), -1 when equal; `zeroSignOnly` is true when every
    // difference up to and including the first is a zero of the opposite sign (inaudible, still a failure).
    inline int firstDifference(const float* a, const float* b, int n, bool& zeroSignOnly) noexcept
    {
        zeroSignOnly = false;
        for (int i = 0; i < n; ++i)
            if (std::memcmp(a + i, b + i, sizeof (float)) != 0)
            {
                zeroSignOnly = a[i] == 0.0f && b[i] == 0.0f;
                return i;
            }
        return -1;
    }

    // Renders `sched` over the scenario's length with processor A (switch off, the old chain) and B (switch on,
    // Layer::render), comparing master per block and `numChannels` channel streams at the end.
    inline Result compare(const mu_golden::ProcessorFactory& factory, const mu_golden::Scenario& sc,
                          const mu_golden::Schedule& sched, int numChannels)
    {
        Result res;
        auto a = factory();
        auto b = factory();
        if (sc.loadState) { sc.loadState(*a); sc.loadState(*b); }
        b->setUseLayerRenderForTest(true);

        ChannelStreamTap tapA((int) sc.lengthFrames), tapB((int) sc.lengthFrames);
        a->setChannelTapForTest(&tapA);
        b->setChannelTapForTest(&tapB);

        mu_golden::MockHost hostA, hostB;
        for (auto* h : { &hostA, &hostB }) { h->bpm = sc.tempoBpm; h->ppq = sc.startBeat; }
        a->setPlayHead(&hostA);
        b->setPlayHead(&hostB);
        for (auto* p : { a.get(), b.get() })
        {
            p->setRateAndBufferSizeDetails(sc.sampleRate, sc.preparedBlockSize);
            p->prepareToPlay(sc.sampleRate, sc.preparedBlockSize);
        }

        const int maxBlock = *std::max_element(sched.blocks.begin(), sched.blocks.end());
        juce::AudioBuffer<float> workA(2, maxBlock), workB(2, maxBlock);
        juce::MidiBuffer midiA, midiB;
        auto events = sc.events;
        std::stable_sort(events.begin(), events.end(), [](const auto& x, const auto& y) { return x.block < y.block; });
        size_t cursor = 0;

        // One iteration per host block: the same events and MIDI to both, then both render and the masters compare.
        juce::int64 frame = 0;
        int block = 0;
        while (frame < sc.lengthFrames && res.ok)
        {
            const int n = (int) std::min<juce::int64>(sched.blocks[(size_t) block % sched.blocks.size()], sc.lengthFrames - frame);
            hostA.timeInSamples = hostB.timeInSamples = frame;
            while (cursor < events.size() && events[cursor].block <= block)
            {
                mu_golden::ScriptContext ca { *a, hostA, block, frame }, cb { *b, hostB, block, frame };
                events[cursor].apply(ca);
                events[cursor].apply(cb);
                ++cursor;
            }
            midiA.clear();
            midiB.clear();
            for (const auto& m : sc.midi)
                if (m.block == block)
                {
                    midiA.addEvent(m.message, juce::jmin(m.sampleOffset, n - 1));
                    midiB.addEvent(m.message, juce::jmin(m.sampleOffset, n - 1));
                }
            juce::AudioBuffer<float> va(workA.getArrayOfWritePointers(), 2, n), vb(workB.getArrayOfWritePointers(), 2, n);
            va.clear();
            vb.clear();
            a->processBlock(va, midiA);
            b->processBlock(vb, midiB);
            for (int c = 0; c < 2 && res.ok; ++c)
            {
                bool zeroSign = false;
                const int d = firstDifference(va.getReadPointer(c), vb.getReadPointer(c), n, zeroSign);
                if (d >= 0)
                {
                    res.ok = false;
                    res.message = "master ch" + juce::String(c) + " differs in block " + juce::String(block) + " at frame "
                                + juce::String(frame + d) + (zeroSign ? " (sign of zero only, inaudible, still a failure)" : "");
                }
            }
            for (auto* h : { &hostA, &hostB })
                if (h->playing) h->ppq += (double) n * h->bpm / (60.0 * sc.sampleRate);
            frame += n;
            ++block;
            res.blocks = block;
        }

        // Each channel's pre-mixer stream, whole render.
        for (int ch = 0; ch < numChannels && ch < MixerEngine::MaxChannels && res.ok; ++ch)
        {
            if (tapA.written[(size_t) ch] != tapB.written[(size_t) ch])
            {
                res.ok = false;
                res.message = "channel " + juce::String(ch) + " handed the mixer " + juce::String(tapA.written[(size_t) ch])
                            + " frames on the old path and " + juce::String(tapB.written[(size_t) ch]) + " on the layer path";
                break;
            }
            for (int c = 0; c < 2 && res.ok; ++c)
            {
                bool zeroSign = false;
                const int d = firstDifference(tapA.bufs[(size_t) ch].getReadPointer(c), tapB.bufs[(size_t) ch].getReadPointer(c),
                                              tapA.written[(size_t) ch], zeroSign);
                if (d >= 0)
                {
                    res.ok = false;
                    res.message = "channel " + juce::String(ch) + " side " + juce::String(c) + " differs at stream frame " + juce::String(d)
                                + (zeroSign ? " (sign of zero only, inaudible, still a failure)" : "");
                }
            }
        }
        if (res.ok && (tapA.overflow || tapB.overflow)) { res.ok = false; res.message = "a channel tap overflowed (size it to the scenario)"; }

        for (auto* p : { a.get(), b.get() }) { p->setChannelTapForTest(nullptr); p->setPlayHead(nullptr); p->releaseResources(); }
        return res;
    }
}
