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
    sequencer.setNumRhythms(1);
    sequencer.updatePattern(0);
    voiceEngines[0] = std::make_unique<VoiceEngine>();
    voiceEngines[0]->insertProc.setSlotIndex(0);   // per-slot noise seed: layers do not share one sequence
    // Stage 34 Step 2: C++17 std::atomic<bool> default ctor leaves the value
    // indeterminate. Initialise every retired-cleanup flag to false so the
    // audio thread + drain block never observe a spurious "ready" state on an
    // empty slot. Retired slots stay null and flags stay false until a swap retires an engine.
    for (auto& slotArr : retiredReadyForCleanup)
        for (auto& flag : slotArr)
            flag.store(false, std::memory_order_relaxed);
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
    const int n = numLayers.load(std::memory_order_acquire);
    for (int i = 0; i < n; ++i)
    {
        voiceEngines[i]->prepareToPlay(sampleRate, samplesPerBlock);
        midiEngines[i].prepare(sampleRate, samplesPerBlock);
    }
    // Stage 34: re-prepare any populated retired engines too so a mid-stream
    // sample-rate or block-size change doesn't leave a tail-out engine
    // operating on stale buffer sizing. No-op in Step 2 (all slots null).
    for (auto& slotArr : retiredVoiceEngines)
        for (auto& engine : slotArr)
            if (engine)
                engine->prepareToPlay(sampleRate, samplesPerBlock);

    fxChain.prepare(sampleRate, samplesPerBlock);
    mixerEngine.prepare(sampleRate, samplesPerBlock);
    samples.prepare(samplesPerBlock, sampleRate);
}

void PluginProcessor::releaseResources()
{
    sequencerPlaying.store(false);   // the host stopped calling processBlock: nothing will flag a staged swap
    samples.releaseResources();
}

void PluginProcessor::processBlock(juce::AudioBuffer<float>& buffer,
                                   juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;

    // Preserve the DAW sidechain, then clear (shared — the SC input bus shares buffer
    // channels with the master output, so a bare clear would wipe it → no ducking /
    // dead GR). The mixer keeps a private copy.
    captureSidechainAndClear(buffer);

    const BlockTransport transport = deriveTransport(buffer, midiMessages);
    const bool   playing = transport.playing;
    const double beatPos = transport.beatPos;

    // layersLock is the barrier against a structural edit (suspendProcessing is not one in every host);
    // layerEditInFlight additionally covers the edit's parameter moves after the lock is dropped. Either
    // way the block is silent and nothing here advances, as on any missed lock.
    const juce::ScopedTryLock rLock(layersLock);
    if (!rLock.isLocked() || layerEditInFlight.load(std::memory_order_acquire))
    {
        buffer.clear();
        return;
    }

    // An offline render applies the queued parameter changes here, at a block boundary, so the same
    // automation lands on the same block every time instead of whenever the 100 Hz poll runs. It runs
    // under layersLock because it writes Rhythm and VoiceEngine slots that structural edits and hot-swap
    // commits replace. It may allocate and wait (bounded): nothing is playing in real time.
    if (isNonRealtime())
        servicePendingRhythmParams(true);

    // Must read the layer count AFTER acquiring layersLock — otherwise the snapshot
    // can be stale relative to the layer set and `sequencer.getRhythm(r)` indexes out of range.
    const int numRhythms = numLayers.load(std::memory_order_acquire);

    // Send the note-offs owed by slots torn down since the last block, ahead of any new hit.
    for (int i = 0; i < numOrphanOffs; ++i)
        midiMessages.addEvent(juce::MidiMessage::noteOff(orphanOffs[(size_t) i].channel, orphanOffs[(size_t) i].note), 0);
    numOrphanOffs = 0;

    if (playing)
        advanceSequencer(numRhythms, beatPos, midiMessages);

    // Apply modulation: each rhythm computes its modulated VoiceParams, strip values and Euclid overrides
    // from its matrix. Try-locks only, so the audio thread never blocks on the message thread.
    for (int r = 0; r < numRhythms; ++r)
        sampleRhythm(r).applyModulation(beatPos, mixerEngine.channels[(size_t) r], sequencer, r,
                                        voiceEngines[(size_t) r].get());

    renderAudioBuses(buffer, midiMessages, numRhythms, transport.bpm);
}

//==============================================================================
// processBlock phases. Behaviour-preserving extraction of the full-build
// audio-thread path; called in order from processBlock under its layersLock.
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
            sequencer.resetWrapDetector();
    }
    sequencerPlaying.store(playing);
    lastBeatPos.store(beatPos);

    return { playing, beatPos, bpm };
}

