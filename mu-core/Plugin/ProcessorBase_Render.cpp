// The shared block driver of the layer render contract: renderAndMix, renderLayers and the transport
// latches. docs/mu-tronic/design.md 2.1.1 (the shape of one block), 2.1.5 (the time model).
//
// Real-time rules every function here keeps (audio thread unless noted): no allocation (all storage is
// sized by prepareLayerRender), no lock but the one LayerBlock try-lock in TryLock mode, no layer pointer
// kept past the block, no call that can block. The message-thread work the audio thread needs (freeing a
// tail that went Done) is requested through the deferred-work mailbox, an atomic flag.

#include "Plugin/ProcessorBase.h"
#include "Sequencer/BeatWrap.h"

namespace
{
    constexpr double kDefaultLoopBeats = 4.0;          // a slot with no live layer: the swapper's 4/4 bar rule
    constexpr double kJumpToleranceBeats = 1.0 / 256.0;  // the continuity check's base tolerance (design 2.1.6, point 1)
    constexpr int    kMidiBytesPerEvent = 6;           // MidiBuffer's storage per event: int32 time + uint16 size + bytes
}

void ProcessorBase::prepareLayerRender(double sampleRate, int maxBlockSize)
{
    // Message or host thread; the host is not processing. Every allocation of the driver happens here.
    lr.rate     = sampleRate;
    lr.prepared = juce::jmax(1, maxBlockSize);
    for (auto& s : lr.scratch) s.setSize(2, lr.prepared, false, true, false);
    if (lr.midi.get() == nullptr) lr.midi.allocate((size_t) mu_limits::kMaxBlockMidiEvents, true);
    lr.loopBeats.fill(kDefaultLoopBeats);
    lr.live.fill(false);
    lr.host = {};
    lr.lastPlaying = lr.jumpPending = false;
    lr.edgeStarted = lr.edgeStopped = lr.edgeJumped = false;
    lr.mixCount = 0;
    lr.midiIn = lr.liveOut = lr.tailOut = 0;
    lr.dropped = 0;

    // Every slot's layer, spares included, for this rate and size. layersLock keeps a structural edit (message
    // thread) from moving them meanwhile; the audio thread is not running.
    {
        const juce::ScopedLock sl(layersLock);
        for (auto& l : layers)
            if (l != nullptr) l->prepare(sampleRate, lr.prepared);
    }

    // The bin never crosses a prepare: everything still sounding is cut and freed (nothing is processing,
    // so every grace has passed), then it converts seconds with the new rate.
    retireBin.setPrepared(sampleRate, lr.prepared);
    retireBin.cutAll();
    retireBin.service();
}

void ProcessorBase::releaseLayerRender()
{
    retireBin.cutAll();
    retireBin.service();
    lr.host = {};
    lr.lastPlaying = lr.jumpPending = false;
}

mu_core::BlockTransport ProcessorBase::resolveBlockTransport(juce::MidiBuffer& midi, int numSamples, double wrapBeats)
{
    const double sr = lr.rate > 0.0 ? lr.rate : getSampleRate();
    const double clockBeat = midiClockSync.process(midi, numSamples, sr);
    const auto t = mu_core::resolveTransport(pollHostTransport(), wrapperType == wrapperType_Standalone,
                                             midiClockSync, clockBeat,
                                             { internalPlaying, internalBpm, internalBeatPos, &nudgePhaseBeats,
                                               &nudgeDirection, &appliedSyncOffsetBeats },
                                             numSamples, sr, wrapBeats, getSyncOffsetMs());
    noteBlockTransport(t);
    return t;
}

// Host-block continuity (design 2.1.5, R5): every host block, rendered or not, is compared with the one
// before it, so the block after a missed lock or a structural edit sees one block's worth of beats, not a
// jump. A real discontinuity sets the sticky jumpPending, which the next rendered block delivers once.
void ProcessorBase::noteHostBlock(const mu_core::BlockTransport& t, int numSamples, double beatWrap) noexcept
{
    const double bpsNow   = t.bpm / 60.0;
    const double blockSec = lr.rate > 0.0 ? (double) juce::jmax(0, numSamples) / lr.rate : 0.0;
    if (lr.host.valid && lr.host.playing && t.playing)
    {
        // The delta is reduced to the nearest multiple of a bounded host's beat space, so its wrap is not a
        // jump; the tolerance grows with a tempo change between blocks (automation moves the beat a little).
        const double d   = mu_core::reduceBeatDelta(t.startBeat - lr.host.endBeat, beatWrap);
        const double tol = kJumpToleranceBeats + std::abs(bpsNow - lr.host.beatsPerSecond) * lr.host.blockSeconds;
        if (std::abs(d) > tol) lr.jumpPending = true;
    }
    lr.host = { true, t.playing, t.startBeat + t.blockBeats, bpsNow, blockSec };
}

