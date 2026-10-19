#include "Audio/SpinLock.h"   // mu-core: spin lock helpers
#include "PluginProcessor.h"
#include "Plugin/TransportResolver.h"   // mu-core: the family transport rule
#include "Plugin/MidiRaw.h"             // mu-core: note tests on raw MIDI bytes (no allocation)
#include "License/ProductLicensing.h"   // mu-core: ProcessorBase::initLicensing (licensed products only)
#include "PluginProcessor_Internal.h"
#include "Audio/InsertSlotConfig.h"
#include "Modulation/ModulationSkew.h"  // proportion-space skew helpers (shared with test C5)
#include "Modulation/MuClidModDest.h"  // mu-clid modulation targets
#include "Sequencer/GeneratorAliases.h"
#include "Audio/FX/Slots/FXAlgorithmDef.h"

#include <thread>   // std::this_thread::yield in modulator deserialise lock-spin

namespace mu_clid {

// Declare one stereo sidechain input (disabled by default; DAW enables when the user
// wires an external signal) + 10 stereo output buses: Master (always enabled),
// Out 1..8 + FX Returns (disabled by default, matching pre-multi-bus behaviour).
PluginProcessor::PluginProcessor()
    : ProcessorBase(BusesProperties()
          .withInput ("Sidechain",   juce::AudioChannelSet::stereo(), false)
          .withOutput("Master",      juce::AudioChannelSet::stereo(), true)
          .withOutput("Out 1",       juce::AudioChannelSet::stereo(), false)
          .withOutput("Out 2",       juce::AudioChannelSet::stereo(), false)
          .withOutput("Out 3",       juce::AudioChannelSet::stereo(), false)
          .withOutput("Out 4",       juce::AudioChannelSet::stereo(), false)
          .withOutput("Out 5",       juce::AudioChannelSet::stereo(), false)
          .withOutput("Out 6",       juce::AudioChannelSet::stereo(), false)
          .withOutput("Out 7",       juce::AudioChannelSet::stereo(), false)
          .withOutput("Out 8",       juce::AudioChannelSet::stereo(), false)
          .withOutput("FX Returns",  juce::AudioChannelSet::stereo(), false),
          createParameterLayout(),
          juce::Identifier("MuClidState"))
{
    // Register mu-clid's modulation depth scales with mu-core before any audio runs
    // (once, message thread) — keeps mu-core from enumerating mu-clid param ids.

    initAppSettings("muClid");   // settings file + saved UI size / MIDI clock (ProcessorBase)

    // A hot-swap's retired engines sound out in the shared RetireBin only in live slots: a count-reducing store
    // cuts the tails of the slots it drops (they were silent there before too, and a regrow starts clean).
    setRetireCountPolicy(mu_core::RetireBin::CountPolicy::CutAtCount);

    midiNoteMode.store(appSettings->getIntValue("midiNoteMode", 0), std::memory_order_relaxed);

    // Load MIDI program-change preset maps (each lives in its own JSON file
    // next to appSettings). Maps are owned by ProcessorBase; we just point
    // them at the correct storage location and trigger the load.
    {
        const auto settingsDir = appSettings->getFile().getParentDirectory();
        midiPresetMap    .setStorageFile(settingsDir.getChildFile("muClid_midiPresets.json"));
        midiFullPresetMap.setStorageFile(settingsDir.getChildFile("muClid_midiFullPresets.json"));
        midiPresetMap    .load();
        midiFullPresetMap.load();
    }

    // Multi-bus output toggle (DAW). Default: on.
    multiBusEnabled.store(appSettings->getBoolValue("multiBusEnabled", true),
                          std::memory_order_relaxed);

    // Offline licence + online activation (shared; after initAppSettings for getContentDir).
    initLicensing({ mu_clid::kLicenseProductId, mu_clid::kLicenseFilename,
                    mu_clid::kLicensePublicKey, "muclid.activation", mu_clid::kCheckoutUrl });
    sequencer.setStepCap(HitGenerator::kMaxSteps);

    // Resolve each rhythm parameter once (its raw value pointer for the drain) and build the sorted
    // suffix table the listener searches, before any listener can fire.
    for (int ri = 0; ri < RhythmSequencer::MaxRhythms; ++ri)
    {
        const juce::String prefix = "r" + juce::String(ri) + "_";
        for (int i = 0; i < kRhythmParamCount; ++i)
            rawParams[(size_t) ri][(size_t) i] = apvts.getRawParameterValue(prefix + kRhythmParamDefs[i].suffix);
    }
    for (int i = 0; i < kRhythmParamCount; ++i)
        if (rawParams[0][(size_t) i] != nullptr)
            suffixIndex[(size_t) suffixIndexCount++] = { kRhythmParamDefs[i].suffix, i };
    std::sort(suffixIndex.begin(), suffixIndex.begin() + suffixIndexCount,
              [](const SuffixIndex& a, const SuffixIndex& b) { return std::strcmp(a.suffix, b.suffix) < 0; });

    swapTimingParam = apvts.getRawParameterValue(mu_hotswap::kSwapTimingParamId);

    // Listen to the product parameters (rhythm + mstrLoop); mixer / FX ids are synced by ProcessorBase.
    for (auto* param : getParameters())
        if (auto* p = dynamic_cast<juce::AudioProcessorParameterWithID*>(param))
            if (! isFxParamId(p->getParameterID()))
                apvts.addParameterListener(p->getParameterID(), this);
    startFxParamSync();

    // Add default rhythm (16 steps, 4 hits) and sync its state to APVTS.
    // All eight rhythms exist for the processor's lifetime (a restored state writes into slots above
    // the live count before it sets the count); the sequencer binds to them.
    keepSpareLayers = true;
    for (int ri = 0; ri < RhythmSequencer::MaxRhythms; ++ri)
    {
        layers[(size_t) ri] = makeRhythm(ri);
        sequencer.bindRhythm(ri, &layerAs<Rhythm>(ri));
    }
    Rhythm& defaultRhythm = layerAs<Rhythm>(0);
    defaultRhythm.name       = "<unnamed>";
    defaultRhythm.genA.steps = 16;
    defaultRhythm.genA.hits  = 4;
    setLiveRhythms(1);
    updateRhythmPattern(0);
    voiceEngine(0) = std::make_unique<VoiceEngine>();
    voiceEngine(0)->insertProc.setSlotIndex(0);   // per-slot noise seed: layers do not share one sequence
    storeLayerCount(1, std::memory_order_release);   // through the packed layout word, as every count store

    {
        mu_core::ScopedApvtsLoading guard(apvtsLoading);
        pushRhythmToApvts(0);
    }

    // Ensure user content folders exist and restore the saved default preset (skipped by render mode).
    ensureContentFoldersExist();
    loadStartupDefault();
}

PluginProcessor::~PluginProcessor()
{
    prepareForDestruction();   // first: no deferred-work tick may reach this half-destroyed object
    for (auto* param : getParameters())
        if (auto* p = dynamic_cast<juce::AudioProcessorParameterWithID*>(param))
            if (! isFxParamId(p->getParameterID()))
                apvts.removeParameterListener(p->getParameterID(), this);
}

//==============================================================================
const juce::String PluginProcessor::getName() const
{
    // UTF-8 Greek lowercase mu (μ, U+03BC) prefix — matches the AboutOverlay
    // logo and the user-facing branding everywhere else in the UI.
    return juce::String(juce::CharPointer_UTF8("\xce\xbc-Clid"));
}
double PluginProcessor::getTailLengthSeconds() const
{
    // cover worst-case wet tail so hosts don't crop the audio after transport stop.
    // DelaySlot::MaxDelaySamples = 4 s at 192 kHz, and the largest reverb preset can decay
    // for several seconds via Signalsmith's FDN. Fixed 10 s is conservative but safe and
    // avoids coupling the plugin's tail-length contract to FX-internal state.
    return 10.0;
}

int PluginProcessor::getNumPrograms() { return 1; }
int PluginProcessor::getCurrentProgram() { return 0; }
void PluginProcessor::setCurrentProgram(int) {}
const juce::String PluginProcessor::getProgramName(int) { return {}; }
void PluginProcessor::changeProgramName(int, const juce::String&) {}

//==============================================================================
void PluginProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    currentBlockSize  = samplesPerBlock;
    sequencerPlaying.store(false);   // no block has been processed since this prepare: a load now applies at once

