#include "Audio/SpinLock.h"   // mu-core: spin lock helpers
#include "PluginProcessor.h"
#include "Plugin/TransportResolver.h"   // mu-core: the family transport rule
#include "License/ProductLicensing.h"   // mu-core: ProcessorBase::initLicensing (licensed products only)
#include "PluginProcessor_Internal.h"
#include "Audio/InsertSlotConfig.h"
#include "Modulation/ModulationSkew.h"  // proportion-space skew helpers (shared with test C5)
#include "Modulation/MuClidModDest.h"  // mu-clid modulation targets
#if MUCLID_LITE_BUILD
#include "UI/LiteEditor.h"
#else
#include "PluginEditor.h"
#endif
#include "Sequencer/Rhythm.h"
#include "Audio/FX/Slots/FXAlgorithmDef.h"

#include <thread>   // std::this_thread::yield in modulator deserialise lock-spin

namespace mu_clid {

// Declare one stereo sidechain input (disabled by default; DAW enables when the user
// wires an external signal) + 10 stereo output buses: Master (always enabled),
// Out 1..8 + FX Returns (disabled by default, matching pre-multi-bus behaviour).
PluginProcessor::PluginProcessor()
#if MUCLID_LITE_BUILD
    : ProcessorBase(BusesProperties(), createParameterLayout(), juce::Identifier("MuClidState"))
#else
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
#endif
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

   #if !MUCLID_LITE_BUILD
    // Offline licence + online activation (shared; after initAppSettings for getContentDir).
    initLicensing({ mu_clid::kLicenseProductId, mu_clid::kLicenseFilename,
                    mu_clid::kLicensePublicKey, "muclid.activation" });
   #endif
    sequencer.setStepCap(maxSteps(HitGenerator::kMaxSteps));   // 16 in demo

    // Listen to the product parameters (rhythm + mstrLoop); mixer / FX ids are synced by ProcessorBase.
    for (auto* param : getParameters())
        if (auto* p = dynamic_cast<juce::AudioProcessorParameterWithID*>(param))
            if (! isFxParamId(p->getParameterID()))
                apvts.addParameterListener(p->getParameterID(), this);
    startFxParamSync();

    // Pre-populate modulation param map so lookups never allocate on the audio thread.
    modParamValues.reserve(50);
    for (const char* key : { "amp.attack", "amp.decay", "amp.sustain",  // amp.release retired
                              "filter.cutoff", "filter.resonance",
                              "fenv.attack", "fenv.decay", "fenv.depth",
                              "pitch.semitones", "pitch.octave",
                              // Stage 36: 4 generic insert slots — semantics per
                              // active algorithm via mu_ui::kInsertAlgoSlots.
                              "insert.p1", "insert.p2", "insert.p3", "insert.p4",
                              "pitch.envDepth", "amp.level", "accentDb",
                              "euclid.a.hits", "euclid.a.rotate",
                              "euclid.a.prePad", "euclid.a.postPad",
                              "euclid.a.insSt", "euclid.a.insLen",
                              "euclid.b.hits", "euclid.b.rotate",
                              "euclid.b.prePad", "euclid.b.postPad",
                              "euclid.b.insSt", "euclid.b.insLen",
                              "euclid.c.hits", "euclid.c.rotate",
                              "euclid.c.prePad", "euclid.c.postPad",
                              "euclid.c.insSt", "euclid.c.insLen",
                              "filter.lowCut",
                              "amp.pan", "send.effect", "send.delay", "send.reverb" })
        modParamValues[key] = 0.0f;
    // Point each destination's slot at its map value (no rehash after this: keys are fixed).
    for (auto& [key, value] : modParamValues)
        if (const int i = ModDest::indexOf(key); i >= 0)
            modSlot[(size_t) i] = &value;

