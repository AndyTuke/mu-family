#pragma once

// Test-only fake layer types and a fake host processor for the layer render contract (E1): the contract is
// proven on these before any product adopts it (extraction-plan 2.1, "proven on a fake type").
//
//   FakeBeatLayer   a transport-anchored layer: its output is an integer function of the ABSOLUTE frame it
//                   derives from ctx.startBeat, so any partition of the same frames into blocks or chunks must
//                   give the same samples bit for bit. It records every call and hook for the edge tests.
//   PeerReaderLayer a priority-1 layer that renders half of a peer's output (mu-On's Rumble shape).
//   FakeLayerHost   a ProcessorBase with eight slots of these, the real mixer strips, and both an "old"
//                   processor-side chain and the shared driver behind the useLayerRender switch.

#include "Plugin/ProcessorBase.h"
#include "Plugin/MixerFxParams.h"
#include "Sequencer/LayerTypeInfo.h"
#include "GoldenRender.h"
#include "AllocationProbe.h"

#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <memory>

namespace mu_contract_test
{
    constexpr uint32_t kAliveMagic = 0xA11CE5u;
    constexpr uint32_t kDeadMagic  = 0xDEADu;

    // The deterministic sound of a fake layer at absolute frame f for slot s: a sawtooth gated on and off every
    // 1500 frames, integer arithmetic only, so it does not depend on how frames are grouped into calls.
    inline float fakeSample(int64_t f, int slot, int channel) noexcept
    {
        if (f < 0) f = -f;
        const bool gateOn = ((f / 1500) % 2) == 0;
        if (! gateOn) return 0.0f;
        const int64_t period = 37 + 11 * slot + channel * 3;
        return (float) ((f % period) - period / 2) / (float) (4 * period);
    }

    // The frame a call starts at, recovered from its beat (robust to the beat's rounding by llround).
    inline int64_t absoluteFrame(const mu_core::RenderContext& ctx) noexcept
    {
        return ctx.beatsPerSample > 0.0 ? (int64_t) std::llround(ctx.startBeat / ctx.beatsPerSample) : 0;
    }

    // The fake engine both render paths share: writes [0, n) of buf from the absolute frame.
    inline void renderFakeEngine(int64_t firstFrame, int slot, juce::AudioBuffer<float>& buf, int n, float gain) noexcept
    {
        for (int c = 0; c < juce::jmin(2, buf.getNumChannels()); ++c)
        {
            float* d = buf.getWritePointer(c);
            for (int i = 0; i < n; ++i) d[i] = gain * fakeSample(firstFrame + i, slot, c);
        }
    }

    struct FakeBeatLayer : Layer
    {
        FakeBeatLayer() = default;
        ~FakeBeatLayer() override { magic = kDeadMagic; }

        int typeId() const override { return (int) mu_core::LayerTypeId::Arp; }
        void prepare(double sr, int bs) override { preparedRate = sr; preparedBlock = bs; ++prepares; }
        void bindParams(juce::AudioProcessorValueTreeState& apvts, int slot) override
        {
            boundSlot = slot;
            ++binds;
            gainParam = mu_core::ParamBinding(apvts.getRawParameterValue("v" + juce::String(slot) + "_a"), this);
        }

        void render(const mu_core::RenderContext& ctx, juce::AudioBuffer<float>& out, int n) override
        {
            if (magic != kAliveMagic) ++useAfterFree;
            ++renders;
            maxCall = juce::jmax(maxCall, n);
            if (n > ctx.preparedBlockSize || n != ctx.blockSize || ctx.frameOffset != ctx.midi.firstFrame) ++contractBreaks;
            lastSlot = ctx.slot;
            justStarted += ctx.justStarted;
            justStopped += ctx.justStopped;
            jumped      += ctx.transportJumped;
            hiddenStops += ctx.stoppedUnrendered;
            lastStartBeat = ctx.startBeat;
            if (ctx.beatWrap > 0.0 && (ctx.startBeat < 0.0)) ++negativeInBoundedHost;
            mu_core::forEachMidi(ctx, [this](const mu_core::MidiEvent&, int) { ++midiSeen; });
            lastCarried = ctx.midi.carried;
            if (ctx.midi.count > 0) firstEventNote = ctx.midi.events[0].bytes[1];
            if (! ctx.playing) return;   // stopped: silence (the scratch arrives cleared)
            const float g = gainParam.isBound() ? 0.25f + gainParam.load() : 0.5f;
            renderFakeEngine(absoluteFrame(ctx), ctx.slot, out, n, g);
            if (emitNoteOn && n > 0 && ctx.emitMidi(0, 0x90, 60, 100)) ++midiEmitted;
        }