    fxChain.prepare(sampleRate, samplesPerBlock);
    mixerEngine.prepare(sampleRate, samplesPerBlock);
    samples.prepare(samplesPerBlock, sampleRate);

    // The shared driver's buffers and every slot's rhythm (SampleRhythm::prepare: its voice and MIDI engines); the
    // retire bin is flushed, so a mid-stream rate or block-size change leaves no tail on stale sizing.
    prepareLayerRender(sampleRate, samplesPerBlock);
}

void PluginProcessor::releaseResources()
{
    sequencerPlaying.store(false);   // the host stopped calling processBlock: nothing will flag a staged swap
    samples.releaseResources();
    releaseLayerRender();            // the retire bin is flushed: no tail outlives the stream it played in
}

// One block: the transport (mu-Clid's own resolve, every host block), then the shared driver (renderAndMix): one
// try-lock of the rhythms, their render in chunks of the prepared size, the mixer. The per-block glue (the step
// clock, the hits, the swap boundaries, the play state, the MIDI out) runs in the driver's hooks below, only in a
// block that renders; a block that misses the lock or meets an edit in flight is silent and nothing advances.
void PluginProcessor::processBlock(juce::AudioBuffer<float>& buffer,
                                   juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;

    // The driver captures the sidechain and clears the master bus chunk by chunk; the other buses are cleared here
    // (the sidechain input shares only the master bus's channels).
    const int masterChannels = getBusBuffer(buffer, false, kMasterBusIndex).getNumChannels();
    for (int c = masterChannels; c < buffer.getNumChannels(); ++c)
        buffer.clear(c, 0, buffer.getNumSamples());

    renderThroughLayers(buffer, midiMessages, deriveTransport(buffer, midiMessages));
}