    // Add default rhythm (16 steps, 4 hits) and sync its state to APVTS.
    Rhythm defaultRhythm;
    defaultRhythm.name       = "<unnamed>";
    defaultRhythm.genA.steps = 16;
    defaultRhythm.genA.hits  = 4;
    sequencer.addRhythm(defaultRhythm);
#if !MUCLID_LITE_BUILD
    voiceEngines[0] = std::make_unique<VoiceEngine>();
#endif
    // Stage 34 Step 2: C++17 std::atomic<bool> default ctor leaves the value
    // indeterminate. Initialise every retired-cleanup flag to false so the
    // audio thread + drain block never observe a spurious "ready" state on an
    // empty slot. Unconditional (outside the LITE guard) so Lite's handle-
    // AsyncUpdate path can't read an indeterminate atomic either. Retired
    // slots stay null and flags stay false until Step 3 wires retire-on-swap.
    for (auto& slotArr : retiredReadyForCleanup)
        for (auto& flag : slotArr)
            flag.store(false, std::memory_order_relaxed);
    numActiveRhythms.store(1, std::memory_order_release);

#if MUCLID_LITE_BUILD
    // Cache LITE-only APVTS atomic pointers so processBlock doesn't pay a per-block
    // hash lookup + StringRef materialise for these two reads.
    liteMidiNotePtr  = apvts.getRawParameterValue("lite_midiNote");
    liteAccentAmtPtr = apvts.getRawParameterValue("lite_accentAmt");
#endif

    {
        mu_core::ScopedApvtsLoading guard(apvtsLoading);
        pushRhythmToAPVTS(0);
    }

#if !MUCLID_LITE_BUILD
    // Ensure user content folders exist and restore the saved default preset (skipped by render mode).
    ensureContentFoldersExist();
    loadStartupDefault();
#endif
}

PluginProcessor::~PluginProcessor()
{
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
#if MUCLID_LITE_BUILD
    return juce::String(juce::CharPointer_UTF8("\xce\xbc-Clid Lite"));
#else
    return juce::String(juce::CharPointer_UTF8("\xce\xbc-Clid"));
#endif
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
    const int n = numActiveRhythms.load(std::memory_order_acquire);
    for (int i = 0; i < n; ++i)
    {
#if !MUCLID_LITE_BUILD
        voiceEngines[i]->prepareToPlay(sampleRate, samplesPerBlock);
#endif
        midiEngines[i].prepare(sampleRate, samplesPerBlock);
    }
#if !MUCLID_LITE_BUILD
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
#endif
}

void PluginProcessor::releaseResources()
{
    samples.releaseResources();
}

void PluginProcessor::processBlock(juce::AudioBuffer<float>& buffer,
                                   juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;

    // Preserve the DAW sidechain, then clear (shared — the SC input bus shares buffer
    // channels with the master output, so a bare clear would wipe it → no ducking /
    // dead GR). No-op in the Lite build (no buses); the mixer keeps a private copy.
    captureSidechainAndClear(buffer);

#if MUCLID_LITE_BUILD
    // Lite mode: MIDI-only sequencing, no audio processing.
    const auto transport = computeLiteTransport(buffer.getNumSamples());

    const juce::ScopedTryLock rLock(rhythmsLock);
    if (!rLock.isLocked()) return;
    const int numRhythms = numActiveRhythms.load(std::memory_order_acquire);

    advanceLiteSequencer(numRhythms, transport.playing, transport.beatPos,
                         midiMessages, buffer.getNumSamples());
#else

    const BlockTransport transport = deriveTransport(buffer, midiMessages);
    const bool   playing = transport.playing;
    const double beatPos = transport.beatPos;

    const juce::ScopedTryLock rLock(rhythmsLock);
    if (!rLock.isLocked())
    {
        buffer.clear();
        return;
    }

    // Must read numActiveRhythms AFTER acquiring rhythmsLock — otherwise the snapshot
    // can be stale relative to the vector state and `sequencer.getRhythm(r)` indexes
    // out of bounds (MSVC _ITERATOR_DEBUG_LEVEL → abort()).
    const int numRhythms = numActiveRhythms.load(std::memory_order_acquire);

    if (playing)
        advanceSequencer(numRhythms, beatPos);

    // Apply modulation: compute per-rhythm modulated VoiceParams from the modulation matrix.
    // Uses a try-lock so the audio thread never blocks on the message thread.
    for (int r = 0; r < numRhythms; ++r)
        applyRhythmModulation(r, beatPos);

    renderAudioBuses(buffer, midiMessages, numRhythms, transport.bpm);
#endif
}