        int priority = 0;
        bool flipPriority = false;   // every read of the priority returns the other value (a type whose priority follows a parameter)
        mutable int priorityReads = 0;
        int renderPriority() const override { return flipPriority ? (priorityReads++ % 2) : priority; }
        double loopBeatsValue = 4.0;
        double loopBeats() const override { return loopBeatsValue; }
        void onTransportStopped(const mu_core::RenderContext& ctx) override { ++stopHooks; hookSlot = ctx.slot; }
        void onTransportJump(const mu_core::RenderContext& ctx) override    { ++jumpHooks; hookSlot = ctx.slot; }
        void onMoved(int i) override { movedTo = i; }
        void onParkedBlock(const mu_core::RenderContext& ctx) override
        {
            ++parkedCalls;
            parkedSlot = ctx.slot;
            mu_core::forEachMidi(ctx, [this](const mu_core::MidiEvent&, int) { ++parkedMidiSeen; });
        }

        uint32_t magic = kAliveMagic;
        mu_core::ParamBinding gainParam;
        double preparedRate = 0.0;
        int preparedBlock = 0, prepares = 0, binds = 0, boundSlot = -1, movedTo = -1, hookSlot = -1;
        int renders = 0, maxCall = 0, contractBreaks = 0, lastSlot = -1, useAfterFree = 0;
        int justStarted = 0, justStopped = 0, jumped = 0, hiddenStops = 0, stopHooks = 0, jumpHooks = 0;
        int midiSeen = 0, midiEmitted = 0, negativeInBoundedHost = 0;
        int lastCarried = 0, firstEventNote = -1, parkedCalls = 0, parkedSlot = -1, parkedMidiSeen = 0;
        bool emitNoteOn = false;
        double lastStartBeat = 0.0;
    };

    // Renders half of slot `source`'s output (silence when the peer is absent): a priority-1 reader.
    struct PeerReaderLayer : FakeBeatLayer
    {
        explicit PeerReaderLayer(int src) : source(src) { priority = 1; }
        void render(const mu_core::RenderContext& ctx, juce::AudioBuffer<float>& out, int n) override
        {
            ++renders;
            const auto* p = ctx.peer(source);
            if (p == nullptr) { ++peerMissing; return; }
            for (int c = 0; c < 2; ++c)
            {
                float* d = out.getWritePointer(c);
                for (int i = 0; i < n; ++i) d[i] = 0.5f * p->channel[c][i];
            }
        }
        int source;
        int peerMissing = 0;
    };

    // A ProcessorBase with eight fake slots, the real mixer-strip parameters and a gain parameter per slot.
    struct FakeLayerHost : ProcessorBase
    {
        // `sidechainInput`: a stereo sidechain input bus sharing the output's buffer channels (an instrument's shape).
        explicit FakeLayerHost(int live = 2, mu_core::LayerAccess access = mu_core::LayerAccess::TryLock, bool sidechainInput = false)
            : FakeLayerHost(live, access, sidechainInput, makeLayout())
        {
        }

        // The same host over a parameter layout of the test's own (a second type prefix, say).
        FakeLayerHost(int live, mu_core::LayerAccess access, bool sidechainInput, juce::AudioProcessorValueTreeState::ParameterLayout layout)
            : ProcessorBase(sidechainInput ? BusesProperties().withInput("Sidechain", juce::AudioChannelSet::stereo(), true)
                                                              .withOutput("Out", juce::AudioChannelSet::stereo())
                                           : BusesProperties().withOutput("Out", juce::AudioChannelSet::stereo()),
                            std::move(layout))
        {
            setLayerAccess(access);
            keepSpareLayers = true;
            juce::StringArray prefixes;
            for (int i = 0; i < mu_limits::kMaxLayers; ++i) prefixes.add("v" + juce::String(i) + "_");
            initLayerState(prefixes);
            for (int i = 0; i < mu_limits::kMaxLayers; ++i)
            {
                layers[(size_t) i] = std::make_unique<FakeBeatLayer>();
                layers[(size_t) i]->bindParams(apvts, i);
            }
            {
                const juce::ScopedLock lw(layoutWriteLock);
                storeLayerCount(live);
            }
            startFxParamSync();
        }
        ~FakeLayerHost() override
        {
            prepareForDestruction();
            for (auto& l : layers) l.reset();
        }

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