void PluginProcessor::sendOrphanOffs(juce::MidiBuffer& midiMessages)
{
    for (int i = 0; i < numOrphanOffs; ++i)
        midiMessages.addEvent(juce::MidiMessage::noteOff(orphanOffs[(size_t) i].channel, orphanOffs[(size_t) i].note), 0);
    numOrphanOffs = 0;
}

// The layer chain: the block's transport as the driver reads it (the beat after mu-Clid's own resolve, so a
// sub-step dip is held as before), the host's buses, then renderAndMix. The per-block glue runs in its hooks.
void PluginProcessor::renderThroughLayers(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages,
                                          const BlockTransport& transport)
{
    const int n = buffer.getNumSamples();
    mu_core::BlockTransport t;
    t.playing        = transport.playing;
    t.bpm            = transport.bpm;
    t.startBeat      = transport.beatPos;
    t.beatsPerSample = currentSampleRate > 0.0 ? (transport.bpm / 60.0) / currentSampleRate : 0.0;
    t.blockBeats     = transport.playing ? t.beatsPerSample * (double) n : 0.0;

    auto masterBus = getBusBuffer(buffer, false, kMasterBusIndex);
    std::array<juce::AudioBuffer<float>, MixerEngine::MaxChannels> directBufs;
    std::array<juce::AudioBuffer<float>*, MixerEngine::MaxChannels> directPtrs {};
    for (int i = 0; i < MixerEngine::MaxChannels; ++i)
    {
        const int busIndex = kFirstDirectOutBus + i;
        if (busIndex < getBusCount(false))
            if (auto* bus = getBus(false, busIndex); bus != nullptr && bus->isEnabled())
            {
                directBufs[(size_t) i] = getBusBuffer(buffer, false, busIndex);
                directPtrs[(size_t) i] = &directBufs[(size_t) i];
            }
    }
    juce::AudioBuffer<float>  fxRetBuf;
    juce::AudioBuffer<float>* fxRetPtr = nullptr;
    if (kFXReturnsBusIndex < getBusCount(false))
        if (auto* bus = getBus(false, kFXReturnsBusIndex); bus != nullptr && bus->isEnabled())
        {
            fxRetBuf = getBusBuffer(buffer, false, kFXReturnsBusIndex);
            fxRetPtr = &fxRetBuf;
        }

    renderAndMix(masterBus, midiMessages, t, &directPtrs, fxRetPtr);
}