#if MUCLID_LITE_BUILD
//==============================================================================
// Lite processBlock helpers.
//==============================================================================
PluginProcessor::BlockTransport PluginProcessor::computeLiteTransport(int numSamples)
{
    // The family transport rule (mu-core TransportResolver): Lite runs in a host, so play, tempo
    // and beat follow it (its own transport only when a host gives no position at all).
    const auto t = mu_core::resolveTransport(pollHostTransport(),
                                             wrapperType == wrapperType_Standalone, midiClockSync, 0.0,
                                             { internalPlaying, internalBpm, internalBeatPos },
                                             juce::jmax(1, numSamples), currentSampleRate);
    const bool   playing = t.playing;
    const double beatPos = t.startBeat;

    sequencerPlaying.store(playing);
    lastBeatPos.store(beatPos);
    return { playing, beatPos };
}

void PluginProcessor::advanceLiteSequencer(int numRhythms, bool playing, double beatPos,
                                            juce::MidiBuffer& midi, int numSamples)
{
    if (playing)
    {
        const auto blockResult = sequencer.processBlock(beatPos);
        const int   midiNote  = (int) liteMidiNotePtr->load();
        const float accentAmt = liteAccentAmtPtr->load();

        // Bottom half (0–50): accented ramps 100→127, non-accented stays 100.
        // Top half  (50–100): accented stays 127, non-accented ramps 100→75.
        float accentedVel, normalVel;
        if (accentAmt <= 50.0f)
        {
            const float t = accentAmt / 50.0f;
            accentedVel = (100.0f + t * 27.0f) / 127.0f;
            normalVel   =  100.0f               / 127.0f;
        }
        else
        {
            const float t = (accentAmt - 50.0f) / 50.0f;
            accentedVel = 1.0f;
            normalVel   = (100.0f - t * 25.0f)  / 127.0f;
        }

        for (int r = 0; r < numRhythms; ++r)
        {
            if (blockResult.firedMask & (1 << r))
            {
                const bool isAccented = (blockResult.accentMask & (1 << r)) != 0;
                midiEngines[r].trigger(midi, 0, midiNote, 1,
                                       isAccented ? accentedVel : normalVel);
                rhythmPlayState[r].hitCount.store(rhythmPlayState[r].hitCount.load() + 1);
            }
        }

        const float frac = static_cast<float>(
            std::fmod(beatPos / SequencerEngine::StepLengthBeats, 1.0));
        beatFraction.store(frac);
        for (int r = 0; r < numRhythms; ++r)
        {
            rhythmPlayState[r].currentStep  .store(sequencer.getLastStepIndex(r));
            rhythmPlayState[r].currentStepC .store(sequencer.getLastAccentStepIndex(r));
            rhythmPlayState[r].patternLength.store(sequencer.getPatternLength(r));
            const Rhythm& rhy = sequencer.getRhythm(r);
            rhythmPlayState[r].stepsA.store(juce::jmax(1, rhy.genA.steps));
            rhythmPlayState[r].stepsB.store(juce::jmax(1, rhy.genB.steps));
            rhythmPlayState[r].stepsC.store(juce::jmax(1, rhy.genC.steps));
        }
    }
    for (int r = 0; r < numRhythms; ++r)
        midiEngines[r].processBlock(midi, juce::jmax(1, numSamples));
}

#else
//==============================================================================
// processBlock phases. Behaviour-preserving extraction of the full-build
// audio-thread path; called in order from processBlock under its rhythmsLock.
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
    // ProcessorBase (mu-core). The virtuals applyMidiPresetSlot / applyFullMidiPreset
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
            const auto msg = meta.getMessage();
            if (msg.isNoteOn())
            {
                // First held note starts playback from beat 0.
                if (midiHeldNotes.fetch_add(1, std::memory_order_relaxed) == 0)
                {
                    noteModeBeatPos.store(0.0, std::memory_order_relaxed);
                    noteModePlaying.store(true, std::memory_order_relaxed);
                }
            }
            else if (msg.isNoteOff())
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
                                                 { internalPlaying, internalBpm, internalBeatPos },
                                                 buffer.getNumSamples(), currentSampleRate);
        playing = t.playing;
        beatPos = t.startBeat;
        bpm     = t.bpm;
    }

    // detect transport stop→start edge and reset the sequencer's wrap detector
    // so the first block after restart can't emit a false masterLoopWrapped from
    // a stale lastEffectiveStep.
    {
        const bool wasPlaying = sequencerPlaying.load();
        if (playing && !wasPlaying)
            sequencer.resetWrapDetector();
    }
    sequencerPlaying.store(playing);
    lastBeatPos.store(beatPos);

    return { playing, beatPos, bpm };
}

