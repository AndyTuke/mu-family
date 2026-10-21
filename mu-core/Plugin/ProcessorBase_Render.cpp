// The shared block driver of the layer render contract: renderAndMix, renderLayers and the transport
// latches. docs/mu-tronic/design.md 2.1.1 (the shape of one block), 2.1.5 (the time model).
//
// Real-time rules every function here keeps (audio thread unless noted): no allocation (all storage is
// sized by prepareLayerRender), no lock but the one LayerBlock try-lock in TryLock mode, no layer pointer
// kept past the block, no call that can block. The message-thread work the audio thread needs (freeing a
// tail that went Done) is requested through the deferred-work mailbox, an atomic flag.

#include "Plugin/ProcessorBase.h"
#include "Sequencer/BeatWrap.h"
#include "Sequencer/LayerTypeInfo.h"   // mu_core::LayerTypeId

namespace
{
    constexpr double kDefaultLoopBeats = 4.0;          // a slot with no live layer: the swapper's 4/4 bar rule
    constexpr double kJumpToleranceBeats = 1.0 / 256.0;  // the continuity check's base tolerance (design 2.1.6, point 1)
}

void ProcessorBase::prepareLayerRender(double sampleRate, int maxBlockSize)
{
    // Message or host thread; the host is not processing. Every allocation of the driver happens here.
    {
        // What setLayerType compares against: this prepare's rate and size, and a new generation.
        const juce::ScopedLock sl(layersLock);
        ++prepareGeneration;
        layersPreparedRate  = sampleRate;
        layersPreparedBlock = juce::jmax(1, maxBlockSize);
    }
    renderState.rate     = sampleRate;
    renderState.prepared = juce::jmax(1, maxBlockSize);
    for (auto& s : renderState.scratch) s.setSize(2, renderState.prepared, false, true, false);
    if (renderState.midi.get() == nullptr) renderState.midi.allocate((size_t) mu_limits::kMaxBlockMidiEvents, true);
    renderState.loopBeats.fill(kDefaultLoopBeats);
    renderState.live.fill(false);
    renderState.host = {};
    renderState.lastPlaying = renderState.jumpPending = renderState.stopPending = false;
    renderState.edgeStarted = renderState.edgeStopped = renderState.edgeJumped = renderState.edgeHiddenStop = false;
    renderState.mixCount = 0;
    renderState.midiIn = renderState.liveOut = renderState.tailOut = 0;
    renderState.dropped = 0;
    renderState.inSkip = false;
    renderState.fadePos = LayerRenderState::kFadeIn;
    for (int c = 0; c < 2; ++c) renderState.lastOut[c] = renderState.tailLevel[c] = renderState.tailStep[c] = 0.0f;

    // What the layer types share (their engines, tables), for this rate and size, before the layers that read it.
    {
        const juce::ScopedLock sl(layerServicesLock);
        for (auto& s : layerServices)
            if (s != nullptr) s->prepare(sampleRate, renderState.prepared);
    }

    // Every slot's layer, spares included, for this rate and size. layersLock keeps a structural edit (message
    // thread) from moving them meanwhile; the audio thread is not running.
    {
        const juce::ScopedLock sl(layersLock);
        for (auto& l : layers)
            if (l != nullptr) l->prepare(sampleRate, renderState.prepared);
    }

    // The bin never crosses a prepare: everything still sounding is cut and freed (nothing is processing,
    // so every grace has passed), then it converts seconds with the new rate.
    retireBin.setPrepared(sampleRate, renderState.prepared);
    retireBin.cutAll();
    retireBin.service();
}

void ProcessorBase::releaseLayerRender()
{
    retireBin.cutAll();
    retireBin.service();
    renderState.host = {};
    renderState.lastPlaying = renderState.jumpPending = renderState.stopPending = false;
}