// A host block that rendered nothing: a stop hidden in it must still reach the layers, as a jump if play
// resumes before the next rendered block (design 2.1.5, the stop rule).
void ProcessorBase::noteSkippedBlock(const mu_core::BlockTransport& t) noexcept
{
    if (! t.playing && lr.lastPlaying) lr.jumpPending = true;
    ++lr.blocksSkipped;
}

// One decode per block: the router has already taken its claims out of `midi`. Short messages only (sysex
// and longer messages are not carried); the input fills the storage up to the note-off reserve.
void ProcessorBase::decodeBlockMidi(const juce::MidiBuffer& midi, int numSamples) noexcept
{
    const int cap = mu_limits::kMaxBlockMidiEvents - mu_limits::kMidiNoteOffReserve;
    int32_t count = 0;
    uint16_t dropped = 0;
    for (const auto meta : midi)
    {
        if (meta.numBytes < 1 || meta.numBytes > 3) continue;
        if (count >= cap) { if (dropped < 0xFFFF) ++dropped; continue; }
        auto& e = lr.midi[(size_t) count++];
        e.frame = juce::jlimit(0, juce::jmax(0, numSamples - 1), meta.samplePosition);
        e.size  = (uint8_t) meta.numBytes;
        for (int b = 0; b < 3; ++b) e.bytes[b] = b < meta.numBytes ? meta.data[b] : (uint8_t) 0;
    }
    lr.midiIn  = count;
    lr.liveOut = 0;
    lr.tailOut = 0;
    lr.dropped = dropped;
}

void ProcessorBase::renderAndMix(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi, const mu_core::BlockTransport& t,
                                 double beatWrap,
                                 std::array<juce::AudioBuffer<float>*, MixerEngine::MaxChannels>* directOuts,
                                 juce::AudioBuffer<float>* fxReturnsOut) noexcept
{
    const int n = buffer.getNumSamples();

    // Step 1: continuity, every host block.
    noteHostBlock(t, n, beatWrap);
    if (n <= 0 || lr.prepared <= 0) { noteSkippedBlock(t); return; }

    // Step 2: the block's one access to the layers (and its ScopedNoDenormals, C11).
    const mu_core::LayerBlock block(layerAccess, layersLock, layerEditInFlight, layers);
    if (! block) { noteSkippedBlock(t); return; }

    // Step 3: one MIDI decode for every layer.
    decodeBlockMidi(midi, n);

    // Step 4: chunks of at most the prepared size (C8), each the layers then the mixer on a sub-buffer, so an
    // oversize host block never overruns the scratch, the mixer's channel buffers or the FX.
    const int numCh = buffer.getNumChannels();
    for (int off = 0; off < n; off += lr.prepared)
    {
        const int len = juce::jmin(lr.prepared, n - off);
        lr.largestCall = juce::jmax(lr.largestCall, len);
        renderLayers(block, t, beatWrap, off, len, off == 0, off + len >= n);

        // Views of the host buffers for this chunk: they reference, never copy or allocate (up to 32 channels).
        juce::AudioBuffer<float> sub;
        sub.setDataToReferTo(buffer.getArrayOfWritePointers(), numCh, off, len);

        std::array<juce::AudioBuffer<float>, MixerEngine::MaxChannels> directViews;
        std::array<juce::AudioBuffer<float>*, MixerEngine::MaxChannels> directPtrs {};
        if (directOuts != nullptr)
            for (size_t i = 0; i < directPtrs.size(); ++i)
                if (auto* d = (*directOuts)[i]; d != nullptr && d->getNumSamples() >= off + len)
                {
                    directViews[i].setDataToReferTo(d->getArrayOfWritePointers(), d->getNumChannels(), off, len);
                    directPtrs[i] = &directViews[i];
                }
        juce::AudioBuffer<float> fxView;
        if (fxReturnsOut != nullptr && fxReturnsOut->getNumSamples() >= off + len)
            fxView.setDataToReferTo(fxReturnsOut->getArrayOfWritePointers(), fxReturnsOut->getNumChannels(), off, len);

        processCoreBlock(sub, nullptr, lr.mixCount, len, t.bpm,
                         directOuts != nullptr ? &directPtrs : nullptr,
                         fxReturnsOut != nullptr && fxView.getNumChannels() > 0 ? &fxView : nullptr,
                         nullptr, &lr.copyToMixer);
    }

    // The layers' MIDI out stays in the block's storage: the product merges it into the host buffer with
    // mergeLayerMidiOut and the room it knows that buffer has (design 2.1.5, R7; the mechanism is the
    // MIDI-output product's to settle when it adopts the driver).
    ++lr.blocksRendered;
}