void PluginProcessor::advanceSequencer(int numRhythms, double beatPos)
{
    const auto blockResult = sequencer.processBlock(beatPos);
    for (int r = 0; r < numRhythms; ++r)
        if ((blockResult.firedMask & (1 << r)) && voiceEngines[r])
        {
            // pattern-legato gating — tiedMask is always populated by
            // the sequencer; the per-rhythm patternLegato flag decides
            // whether to act on it. Untied / legato-off hits go through
            // the standard retrigger path.
            const bool tied = sequencer.getRhythm(r).patternLegato
                           && (blockResult.tiedMask & (1 << r)) != 0;
            voiceEngines[r]->trigger(blockResult.accentMask & (1 << r), tied);
        }

    // Hot-swap: check if any staged rhythm preset should be committed at this boundary.
    if (hotSwapStager.checkBoundaries(numRhythms, blockResult.masterLoopWrapped,
                                      blockResult.rhythmLoopWrapMask))
        triggerAsyncUpdate();

    // Update UI play-state atomics.
    const float frac = static_cast<float>(
        std::fmod(beatPos / SequencerEngine::StepLengthBeats, 1.0));
    beatFraction.store(frac);
    for (int r = 0; r < numRhythms; ++r)
    {
        rhythmPlayState[r].currentStep  .store(sequencer.getLastStepIndex(r));
        rhythmPlayState[r].currentStepC .store(sequencer.getLastAccentStepIndex(r));
        rhythmPlayState[r].patternLength.store(sequencer.getPatternLength(r));
        const Rhythm& rhy = sequencer.getRhythm(r);
        rhythmPlayState[r].stepsA.store(juce::jmax(1, rhy.genA.steps));
        rhythmPlayState[r].stepsB.store(juce::jmax(1, rhy.genB.steps));
        rhythmPlayState[r].stepsC.store(juce::jmax(1, rhy.genC.steps));
        if (blockResult.firedMask & (1 << r))
        {
            rhythmPlayState[r].hitCount.store(rhythmPlayState[r].hitCount.load() + 1); // monotonic counter for race-free UI hit detection
        }
    }
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

    processCoreBlock(masterBus, voiceEngines.data(), numRhythms,
                     buffer.getNumSamples(), effectiveBpm, &directPtrs, fxRetPtr,
                     &retiredDesc);

    samples.mixPreviewInto(masterBus, buffer.getNumSamples());

    for (int r = 0; r < numRhythms; ++r)
        midiEngines[r].processBlock(midiMessages, buffer.getNumSamples());
}
#endif // MUCLID_LITE_BUILD

//==============================================================================
bool PluginProcessor::hasEditor() const { return true; }

juce::AudioProcessorEditor* PluginProcessor::createEditor()
{
#if MUCLID_LITE_BUILD
    return new LiteEditor(*this);
#else
    return new PluginEditor(*this);
#endif
}

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
#if MUCLID_LITE_BUILD
    // MIDI effect: no audio buses.
    return layouts.getMainInputChannelSet()  == juce::AudioChannelSet::disabled()
        && layouts.getMainOutputChannelSet() == juce::AudioChannelSet::disabled();
#else
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
#endif
}

//==============================================================================
void PluginProcessor::commitDeferredWork()
{
    // Drain retired engines + commit pending swaps (both sides of the hot-swap lifecycle).
    // ProcessorBase then drains the MIDI program-change queue.
    hotSwapStager.processSwaps();
}

//==============================================================================

} // namespace mu_clid