// Start of a rendered block (under the layer lock, before any rhythm renders), in the order the processor chain had:
// the owed note-offs, the hits, the swap boundaries, the play state. The rhythms then fire their sample hits and run
// the modulation pass in their render (SampleRhythm::render).
void PluginProcessor::layersBlockStarting(const mu_core::LayerView& view, const mu_core::BlockTransport& t,
                                          juce::MidiBuffer& midiMessages)
{
    // An offline render applies the queued parameter changes here, at a block boundary, so the same automation lands
    // on the same block every time instead of whenever the 100 Hz poll runs. It runs under the layer lock because it
    // writes Rhythm and VoiceEngine slots that structural edits and hot-swap commits replace. It may allocate and
    // wait (bounded): nothing is playing in real time.
    if (isNonRealtime())
        servicePendingRhythmParams(true);

    const int numRhythms = juce::jlimit(0, (int) RhythmSequencer::MaxRhythms, (int) view.numLive);
    sendOrphanOffs(midiMessages);

    // Each live rhythm starts the block with no hit; the cursors then find this block's.
    for (int r = 0; r < numRhythms; ++r)
        sampleRhythm(r).block = {};

    if (t.playing)
    {
        std::array<CursorSteps, RhythmSequencer::MaxRhythms> cursorSteps;
        const auto blockResult = advanceCursors(numRhythms, t.startBeat, cursorSteps);

        // Phase 1: each rhythm that has a hit takes it (a MIDI note-on now, a sample hit at its render).
        for (int r = 0; r < numRhythms; ++r)
            if (blockResult.firedMask & (1 << r))
                sampleRhythm(r).takeHit((blockResult.accentMask & (1 << r)) != 0, (blockResult.tiedMask & (1 << r)) != 0,
                                        midiMessages);

        // Hot-swap: check if any staged rhythm preset should be committed at this boundary.
        if (hotSwapStager.checkBoundaries(numRhythms, blockResult.masterLoopWrapped, blockResult.rhythmLoopWrapMask))
            requestDeferredWork();

        // Update UI play-state atomics.
        const float frac = static_cast<float>(std::fmod(t.startBeat / RhythmSequencer::StepLengthBeats, 1.0));
        beatFraction.store(frac);
        for (int r = 0; r < numRhythms; ++r)
        {
            const auto& cs = cursorSteps[(size_t) r];
            sampleRhythm(r).publishPlayState(cs.step, cs.accentStep, cs.length, frac,
                                             (blockResult.firedMask & (1 << r)) != 0);
        }
    }

    // The modulation pass's inputs: the rhythm's mixer channel and the clock's step.
    for (int r = 0; r < numRhythms; ++r)
    {
        auto& in = sampleRhythm(r).block;
        in.strip = &mixerEngine.channels[(size_t) r];
        in.lastEffectiveStep = stepClock.lastEffective();
    }
}

// End of a rendered block (under the layer lock, after the mixer): the sample preview on the master bus, in the
// chunks the driver rendered, then each live rhythm's MIDI note-off countdown over the host block.
void PluginProcessor::layersBlockRendered(const mu_core::LayerView& view, juce::AudioBuffer<float>& masterBus,
                                          juce::MidiBuffer& midiMessages)
{
    const int total = masterBus.getNumSamples();
    const int chunkMax = juce::jmax(1, currentBlockSize);
    for (int start = 0; start < total; start += chunkMax)
    {
        const int len = juce::jmin(chunkMax, total - start);
        juce::AudioBuffer<float> masterView(masterBus.getArrayOfWritePointers(), masterBus.getNumChannels(), start, len);
        samples.mixPreviewInto(masterView, len);
    }

    const int numRhythms = juce::jlimit(0, (int) RhythmSequencer::MaxRhythms, (int) view.numLive);
    for (int r = 0; r < numRhythms; ++r)
        sampleRhythm(r).midi.processBlock(midiMessages, total);
}