        FakeBeatLayer& fake(int i) { return static_cast<FakeBeatLayer&>(*layers[(size_t) i]); }

        // Test surface over the protected contract.
        using ProcessorCore::layers;
        using ProcessorCore::layersLock;
        using ProcessorCore::layoutWriteLock;
        using ProcessorCore::layerAccess;
        using ProcessorCore::storeLayerCount;
        using ProcessorCore::nextLayoutSerial;
        using ProcessorCore::layoutSerial;
        using ProcessorBase::retireBin;
        using ProcessorBase::applyComposedState;
        using ProcessorBase::captureComposedState;
        using ProcessorBase::mergeLayerMidiOut;
        using ProcessorBase::setRetireCountPolicy;
        using ProcessorBase::setCarrySkippedMidi;
        using ProcessorBase::midiInRaw;
        using ProcessorBase::setRegrowCutsInPlaceTails;
        using ProcessorBase::layerLoopBeats;
        using ProcessorBase::flushPendingAsyncUpdates;
        using ProcessorBase::layerSwapper;

        // The driver called directly (no product ScopedNoDenormals, no resolver), as a stress thread does.
        void renderAndMixForTest(juce::AudioBuffer<float>& buf, juce::MidiBuffer& m, const mu_core::BlockTransport& t) noexcept
        {
            renderAndMix(buf, m, t);
        }

        bool canEditLayers() const override { return editable; }
        std::unique_ptr<Layer> createLayer(int) override { return std::make_unique<FakeBeatLayer>(); }
        bool editable = true;

        double beatWrap = 0.0;          // the host bound the resolver wraps with (BlockTransport::beatWrap)
        bool   lastFlagged = false;
        mu_core::BlockTransport lastTransport;

        void prepareToPlay(double sr, int bs) override
        {
            mixerEngine.prepare(sr, bs);
            fxChain.prepare(sr, bs);
            prepareLayerRender(sr, bs);
            preparedSize = bs;
        }
        void releaseResources() override { releaseLayerRender(); }

        void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override
        {
            juce::ScopedNoDenormals noDenormals;
            const int n = buffer.getNumSamples();
            if (! usesLayerRender()) captureSidechainAndClear(buffer);   // the driver captures per chunk itself
            lastTransport = resolveBlockTransport(midi, n, beatWrap);
            if (usesLayerRender()) renderAndMix(buffer, midi, lastTransport);
            else                   renderOldPath(buffer, lastTransport, n);
            flagLayerSwapBoundaries(lastTransport);
        }

        // The "processor-side chain" a product has before it adopts the driver: try-lock, then the mixer with a
        // render hook that runs the same fake engine directly. Only valid for blocks up to the prepared size
        // (as every product's chain today).
        void renderOldPath(juce::AudioBuffer<float>& buffer, const mu_core::BlockTransport& t, int n)
        {
            const juce::ScopedTryLock tl(layersLock);
            if (! tl.isLocked() || layerEditInFlight.load(std::memory_order_acquire)) return;
            oldT = t;
            processCoreBlock(buffer, nullptr, numLayers.load(std::memory_order_relaxed), n, t.bpm,
                             nullptr, nullptr, nullptr, &oldHook);
        }
        mu_core::BlockTransport oldT;
        MixerEngine::RenderChannelFn oldHook = [this](int r, juce::AudioBuffer<float>& buf, int n)
        {
            if (! oldT.playing) return;
            auto& l = fake(r);
            const float g = l.gainParam.isBound() ? 0.25f + l.gainParam.load() : 0.5f;
            const int64_t f0 = oldT.beatsPerSample > 0.0 ? (int64_t) std::llround(oldT.startBeat / oldT.beatsPerSample) : 0;
            renderFakeEngine(f0, r, buf, n, g);
        };

        int preparedSize = 0;
        juce::String getLayerName(int) const override { return {}; }
        int getLayerColourIndex(int i) const override { return layers[(size_t) i] ? layers[(size_t) i]->colourIndex : 0; }
        juce::File   getLayerPresetDir()       const override { return {}; }
        juce::String getLayerPresetExtension() const override { return ".muTest"; }
        juce::String getFullPresetExtension()  const override { return ".muTest"; }
        void applyFullMidiPreset(const juce::File&) override {}