mu_core::BlockTransport ProcessorBase::resolveBlockTransport(juce::MidiBuffer& midi, int numSamples, double wrapBeats)
{
    const double sr = renderState.rate > 0.0 ? renderState.rate : getSampleRate();
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
//
// A host that reports playing while its position does not move (a stall, some hosts' pre-roll) is a FREEZE, not
// one jump per block: the frozen blocks set nothing, and when the position moves again the layers get one jump,
// so they re-anchor once on resume.
void ProcessorBase::noteHostBlock(const mu_core::BlockTransport& t, int numSamples) noexcept
{
    auto& h = renderState.host;
    const double bpsNow   = t.bpm / 60.0;
    const double blockSec = renderState.rate > 0.0 ? (double) juce::jmax(0, numSamples) / renderState.rate : 0.0;
    bool frozen = false;
    if (h.valid && h.playing && t.playing)
    {
        // The delta is reduced to the nearest multiple of a bounded host's beat space, so its wrap is not a
        // jump; the tolerance grows with a tempo change between blocks (automation moves the beat a little).
        const double tol    = kJumpToleranceBeats + std::abs(bpsNow - h.beatsPerSecond) * h.blockSeconds;
        const double d      = mu_core::reduceBeatDelta(t.startBeat - h.endBeat, t.beatWrap);
        const double stayed = mu_core::reduceBeatDelta(t.startBeat - h.startBeat, t.beatWrap);
        frozen = std::abs(d) > tol && std::abs(stayed) <= kJumpToleranceBeats;
        if (frozen)                       {}                                // a frozen block is not a jump
        else if (std::abs(d) > tol)       renderState.jumpPending = true;   // a locate, a loop, a clock snap
        else if (h.frozen)                renderState.jumpPending = true;   // the freeze ended: one jump on resume
    }
    h = { true, t.playing, t.startBeat, t.startBeat + t.blockBeats, bpsNow, blockSec, frozen };
}

// A host block that rendered nothing: a stop hidden in it must still reach the layers, as a jump if play
// resumes before the next rendered block (design 2.1.5, the stop rule).
// The buffer arrived as the host gave it, so a skipped block clears it: silence, as a missed try-lock always was.
// With the carry on, its channel messages wait for the next rendered block (at frame 0, in arrival order).
void ProcessorBase::noteSkippedBlock(const mu_core::BlockTransport& t, juce::AudioBuffer<float>& buffer,
                                     const juce::MidiBuffer& midi, bool editSkip) noexcept
{
    if (! t.playing && renderState.lastPlaying) renderState.jumpPending = renderState.stopPending = true;
    buffer.clear();

    // A Published host's edit silences blocks it cannot render: the master decays from its last sample to zero over a few
    // milliseconds instead of dropping, so the cut is not a click. The decay carries on across consecutive skipped blocks.
    if (editSkip && layerAccess.mode == mu_core::LayerAccess::Published)
    {
        auto& rs = renderState;
        if (! rs.inSkip)
        {
            rs.inSkip = true;
            rs.fadePos = 0;
            for (int c = 0; c < 2; ++c) { rs.tailLevel[c] = rs.lastOut[c]; rs.tailStep[c] = rs.lastOut[c] / (float) LayerRenderState::kSkipDecay; }
        }
        const int n = buffer.getNumSamples();
        for (int c = 0; c < juce::jmin(2, buffer.getNumChannels()); ++c)
        {
            float* d = buffer.getWritePointer(c);
            float level = rs.tailLevel[c];
            const float step = rs.tailStep[c];
            for (int i = 0; i < n; ++i)
            {
                level = std::abs(level) > std::abs(step) ? level - step : 0.0f;
                d[i] = level;
            }
            rs.tailLevel[c] = level;
        }
    }
    if (renderState.carryMidi && renderState.midi.get() != nullptr)
        for (const auto meta : midi)
        {
            if (meta.numBytes < 1 || meta.numBytes > 3) continue;
            const auto type = (uint8_t) (meta.data[0] & 0xf0);
            if (type < 0x80 || type == 0xf0) continue;   // a system message (clock, transport) holds no state to carry
            if (renderState.carried >= mu_limits::kMaxCarriedMidiEvents)
            {
                if (renderState.carryDropped < 0xFFFF) ++renderState.carryDropped;
                continue;
            }
            auto& e = renderState.midi[(size_t) renderState.carried++];
            e.frame = 0;
            e.size  = (uint8_t) meta.numBytes;
            for (int b = 0; b < 3; ++b) e.bytes[b] = b < meta.numBytes ? meta.data[b] : (uint8_t) 0;
        }
    // This block produced no MIDI: a later mergeLayerMidiOut must not re-send the last rendered block's events.
    renderState.liveOut = renderState.tailOut = 0;
    renderState.lastBlockFrames = 0;
    ++renderState.blocksSkipped;
}

// The DAW sidechain of host frames [offset, offset + n) into the mixer's private copy (sized at prepare; n is at
// most the prepared size), BEFORE the chunk's output region is cleared: an instrument's sidechain input shares
// buffer channels with its output. No bus, or a disabled one: no sidechain.
void ProcessorBase::captureSidechainRegion(juce::AudioBuffer<float>& buffer, int offset, int n) noexcept
{
    if (getBusCount(true) > 0)
        if (auto* scBus = getBus(true, 0); scBus != nullptr && scBus->isEnabled())
        {
            auto bus = getBusBuffer(buffer, true, 0);
            if (bus.getNumChannels() > 0 && bus.getNumChannels() < 32)
            {
                juce::AudioBuffer<float> region;
                region.setDataToReferTo(bus.getArrayOfWritePointers(), bus.getNumChannels(), offset, n);
                mixerEngine.copyExternalSidechain(region);
                return;
            }
        }
    mixerEngine.setExternalSidechain(nullptr, nullptr);
}

// One decode per block: the router has already taken its claims out of `midi`. Short messages only (sysex
// and longer messages are not carried); the input fills the storage up to the note-off reserve. Events carried
// from unrendered blocks are already at the front and stay there.
void ProcessorBase::decodeBlockMidi(const juce::MidiBuffer& midi, int numSamples) noexcept
{
    const int cap = mu_limits::kMaxBlockMidiEvents - mu_limits::kMidiNoteOffReserve;
    int32_t count = renderState.carried;
    renderState.midiCarried = (uint16_t) renderState.carried;
    renderState.carried = 0;
    uint16_t dropped = renderState.carryDropped;   // what the carry could not keep is reported with this block
    renderState.carryDropped = 0;
    for (const auto meta : midi)
    {
        if (meta.numBytes < 1 || meta.numBytes > 3) continue;
        if (count >= cap) { if (dropped < 0xFFFF) ++dropped; continue; }
        auto& e = renderState.midi[(size_t) count++];
        e.frame = juce::jlimit(0, juce::jmax(0, numSamples - 1), meta.samplePosition);
        e.size  = (uint8_t) meta.numBytes;
        for (int b = 0; b < 3; ++b) e.bytes[b] = b < meta.numBytes ? meta.data[b] : (uint8_t) 0;
    }
    renderState.midiIn  = count;
    renderState.liveOut = 0;
    renderState.tailOut = 0;
    renderState.dropped = dropped;
}

void ProcessorBase::renderAndMix(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi, const mu_core::BlockTransport& t,
                                 std::array<juce::AudioBuffer<float>*, MixerEngine::MaxChannels>* directOuts,
                                 juce::AudioBuffer<float>* fxReturnsOut) noexcept
{
    const int n = buffer.getNumSamples();

    // Step 1: continuity, every host block.
    noteHostBlock(t, n);
    if (n <= 0 || renderState.prepared <= 0) { noteSkippedBlock(t, buffer, midi); return; }
    // The driver's chunk is never longer than what the mixer (and the FX it runs) was prepared for.
    jassert(renderState.prepared <= mixerEngine.getPreparedBlockSize());

    // Step 2: the block's one access to the layers (and its ScopedNoDenormals, C11).
    const mu_core::LayerBlock block(layerAccess, layersLock, layerEditInFlight, layers);
    if (! block) { noteSkippedBlock(t, buffer, midi, true); return; }

    // Step 3: one MIDI decode for every layer.
    decodeBlockMidi(midi, n);
    layersBlockStarting(block.view(), t, midi);

    // Step 4: chunks of at most the prepared size (C8), each the layers then the mixer on a sub-buffer, so an
    // oversize host block never overruns the scratch, the mixer's channel buffers or the FX.
    // Chunk views reference the host's channel pointers; JUCE would allocate a pointer table at 32 channels or more.
    jassert(buffer.getNumChannels() < 32);
    const int numCh = juce::jmin(buffer.getNumChannels(), 31);
    for (int off = 0; off < n; off += renderState.prepared)
    {
        const int len = juce::jmin(renderState.prepared, n - off);
        renderState.largestCall = juce::jmax(renderState.largestCall, len);

        // This chunk's sidechain frames first, then its output region cleared (the host buffer arrived raw).
        captureSidechainRegion(buffer, off, len);
        juce::AudioBuffer<float> sub;
        sub.setDataToReferTo(buffer.getArrayOfWritePointers(), numCh, off, len);
        sub.clear();

        renderLayers(block, t, off, len, off == 0, off + len >= n);

        std::array<juce::AudioBuffer<float>, MixerEngine::MaxChannels> directViews;
        std::array<juce::AudioBuffer<float>*, MixerEngine::MaxChannels> directPtrs {};
        if (directOuts != nullptr)
            for (size_t i = 0; i < directPtrs.size(); ++i)
                if (auto* d = (*directOuts)[i]; d != nullptr && d->getNumSamples() >= off + len && d->getNumChannels() < 32)
                {
                    directViews[i].setDataToReferTo(d->getArrayOfWritePointers(), d->getNumChannels(), off, len);
                    directPtrs[i] = &directViews[i];
                }
        juce::AudioBuffer<float> fxView;
        if (fxReturnsOut != nullptr && fxReturnsOut->getNumSamples() >= off + len && fxReturnsOut->getNumChannels() < 32)
            fxView.setDataToReferTo(fxReturnsOut->getArrayOfWritePointers(), fxReturnsOut->getNumChannels(), off, len);

        processCoreBlock(sub, nullptr, renderState.mixCount, len, t.bpm,
                         directOuts != nullptr ? &directPtrs : nullptr,
                         fxReturnsOut != nullptr && fxView.getNumChannels() > 0 ? &fxView : nullptr,
                         nullptr, &renderState.copyToMixer);
    }

    // What a type's services add to the master bus themselves (a sample browser's preview), in the chunks the mixer
    // ran in, so a service never sees more frames than it was prepared for.
    for (const auto& slot : audioServices)
        if (auto* services = slot.load(std::memory_order_acquire))
            for (int off = 0; off < n; off += juce::jmax(1, renderState.prepared))
            {
                juce::AudioBuffer<float> chunk;
                chunk.setDataToReferTo(buffer.getArrayOfWritePointers(), numCh, off, juce::jmin(juce::jmax(1, renderState.prepared), n - off));
                services->mixIntoMaster(chunk, chunk.getNumSamples());
            }

    if (layerAccess.mode == mu_core::LayerAccess::Published) declickMaster(buffer, n);

    // The layers' MIDI out stays in the block's storage: the product merges it into the host buffer with
    // mergeLayerMidiOut and the room it knows that buffer has (design 2.1.5, R7; the mechanism is the
    // MIDI-output product's to settle when it adopts the driver).
    layersBlockRendered(block.view(), buffer, midi);
    renderState.lastBlockFrames = n;
    ++renderState.blocksRendered;
}

void ProcessorBase::declickMaster(juce::AudioBuffer<float>& buffer, int numSamples) noexcept
{
    auto& rs = renderState;
    const int numCh = juce::jmin(2, buffer.getNumChannels());
    if (rs.inSkip || rs.fadePos < LayerRenderState::kFadeIn)
    {
        // The first rendered samples after a skipped block: the layers fade in linearly while what is left of the decay
        // runs out, so the output is continuous with the silence it follows.
        for (int c = 0; c < numCh; ++c)
        {
            float* d = buffer.getWritePointer(c);
            float level = rs.inSkip ? rs.tailLevel[c] : 0.0f;
            const float step = rs.tailStep[c];
            for (int i = 0; i < numSamples; ++i)
            {
                const int pos = rs.fadePos + i;
                const float gain = pos >= LayerRenderState::kFadeIn ? 1.0f : (float) (pos + 1) / (float) LayerRenderState::kFadeIn;
                level = std::abs(level) > std::abs(step) ? level - step : 0.0f;
                d[i] = d[i] * gain + level;
            }
        }
        rs.fadePos = juce::jmin(LayerRenderState::kFadeIn, rs.fadePos + numSamples);
        rs.inSkip = false;
    }
    for (int c = 0; c < numCh; ++c) rs.lastOut[c] = numSamples > 0 ? buffer.getSample(c, numSamples - 1) : 0.0f;
}

void ProcessorBase::renderLayers(const mu_core::LayerBlock& block, const mu_core::BlockTransport& t,
                                 int offset, int numSamples, bool firstChunk, bool lastChunk) noexcept
{
    const auto& v = block.view();
    constexpr int K = mu_limits::kMaxLayers;
    const int reserveStart = mu_limits::kMaxBlockMidiEvents - mu_limits::kMidiNoteOffReserve;

    // The call's context template (design 2.1.5). A chunk starts where the previous one ended in beats; the
    // contract never wraps startBeat (a layer in a bounded host applies mu_core::wrapBeat itself).
    auto base = mu_core::makeRenderContext();
    base.sampleRate        = renderState.rate;
    base.blockSize         = numSamples;
    base.preparedBlockSize = renderState.prepared;
    base.frameOffset       = offset;
    base.bpm               = t.bpm;
    base.startBeat         = t.startBeat + (t.playing ? (double) offset * t.beatsPerSample : 0.0);
    base.beatsPerSample    = t.beatsPerSample;
    base.callBeats         = t.playing ? (double) numSamples * t.beatsPerSample : 0.0;
    base.beatWrap          = t.beatWrap;
    base.playing           = t.playing ? 1 : 0;
    base.midi              = { renderState.midi.get(), renderState.midiIn, offset, 0xFFFF, renderState.dropped,
                               renderState.midiCarried, 0 };
    base.midiOut           = { renderState.midi.get() + renderState.midiIn, reserveStart - renderState.midiIn, 0, &renderState.liveOut };

    // Phase 0 (first chunk): edges against the last RENDERED block, the transport hooks, and mixCount by the
    // count policy, taken before any tail renders.
    if (firstChunk)
    {
        renderState.edgeStarted = t.playing && ! renderState.lastPlaying;
        renderState.edgeStopped = ! t.playing && renderState.lastPlaying;
        renderState.edgeJumped  = t.playing && renderState.lastPlaying && renderState.jumpPending;
        renderState.edgeHiddenStop = renderState.edgeJumped && renderState.stopPending;

        int mc = juce::jlimit(0, K, (int) v.numLive);
        if (retireBin.countPolicy() == mu_core::RetireBin::CountPolicy::RenderPastCount)
            mc = juce::jmax(mc, retireBin.highestActiveSlot(v.serial) + 1);
        renderState.mixCount = juce::jlimit(0, K, mc);
    }
    base.justStarted     = firstChunk && renderState.edgeStarted ? 1 : 0;
    base.justStopped     = firstChunk && renderState.edgeStopped ? 1 : 0;
    base.transportJumped = firstChunk && renderState.edgeJumped  ? 1 : 0;
    base.stoppedUnrendered = firstChunk && renderState.edgeHiddenStop ? 1 : 0;

    // Which slots have a live, non-Empty layer this block (Empty is never called).
    for (int s = 0; s < K; ++s)
    {
        const Layer* l = s < v.numLive ? v.layer[(size_t) s] : nullptr;
        renderState.live[(size_t) s] = l != nullptr && l->typeId() != 0;
    }

    if (firstChunk && (renderState.edgeStopped || renderState.edgeJumped))
    {
        // Every live layer, slot order, before any render of the block.
        for (int s = 0; s < K; ++s)
        {
            if (! renderState.live[(size_t) s]) continue;
            auto ctx = base;
            ctx.slot = s;
            if (renderState.edgeStopped) v.layer[(size_t) s]->onTransportStopped(ctx);
            else                v.layer[(size_t) s]->onTransportJump(ctx);
        }
    }

    // A slot's per-call view of its scratch: exactly this call's frames, handed over cleared (as the mixer
    // clears a channel before its render hook, so a render that adds into its buffer stays bit-identical).
    auto openView = [this, numSamples](int s) -> juce::AudioBuffer<float>&
    {
        auto& view = renderState.view[(size_t) s];
        view.setDataToReferTo(renderState.scratch[(size_t) s].getArrayOfWritePointers(), 2, 0, numSamples);
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
    // entry cut between phase 0 and phase 4 then renders silence, never last block's samples). Each live layer's
    // priority is read once per call (`priority1`) and phase 3 uses that reading: a type whose priority follows a
    // parameter (mu-On's engine) is rendered exactly once even when the parameter moves between the two passes.
    for (auto& p : renderState.peers) p = {};
    std::array<bool, (size_t) K> priority1 {};
    for (int s = 0; s < K; ++s)
    {
        if (! renderState.live[(size_t) s])
        {
            if (s < renderState.mixCount) openView(s);
            // A layer parked above the live count (a kept spare) follows the call's input, nothing more.
            if (s >= v.numLive && v.layer[(size_t) s] != nullptr && v.layer[(size_t) s]->typeId() != 0)
            {
                auto ctx = base;
                ctx.slot = s;
                v.layer[(size_t) s]->onParkedBlock(ctx);
            }
            continue;
        }
        if (v.layer[(size_t) s]->renderPriority() != 0) { priority1[(size_t) s] = true; continue; }
        renderLive(s, nullptr);

        // Phase 2 (folded in): the peer table, post-insert and pre-strip (C10), before any tail is added.
        auto& p = renderState.peers[(size_t) s];
        p.channel[0] = renderState.scratch[(size_t) s].getReadPointer(0);
        p.channel[1] = renderState.scratch[(size_t) s].getReadPointer(1);
        p.valid = 1;
    }

    // Phase 3: priority-1 live layers, which may read a priority-0 peer.
    for (int s = 0; s < K; ++s)
        if (renderState.live[(size_t) s] && priority1[(size_t) s])
            renderLive(s, renderState.peers.data());

    // Phase 4: the bin, rows below mixCount only, slot order then entry order, added after the live layer.
    // Tails see the block's flags, read no MIDI and write owed note-offs into the reserve.
    bool wentDone = false;
    for (int s = 0; s < renderState.mixCount; ++s)
    {
        auto ctx = base;
        ctx.slot = s;
        ctx.midiOut = { renderState.midi.get() + reserveStart, mu_limits::kMidiNoteOffReserve, 0, &renderState.tailOut };
        auto& view = renderState.view[(size_t) s];
        // Every slot below mixCount was opened this call, so the view normally refers to its scratch already and is
        // left alone (its cleared flag tells the mixer hook a silent slot from one that rendered). Re-pointing would
        // drop that flag, so a re-point keeps it (the clear only re-zeroes zeros).
        auto& scratch = renderState.scratch[(size_t) s];
        if (view.getNumSamples() != numSamples || view.getNumChannels() != 2 || view.getReadPointer(0) != scratch.getReadPointer(0))
        {
            const bool untouched = view.hasBeenCleared();
            view.setDataToReferTo(scratch.getArrayOfWritePointers(), 2, 0, numSamples);
            if (untouched) view.clear();
        }
        wentDone |= retireBin.render(s, ctx, view, numSamples, v.serial, renderState.live[(size_t) s]);
    }
    if (wentDone) requestDeferredWork();   // the free runs on the message thread after the grace period

    // Phase 5 (last chunk): the loop lengths for the boundary flagging and the rendered play state. Values only.
    if (lastChunk)
    {
        // A parked spare caches its own length too, so a product whose boundary rule reads every slot's length
        // (mu-Tant's) sees the same value whether the count grew in this block or not; Empty slots take the bar.
        for (int s = 0; s < K; ++s)
        {
            const Layer* l = v.layer[(size_t) s];
            renderState.loopBeats[(size_t) s] = l != nullptr && l->typeId() != (int) mu_core::LayerTypeId::Empty ? l->loopBeats() : kDefaultLoopBeats;
        }
        renderState.lastPlaying = t.playing;
        renderState.jumpPending = renderState.stopPending = false;
    }
}

// Appends the last block's layer MIDI out to `midi`, the tails' owed note-offs first, while the bytes added
// stay within `byteBudget`: the room the caller knows the buffer already has, so the MidiBuffer never grows
// (and allocates) on the audio thread. Returns the events appended.
int ProcessorBase::mergeLayerMidiOut(juce::MidiBuffer& midi, int byteBudget) noexcept
{
    const int reserveStart = mu_limits::kMaxBlockMidiEvents - mu_limits::kMidiNoteOffReserve;
    const int lastFrame = juce::jmax(0, renderState.lastBlockFrames - 1);
    int added = 0;
    auto add = [&](const mu_core::MidiEvent& e)
    {
        const int cost = midiEventBytes((int) e.size);
        if (cost > byteBudget) return;
        byteBudget -= cost;
        midi.addEvent(e.bytes, (int) e.size, juce::jlimit(0, lastFrame, (int) e.frame));
        ++added;
    };
    for (int i = 0; i < renderState.tailOut; ++i) add(renderState.midi[(size_t) (reserveStart + i)]);
    for (int i = 0; i < renderState.liveOut; ++i) add(renderState.midi[(size_t) (renderState.midiIn + i)]);
    return added;
}

ProcessorBase::LayerRenderProbe ProcessorBase::renderOneLayerBlockForTest(double sampleRate, int n)
{
    prepareLayerRender(sampleRate, n);
    juce::AudioBuffer<float> buffer(juce::jmax(2, getTotalNumOutputChannels()), n);
    buffer.clear();
    juce::MidiBuffer midi;
    mu_core::BlockTransport t;
    t.playing = true;
    t.bpm = 120.0;
    t.beatsPerSample = (t.bpm / 60.0) / sampleRate;
    t.blockBeats = t.beatsPerSample * (double) n;
    renderAndMix(buffer, midi, t);
    const auto probe = getLayerRenderProbe();
    releaseLayerRender();
    return probe;
}