//==============================================================================
// The block's transport, read on every host block (rendered or not).
//==============================================================================
PluginProcessor::BlockTransport
PluginProcessor::deriveTransport(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    // MIDI clock sync: scan system real-time messages before beat-pos determination.
    const double midiClockBlockBeatPos =
        (wrapperType == wrapperType_Standalone)
            ? midiClockSync.process(midiMessages, buffer.getNumSamples(), currentSampleRate)
            : 0.0;

    // MIDI program change → preset load. Scan + FIFO + drain all live on
    // ProcessorBase (mu-core). The virtuals applyMidiPresetLayer / applyFullMidiPreset
    // below dispatch to the mu-clid-specific stageRhythmPreset / loadPreset.
    queueMidiProgramChanges(midiMessages);

    double beatPos = 0.0;
    bool   playing = false;
    double bpm     = internalBpm.load(std::memory_order_relaxed);   // this block's tempo (FX sync)

    const int  noteMode = midiNoteMode.load(std::memory_order_relaxed);
    const bool isPlugin = (wrapperType != wrapperType_Standalone);

    // Read (and publish for the UI) the host transport once per block, whatever the mode.
    const auto host = pollHostTransport();

    if (noteMode == 1 && isPlugin)
    {
        // Note mode: scan Note On/Off to gate play state. First Note On resets
        // sequences to beat 0 (like pressing Play in standalone). All notes released
        // stops the sequencer; envelopes and FX tails play out naturally.
        for (const auto& meta : midiMessages)
        {
            if (mu_midi::isNoteOn (meta))
            {
                // First held note starts playback from beat 0.
                if (midiHeldNotes.fetch_add(1, std::memory_order_relaxed) == 0)
                {
                    noteModeBeatPos.store(0.0, std::memory_order_relaxed);
                    noteModePlaying.store(true, std::memory_order_relaxed);
                }
            }
            else if (mu_midi::isNoteOff (meta))
            {
                const int prev = midiHeldNotes.fetch_sub(1, std::memory_order_relaxed);
                if (prev <= 1)
                {
                    midiHeldNotes  .store(0,     std::memory_order_relaxed);
                    noteModePlaying.store(false,  std::memory_order_relaxed);
                }
            }
        }

        // Use host BPM when available so tempo-synced FX track the DAW, else the BPM field.
        if (host.bpm > 0.0)
            bpm = host.bpm;

        playing = noteModePlaying.load(std::memory_order_relaxed);
        if (playing)
        {
            // noteModeBeatPos is written exclusively by this audio-thread path —
            // no CAS needed. Any future UI reset (e.g. "Reset to bar 0" button)
            // must use fetch_add on a tick accumulator to avoid a load+store race.
            const double pos = noteModeBeatPos.load(std::memory_order_relaxed);
            beatPos = pos;
            noteBlockTransport({});   // note mode runs its own clock: quantised control actions fire at once
            noteModeBeatPos.store(
                pos + (buffer.getNumSamples() / currentSampleRate) * (bpm / 60.0),
                std::memory_order_relaxed);
        }
    }
    else
    {
        // Free mode (default): the family transport rule (mu-core TransportResolver) — the host /
        // mu-link position, else (standalone) external MIDI clock, else the own transport.
        const auto t = mu_core::resolveTransport(host, ! isPlugin,
                                                 midiClockSync, midiClockBlockBeatPos,
                                                 { internalPlaying, internalBpm, internalBeatPos, &nudgePhaseBeats, &nudgeDirection, &appliedSyncOffsetBeats },
                                                 buffer.getNumSamples(), currentSampleRate, 0.0, getSyncOffsetMs());
        playing = t.playing;
        beatPos = t.startBeat;
        bpm     = t.bpm;
        noteBlockTransport(t);   // lets the next block's quantised control actions find the beat
    }

    // detect transport stop→start edge and reset the sequencer's wrap detector
    // so the first block after restart can't emit a false masterLoopWrapped from
    // a stale lastEffectiveStep.
    {
        // A running transport that dips back less than a step is held in place rather than reset; a real
        // backward jump resets the detector so it cannot read as a master-loop wrap and commit a staged
        // hot-swap early (see RhythmSequencer::resolveBlockBeat).
        const auto resolved = RhythmSequencer::resolveBlockBeat(lastBeatPos.load(), beatPos, playing,
                                                                sequencerPlaying.load(), sequencer.getMasterLoopSteps());
        beatPos = resolved.beat;
        if (resolved.resetWrap)
        {
            sequencer.resetWrapDetector();
            stepClock.resetWrapDetector();
        }
    }
    sequencerPlaying.store(playing);
    lastBeatPos.store(beatPos);

    return { playing, beatPos, bpm };
}