        const juce::String getName() const override          { return "contract-test"; }
        juce::AudioProcessorEditor* createEditor() override   { return nullptr; }
        bool hasEditor() const override                       { return false; }
        bool acceptsMidi() const override                     { return true; }
        bool producesMidi() const override                    { return false; }
        double getTailLengthSeconds() const override          { return 0.0; }
        int getNumPrograms() override                         { return 1; }
        int getCurrentProgram() override                      { return 0; }
        void setCurrentProgram(int) override                  {}
        const juce::String getProgramName(int) override       { return {}; }
        void changeProgramName(int, const juce::String&) override {}
        void getStateInformation(juce::MemoryBlock& mb) override
        {
            if (auto xml = captureComposedState().createXml()) copyXmlToBinary(*xml, mb);
        }
        void setStateInformation(const void* d, int n) override
        {
            if (auto xml = getXmlFromBinary(d, n)) applyComposedState(juce::ValueTree::fromXml(*xml));
        }
    };

    // Drives a FakeLayerHost block by block with a mock host playhead.
    struct Driver
    {
        explicit Driver(FakeLayerHost& p, double sr = 48000.0, int prepared = 512) : proc(p), rate(sr)
        {
            proc.setPlayHead(&host);
            proc.setRateAndBufferSizeDetails(sr, prepared);
            proc.prepareToPlay(sr, prepared);
            work.setSize(2, 8192);
        }
        ~Driver() { proc.setPlayHead(nullptr); proc.releaseResources(); }

        // Renders one host block of n frames at the host's current position, then advances the host if playing.
        const juce::AudioBuffer<float>& block(int n)
        {
            if (n > work.getNumSamples()) work.setSize(2, n);
            view.setDataToReferTo(work.getArrayOfWritePointers(), 2, 0, n);
            view.clear();
            if (fillInput) fillInput(view, frame);   // what the host's input bus carries (a sidechain)
            midi.clear();
            for (const auto& m : pendingMidi) midi.addEvent(m, juce::jmin(n - 1, midiOffset));
            pendingMidi.clear();
            if (countAllocations)
            {
                mu_alloc::Armed armed;
                proc.processBlock(view, midi);
                armed.stop();
                allocations += armed.allocs();
                frees += armed.frees();
            }
            else
                proc.processBlock(view, midi);
            if (host.playing && ! freezePosition) host.ppq += (double) n * host.bpm / (60.0 * rate);
            frame += n;
            return view;
        }

        FakeLayerHost& proc;
        double rate;
        mu_golden::MockHost host;
        juce::AudioBuffer<float> work, view;
        juce::MidiBuffer midi;
        std::vector<juce::MidiMessage> pendingMidi;
        int  midiOffset = 0;
        juce::int64 frame = 0;           // host frames rendered so far
        bool freezePosition = false;     // the host reports playing but its position does not move
        std::function<void(juce::AudioBuffer<float>&, juce::int64)> fillInput;
        bool countAllocations = false;   // count heap use inside processBlock (AllocationProbe)
        int  allocations = 0, frees = 0;
    };

    // A channel tap that appends each channel's stream (allocates nothing on the audio thread: sized up front).
    struct AppendTap : MixerEngine::ChannelTap
    {
        explicit AppendTap(int frames) { for (auto& b : bufs) b.setSize(2, frames); written.fill(0); maxCall = 0; }
        void capture(int ch, const juce::AudioBuffer<float>& buf, int n) noexcept override
        {
            maxCall = juce::jmax(maxCall, n);
            if (ch < 0 || ch >= MixerEngine::MaxChannels) return;
            auto& dst = bufs[(size_t) ch];
            const int at = written[(size_t) ch];
            const int len = juce::jmin(n, dst.getNumSamples() - at);
            for (int c = 0; c < 2 && c < buf.getNumChannels(); ++c)
                if (len > 0) juce::FloatVectorOperations::copy(dst.getWritePointer(c) + at, buf.getReadPointer(c), len);
            written[(size_t) ch] = at + juce::jmax(0, len);
        }
        std::array<juce::AudioBuffer<float>, MixerEngine::MaxChannels> bufs;
        std::array<int, MixerEngine::MaxChannels> written {};
        int maxCall = 0;
    };
}