void ProcessorBase::renderLayers(const mu_core::LayerBlock& block, const mu_core::BlockTransport& t, double beatWrap,
                                 int offset, int numSamples, bool firstChunk, bool lastChunk) noexcept
{
    const auto& v = block.view();
    constexpr int K = mu_limits::kMaxLayers;
    const int reserveStart = mu_limits::kMaxBlockMidiEvents - mu_limits::kMidiNoteOffReserve;

    // The call's context template (design 2.1.5). A chunk starts where the previous one ended in beats; the
    // contract never wraps startBeat (a layer in a bounded host applies mu_core::wrapBeat itself).
    auto base = mu_core::makeRenderContext();
    base.sampleRate        = lr.rate;
    base.blockSize         = numSamples;
    base.preparedBlockSize = lr.prepared;
    base.frameOffset       = offset;
    base.bpm               = t.bpm;
    base.startBeat         = t.startBeat + (t.playing ? (double) offset * t.beatsPerSample : 0.0);
    base.beatsPerSample    = t.beatsPerSample;
    base.callBeats         = t.playing ? (double) numSamples * t.beatsPerSample : 0.0;
    base.beatWrap          = beatWrap;
    base.playing           = t.playing ? 1 : 0;
    base.midi              = { lr.midi.get(), lr.midiIn, offset, 0xFFFF, lr.dropped, 0 };
    base.midiOut           = { lr.midi.get() + lr.midiIn, reserveStart - lr.midiIn, 0, &lr.liveOut };

    // Phase 0 (first chunk): edges against the last RENDERED block, the transport hooks, and mixCount by the
    // count policy, taken before any tail renders.
    if (firstChunk)
    {
        lr.edgeStarted = t.playing && ! lr.lastPlaying;
        lr.edgeStopped = ! t.playing && lr.lastPlaying;
        lr.edgeJumped  = t.playing && lr.lastPlaying && lr.jumpPending;

        int mc = juce::jlimit(0, K, (int) v.numLive);
        if (retireBin.countPolicy() == mu_core::RetireBin::CountPolicy::RenderPastCount)
            mc = juce::jmax(mc, retireBin.highestActiveSlot(v.serial) + 1);
        lr.mixCount = juce::jlimit(0, K, mc);
    }
    base.justStarted     = firstChunk && lr.edgeStarted ? 1 : 0;
    base.justStopped     = firstChunk && lr.edgeStopped ? 1 : 0;
    base.transportJumped = firstChunk && lr.edgeJumped  ? 1 : 0;

    // Which slots have a live, non-Empty layer this block (Empty is never called).
    for (int s = 0; s < K; ++s)
    {
        const Layer* l = s < v.numLive ? v.layer[(size_t) s] : nullptr;
        lr.live[(size_t) s] = l != nullptr && l->typeId() != 0;
    }

    if (firstChunk && (lr.edgeStopped || lr.edgeJumped))
    {
        // Every live layer, slot order, before any render of the block.
        for (int s = 0; s < K; ++s)
        {
            if (! lr.live[(size_t) s]) continue;
            auto ctx = base;
            ctx.slot = s;
            if (lr.edgeStopped) v.layer[(size_t) s]->onTransportStopped(ctx);
            else                v.layer[(size_t) s]->onTransportJump(ctx);
        }
    }

    // A slot's per-call view of its scratch: exactly this call's frames, handed over cleared (as the mixer
    // clears a channel before its render hook, so a render that adds into its buffer stays bit-identical).
    auto openView = [this, numSamples](int s) -> juce::AudioBuffer<float>&
    {
        auto& view = lr.view[(size_t) s];
        view.setDataToReferTo(lr.scratch[(size_t) s].getArrayOfWritePointers(), 2, 0, numSamples);
        view.clear();
        return view;
    };
    auto renderLive = [&](int s, const mu_core::PeerTap* peers)
    {
        auto ctx = base;
        ctx.slot     = s;
        ctx.frozen   = (uint8_t) ((v.frozenMask >> s) & 1u);
        ctx.peers    = peers;
        ctx.numPeers = peers != nullptr ? K : 0;
        v.layer[(size_t) s]->render(ctx, openView(s), numSamples);
    };

    // Phase 1: priority-0 live layers in slot order; every other slot below mixCount gets cleared scratch (an
    // entry cut between phase 0 and phase 4 then renders silence, never last block's samples).
    for (auto& p : lr.peers) p = {};
    for (int s = 0; s < K; ++s)
    {
        if (! lr.live[(size_t) s])
        {
            if (s < lr.mixCount) openView(s);
            continue;
        }
        if (v.layer[(size_t) s]->renderPriority() != 0) continue;
        renderLive(s, nullptr);

        // Phase 2 (folded in): the peer table, post-insert and pre-strip (C10), before any tail is added.
        auto& p = lr.peers[(size_t) s];
        p.channel[0] = lr.scratch[(size_t) s].getReadPointer(0);
        p.channel[1] = lr.scratch[(size_t) s].getReadPointer(1);
        p.valid = 1;
    }

    // Phase 3: priority-1 live layers, which may read a priority-0 peer.
    for (int s = 0; s < K; ++s)
        if (lr.live[(size_t) s] && v.layer[(size_t) s]->renderPriority() != 0)
            renderLive(s, lr.peers.data());

    // Phase 4: the bin, rows below mixCount only, slot order then entry order, added after the live layer.
    // Tails see the block's flags, read no MIDI and write owed note-offs into the reserve.
    bool wentDone = false;
    for (int s = 0; s < lr.mixCount; ++s)
    {
        auto ctx = base;
        ctx.slot = s;
        ctx.midiOut = { lr.midi.get() + reserveStart, mu_limits::kMidiNoteOffReserve, 0, &lr.tailOut };
        auto& view = lr.view[(size_t) s];
        view.setDataToReferTo(lr.scratch[(size_t) s].getArrayOfWritePointers(), 2, 0, numSamples);
        wentDone |= retireBin.render(s, ctx, view, numSamples, v.serial, lr.live[(size_t) s]);
    }
    if (wentDone) requestDeferredWork();   // the free runs on the message thread after the grace period

    // Phase 5 (last chunk): the loop lengths for the boundary flagging and the rendered play state. Values only.
    if (lastChunk)
    {
        for (int s = 0; s < K; ++s)
            lr.loopBeats[(size_t) s] = lr.live[(size_t) s] ? v.layer[(size_t) s]->loopBeats() : kDefaultLoopBeats;
        lr.lastPlaying = t.playing;
        lr.jumpPending = false;
    }
}

// Appends the last block's layer MIDI out to `midi`, the tails' owed note-offs first, while the bytes added
// stay within `byteBudget`: the room the caller knows the buffer already has, so the MidiBuffer never grows
// (and allocates) on the audio thread. Returns the events appended.
int ProcessorBase::mergeLayerMidiOut(juce::MidiBuffer& midi, int byteBudget) noexcept
{
    const int reserveStart = mu_limits::kMaxBlockMidiEvents - mu_limits::kMidiNoteOffReserve;
    int added = 0;
    auto add = [&](const mu_core::MidiEvent& e)
    {
        const int cost = kMidiBytesPerEvent + (int) e.size;
        if (cost > byteBudget) return;
        byteBudget -= cost;
        midi.addEvent(e.bytes, (int) e.size, e.frame);
        ++added;
    };
    for (int i = 0; i < lr.tailOut; ++i) add(lr.midi[(size_t) (reserveStart + i)]);
    for (int i = 0; i < lr.liveOut; ++i) add(lr.midi[(size_t) (lr.midiIn + i)]);
    return added;
}