// The host clock's frame for the block, then each live rhythm's cursor walk, gathered into the sequencer's
// BlockResult shape. Like RhythmSequencer::processBlock, nothing runs (no history consumed) with no live rhythm, and
// the frame's step becomes the previous one only after every cursor walked.
BlockResult PluginProcessor::advanceCursors(int numRhythms, double beatPos,
                                            std::array<CursorSteps, RhythmSequencer::MaxRhythms>& steps)
{
    BlockResult result;
    if (numRhythms <= 0)
        return result;

    const StepFrame frame = stepClock.advance(beatPos);
    result.masterLoopWrapped = frame.masterLoopWrapped;
    for (int r = 0; r < numRhythms; ++r)
    {
        auto& cursor = sampleRhythm(r).cursor;
        const auto hit = cursor.advance(frame);
        if (hit.fired)       result.firedMask          |= 1 << r;
        if (hit.accented)    result.accentMask         |= 1 << r;
        if (hit.tied)        result.tiedMask           |= 1 << r;
        if (hit.loopWrapped) result.rhythmLoopWrapMask |= 1 << r;
        steps[(size_t) r] = { cursor.lastStep(), cursor.lastAccentStep(), cursor.patternLength() };
    }
    stepClock.endBlock(frame);
    return result;
}

//==============================================================================
bool PluginProcessor::hasEditor() const { return true; }

//==============================================================================
void PluginProcessor::setMultiBusEnabled(bool on)
{
    multiBusEnabled.store(on, std::memory_order_relaxed);
    appSettings->setValue("multiBusEnabled", on);
    appSettings->saveIfNeeded();
}

void PluginProcessor::setMidiNoteMode(int mode)
{
    midiNoteMode.store(mode, std::memory_order_relaxed);
    if (mode == 0)
    {
        // Switching back to Free: clear any held-note state so the next Free-mode
        // block doesn't see stale noteModePlaying = true from a prior Note session.
        midiHeldNotes  .store(0,     std::memory_order_relaxed);
        noteModePlaying.store(false, std::memory_order_relaxed);
        noteModeBeatPos.store(0.0,   std::memory_order_relaxed);
    }
    appSettings->setValue("midiNoteMode", mode);
    appSettings->saveIfNeeded();
}

bool PluginProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    // Sidechain input bus: at most one, must be stereo or disabled.
    const auto& ins = layouts.inputBuses;
    if (ins.size() > 1) return false;
    if (ins.size() == 1 && ins.getReference(0) != juce::AudioChannelSet::stereo()
                        && ins.getReference(0) != juce::AudioChannelSet::disabled())
        return false;

    const auto& outs = layouts.outputBuses;
    if (outs.size() < 1 || outs.size() > kTotalBuses)
        return false;

    // Multi-bus disabled: only allow a single stereo output.
    if (! multiBusEnabled.load(std::memory_order_relaxed) && outs.size() > 1)
        return false;

    // Each declared output bus must be either stereo or disabled.
    for (int i = 0; i < outs.size(); ++i)
    {
        const auto& set = outs.getReference(i);
        if (set != juce::AudioChannelSet::stereo() && set != juce::AudioChannelSet::disabled())
            return false;
    }

    // Master bus (0) must be active — disabling it would leave nowhere for the master mix.
    if (outs.getReference(0) == juce::AudioChannelSet::disabled())
        return false;

    return true;
}

//==============================================================================
void PluginProcessor::commitDeferredWork()
{
    // Apply the rhythm parameter changes that arrived off the message thread (VST3 automation).
    servicePendingRhythmParams();

    // Commit the pending swaps (the retire bin has already freed what drained, at the top of this pass).
    // ProcessorBase then drains the MIDI program-change queue.
    hotSwapStager.processSwaps();
}

//==============================================================================

} // namespace mu_clid