void PluginProcessor::advanceSequencer(int numRhythms, double beatPos, juce::MidiBuffer& midiMessages)
{
    const auto blockResult = sequencer.processBlock(beatPos);
    // Phase 1: each rhythm the sequencer fired plays its hit (its sample, or in MIDI output mode one note-on).
    for (int r = 0; r < numRhythms; ++r)
        if (blockResult.firedMask & (1 << r))
            sampleRhythm(r).fireHit((blockResult.accentMask & (1 << r)) != 0, (blockResult.tiedMask & (1 << r)) != 0,
                                    midiMessages, midiEngines[(size_t) r], voiceEngines[(size_t) r].get());

    // Hot-swap: check if any staged rhythm preset should be committed at this boundary.
    if (hotSwapStager.checkBoundaries(numRhythms, blockResult.masterLoopWrapped,
                                      blockResult.rhythmLoopWrapMask))
        requestDeferredWork();

    // Update UI play-state atomics.
    const float frac = static_cast<float>(
        std::fmod(beatPos / RhythmSequencer::StepLengthBeats, 1.0));
    beatFraction.store(frac);
    for (int r = 0; r < numRhythms; ++r)
        sampleRhythm(r).publishPlayState(rhythmPlayState[(size_t) r], sequencer.getLastStepIndex(r),
                                         sequencer.getLastAccentStepIndex(r), sequencer.getPatternLength(r), frac,
                                         (blockResult.firedMask & (1 << r)) != 0);
}


void PluginProcessor::renderAudioBuses(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages,
                                       int numRhythms, double effectiveBpm)
{
    // Gather the host's output bus buffers. Buses we declared in BusesProperties may
    // be disabled in the host's chosen layout — skip those (the channel routes silently).
    auto masterBus = getBusBuffer(buffer, false, kMasterBusIndex);

    std::array<juce::AudioBuffer<float>, 8> directBufs;
    std::array<juce::AudioBuffer<float>*, 8> directPtrs {};
    for (int i = 0; i < 8; ++i)
    {
        const int busIndex = kFirstDirectOutBus + i;
        if (busIndex < getBusCount(false))
            if (auto* bus = getBus(false, busIndex))
                if (bus->isEnabled())
                {
                    directBufs[(size_t) i] = getBusBuffer(buffer, false, busIndex);
                    directPtrs[(size_t) i] = &directBufs[(size_t) i];
                }
    }

    juce::AudioBuffer<float>  fxRetBuf;
    juce::AudioBuffer<float>* fxRetPtr = nullptr;
    if (kFXReturnsBusIndex < getBusCount(false))
        if (auto* bus = getBus(false, kFXReturnsBusIndex))
            if (bus->isEnabled())
            {
                fxRetBuf = getBusBuffer(buffer, false, kFXReturnsBusIndex);
                fxRetPtr = &fxRetBuf;
            }

    // (External DAW sidechain already captured at the top of processBlock, before the
    // clear — the input bus shares channels with the master output, so reading it here
    // post-clear would yield silence.)

    // Stage 34 Step 2: pass the retired-voice descriptor through. The 2D arrays
    // (MaxRhythms × kMaxRetiredEngines) are stored contiguously row-major in
    // std::array<std::array<...>>, so taking .data() of row 0 yields the start
    // of the flat matrix that MixerEngine indexes as `[r * perSlot + i]`. In
    // Step 2 every slot is null and every flag is false, so the inner body never
    // runs — descriptor is wired so Step 3 just has to populate slots.
    RetiredVoices retiredDesc {
        retiredVoiceEngines[0].data(),
        retiredReadyForCleanup[0].data(),
        kMaxRetiredEngines
    };

    // A host may send a block longer than the size prepared for; the mixer and the voices size their
    // scratch buffers for the prepared size only. So the audio render runs in chunks of at most that
    // size, each on views of the host's buses (no allocation: the view keeps its channel pointers inline).
    const int total = buffer.getNumSamples();
    const int chunkMax = juce::jmax(1, currentBlockSize);
    for (int start = 0; start < total; start += chunkMax)
    {
        const int len = juce::jmin(chunkMax, total - start);
        auto view = [start, len](juce::AudioBuffer<float>& b) {
            return juce::AudioBuffer<float>(b.getArrayOfWritePointers(), b.getNumChannels(), start, len); };

        auto masterView = view(masterBus);
        std::array<juce::AudioBuffer<float>, 8> directViews;
        std::array<juce::AudioBuffer<float>*, 8> directViewPtrs {};
        for (size_t i = 0; i < 8; ++i)
            if (directPtrs[i] != nullptr)
            {
                directViews[i]    = view(*directPtrs[i]);
                directViewPtrs[i] = &directViews[i];
            }
        juce::AudioBuffer<float> fxRetView;
        if (fxRetPtr != nullptr) fxRetView = view(*fxRetPtr);

        processCoreBlock(masterView, voiceEngines.data(), numRhythms,
                         len, effectiveBpm, &directViewPtrs, fxRetPtr != nullptr ? &fxRetView : nullptr,
                         &retiredDesc);
        samples.mixPreviewInto(masterView, len);
    }

    for (int r = 0; r < numRhythms; ++r)
        midiEngines[r].processBlock(midiMessages, buffer.getNumSamples());
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

    // Drain retired engines + commit pending swaps (both sides of the hot-swap lifecycle).
    // ProcessorBase then drains the MIDI program-change queue.
    hotSwapStager.processSwaps();
}

//==============================================================================

} // namespace mu_clid
