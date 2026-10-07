#include "Audio/SpinLock.h"   // mu-core: spin lock helpers
#include "PluginProcessor.h"
#include "License/ProductLicensing.h"   // mu-core: ProcessorBase::initLicensing (licensed products only)
#include "PluginProcessor_Internal.h"
#include "Audio/InsertSlotConfig.h"
#include "Plugin/ModulationSkew.h"  // proportion-space skew helpers (shared with test C5)
#include "Modulation/MuClidModDest.h"  // mu-clid modulation targets
#if MUCLID_LITE_BUILD
#include "LiteEditor.h"
#else
#include "PluginEditor.h"
#endif
#include "Sequencer/Rhythm.h"
#include "Audio/FX/Slots/FXAlgorithmDef.h"

#include <thread>   // std::this_thread::yield in modulator deserialise lock-spin

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

    // Register listener for every parameter.
    for (auto* param : getParameters())
        if (auto* p = dynamic_cast<juce::AudioProcessorParameterWithID*>(param))
            apvts.addParameterListener(p->getParameterID(), this);

    // Initialise sample-path slots.
    for (int i = 0; i < SequencerEngine::MaxRhythms; ++i)
        loadedSamplePaths.add(juce::String());

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
    // Ensure user content folders exist and load the default preset if present.
    // Render mode (`--render` CLI) sets `skipAutoLoadDefault` to bypass this so
    // each test starts from a clean single-rhythm state rather than whatever
    // the user has saved as their personal default.
    ensureContentFoldersExist();
    if (! skipAutoLoadDefault)
        loadDefaultPreset();
#endif
}

PluginProcessor::~PluginProcessor()
{
    for (auto* param : getParameters())
        if (auto* p = dynamic_cast<juce::AudioProcessorParameterWithID*>(param))
            apvts.removeParameterListener(p->getParameterID(), this);
}

//==============================================================================
const juce::String PluginProcessor::getName() const
{
    // UTF-8 Greek lowercase mu (μ, U+03BC) prefix — matches the AboutPanel
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
    samplePreview.prepare(samplesPerBlock, sampleRate);
#endif
}

void PluginProcessor::releaseResources()
{
    samplePreview.releaseResources();
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

    const double effectiveBpm = deriveEffectiveBpm();
    renderAudioBuses(buffer, midiMessages, numRhythms, effectiveBpm);
#endif
}

#if MUCLID_LITE_BUILD
//==============================================================================
// Lite processBlock helpers.
//==============================================================================
PluginProcessor::BlockTransport PluginProcessor::computeLiteTransport(int numSamples)
{
    double beatPos = 0.0;
    bool   playing = false;

    if (auto* ph = getPlayHead())
    {
        if (auto pos = ph->getPosition())
        {
            playing = pos->getIsPlaying();
            if (auto ppq = pos->getPpqPosition())
                beatPos = *ppq;
        }
    }
    if (!playing && internalPlaying.load(std::memory_order_relaxed))
    {
        playing = true;
        const double pos = internalBeatPos.load(std::memory_order_relaxed);
        beatPos = pos;
        internalBeatPos.store(pos + (juce::jmax(1, numSamples) / currentSampleRate)
                                  * (internalBpm.load(std::memory_order_relaxed) / 60.0),
                              std::memory_order_relaxed);
    }

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

    const int  noteMode = midiNoteMode.load(std::memory_order_relaxed);
    const bool isPlugin = (wrapperType != wrapperType_Standalone);

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

        playing = noteModePlaying.load(std::memory_order_relaxed);
        if (playing)
        {
            // noteModeBeatPos is written exclusively by this audio-thread path —
            // no CAS needed. Any future UI reset (e.g. "Reset to bar 0" button)
            // must use fetch_add on a tick accumulator to avoid a load+store race.
            const double pos = noteModeBeatPos.load(std::memory_order_relaxed);
            beatPos = pos;
            // Use host BPM when available so tempo-synced FX tracks the DAW;
            // fall back to the internal transport BPM (set via the BPM field).
            double bpm = internalBpm.load(std::memory_order_relaxed);
            if (auto* ph = getPlayHead())
                if (auto phPos = ph->getPosition())
                    if (auto hostBpm = phPos->getBpm())
                        bpm = *hostBpm;
            noteModeBeatPos.store(
                pos + (buffer.getNumSamples() / currentSampleRate) * (bpm / 60.0),
                std::memory_order_relaxed);
        }
    }
    else
    {
        // Free mode (default): host transport drives play, with MIDI clock and
        // internal transport as fallbacks (existing behaviour).
        if (auto* ph = getPlayHead())
        {
            if (auto pos = ph->getPosition())
            {
                playing = pos->getIsPlaying();
                if (auto ppq = pos->getPpqPosition())
                    beatPos = *ppq;
            }
        }

        if (!playing && midiClockSync.isEnabled()
                     && wrapperType == wrapperType_Standalone
                     && (midiClockSync.isPlaying() || internalPlaying.load(std::memory_order_relaxed)))
        {
            playing = true;
            beatPos = midiClockBlockBeatPos;
        }
        else if (!playing && internalPlaying.load(std::memory_order_relaxed))
        {
            playing  = true;
            const double pos = internalBeatPos.load(std::memory_order_relaxed);
            beatPos  = pos;
            internalBeatPos.store(pos + (buffer.getNumSamples() / currentSampleRate) * (internalBpm.load(std::memory_order_relaxed) / 60.0),
                                  std::memory_order_relaxed);
        }
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

    return { playing, beatPos };
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

// Each modulation destination this file reads or writes, as its ModDest::kTable index (compile
// time). applyRhythmModulation reaches the values through modSlot[] — no per-block key hashing.
namespace md
{
    constexpr int accentDb          = ModDest::indexOf("accentDb");
    constexpr int amp_attack        = ModDest::indexOf("amp.attack");
    constexpr int amp_decay         = ModDest::indexOf("amp.decay");
    constexpr int amp_level         = ModDest::indexOf("amp.level");
    constexpr int amp_pan           = ModDest::indexOf("amp.pan");
    constexpr int amp_sustain       = ModDest::indexOf("amp.sustain");
    constexpr int euclid_a_hits     = ModDest::indexOf("euclid.a.hits");
    constexpr int euclid_a_insLen   = ModDest::indexOf("euclid.a.insLen");
    constexpr int euclid_a_insSt    = ModDest::indexOf("euclid.a.insSt");
    constexpr int euclid_a_postPad  = ModDest::indexOf("euclid.a.postPad");
    constexpr int euclid_a_prePad   = ModDest::indexOf("euclid.a.prePad");
    constexpr int euclid_a_rotate   = ModDest::indexOf("euclid.a.rotate");
    constexpr int euclid_b_hits     = ModDest::indexOf("euclid.b.hits");
    constexpr int euclid_b_insLen   = ModDest::indexOf("euclid.b.insLen");
    constexpr int euclid_b_insSt    = ModDest::indexOf("euclid.b.insSt");
    constexpr int euclid_b_postPad  = ModDest::indexOf("euclid.b.postPad");
    constexpr int euclid_b_prePad   = ModDest::indexOf("euclid.b.prePad");
    constexpr int euclid_b_rotate   = ModDest::indexOf("euclid.b.rotate");
    constexpr int euclid_c_hits     = ModDest::indexOf("euclid.c.hits");
    constexpr int euclid_c_insLen   = ModDest::indexOf("euclid.c.insLen");
    constexpr int euclid_c_insSt    = ModDest::indexOf("euclid.c.insSt");
    constexpr int euclid_c_postPad  = ModDest::indexOf("euclid.c.postPad");
    constexpr int euclid_c_prePad   = ModDest::indexOf("euclid.c.prePad");
    constexpr int euclid_c_rotate   = ModDest::indexOf("euclid.c.rotate");
    constexpr int fenv_attack       = ModDest::indexOf("fenv.attack");
    constexpr int fenv_decay        = ModDest::indexOf("fenv.decay");
    constexpr int fenv_depth        = ModDest::indexOf("fenv.depth");
    constexpr int filter_cutoff     = ModDest::indexOf("filter.cutoff");
    constexpr int filter_lowCut     = ModDest::indexOf("filter.lowCut");
    constexpr int filter_resonance  = ModDest::indexOf("filter.resonance");
    constexpr int insert_p1         = ModDest::indexOf("insert.p1");
    constexpr int insert_p2         = ModDest::indexOf("insert.p2");
    constexpr int insert_p3         = ModDest::indexOf("insert.p3");
    constexpr int insert_p4         = ModDest::indexOf("insert.p4");
    constexpr int pitch_envDepth    = ModDest::indexOf("pitch.envDepth");
    constexpr int pitch_octave      = ModDest::indexOf("pitch.octave");
    constexpr int pitch_semitones   = ModDest::indexOf("pitch.semitones");
    constexpr int send_delay        = ModDest::indexOf("send.delay");
    constexpr int send_effect       = ModDest::indexOf("send.effect");
    constexpr int send_reverb       = ModDest::indexOf("send.reverb");
    static_assert(accentDb >= 0, "accentDb is not in ModDest::kTable");
    static_assert(amp_attack >= 0, "amp.attack is not in ModDest::kTable");
    static_assert(amp_decay >= 0, "amp.decay is not in ModDest::kTable");
    static_assert(amp_level >= 0, "amp.level is not in ModDest::kTable");
    static_assert(amp_pan >= 0, "amp.pan is not in ModDest::kTable");
    static_assert(amp_sustain >= 0, "amp.sustain is not in ModDest::kTable");
    static_assert(euclid_a_hits >= 0, "euclid.a.hits is not in ModDest::kTable");
    static_assert(euclid_a_insLen >= 0, "euclid.a.insLen is not in ModDest::kTable");
    static_assert(euclid_a_insSt >= 0, "euclid.a.insSt is not in ModDest::kTable");
    static_assert(euclid_a_postPad >= 0, "euclid.a.postPad is not in ModDest::kTable");
    static_assert(euclid_a_prePad >= 0, "euclid.a.prePad is not in ModDest::kTable");
    static_assert(euclid_a_rotate >= 0, "euclid.a.rotate is not in ModDest::kTable");
    static_assert(euclid_b_hits >= 0, "euclid.b.hits is not in ModDest::kTable");
    static_assert(euclid_b_insLen >= 0, "euclid.b.insLen is not in ModDest::kTable");
    static_assert(euclid_b_insSt >= 0, "euclid.b.insSt is not in ModDest::kTable");
    static_assert(euclid_b_postPad >= 0, "euclid.b.postPad is not in ModDest::kTable");
    static_assert(euclid_b_prePad >= 0, "euclid.b.prePad is not in ModDest::kTable");
    static_assert(euclid_b_rotate >= 0, "euclid.b.rotate is not in ModDest::kTable");
    static_assert(euclid_c_hits >= 0, "euclid.c.hits is not in ModDest::kTable");
    static_assert(euclid_c_insLen >= 0, "euclid.c.insLen is not in ModDest::kTable");
    static_assert(euclid_c_insSt >= 0, "euclid.c.insSt is not in ModDest::kTable");
    static_assert(euclid_c_postPad >= 0, "euclid.c.postPad is not in ModDest::kTable");
    static_assert(euclid_c_prePad >= 0, "euclid.c.prePad is not in ModDest::kTable");
    static_assert(euclid_c_rotate >= 0, "euclid.c.rotate is not in ModDest::kTable");
    static_assert(fenv_attack >= 0, "fenv.attack is not in ModDest::kTable");
    static_assert(fenv_decay >= 0, "fenv.decay is not in ModDest::kTable");
    static_assert(fenv_depth >= 0, "fenv.depth is not in ModDest::kTable");
    static_assert(filter_cutoff >= 0, "filter.cutoff is not in ModDest::kTable");
    static_assert(filter_lowCut >= 0, "filter.lowCut is not in ModDest::kTable");
    static_assert(filter_resonance >= 0, "filter.resonance is not in ModDest::kTable");
    static_assert(insert_p1 >= 0, "insert.p1 is not in ModDest::kTable");
    static_assert(insert_p2 >= 0, "insert.p2 is not in ModDest::kTable");
    static_assert(insert_p3 >= 0, "insert.p3 is not in ModDest::kTable");
    static_assert(insert_p4 >= 0, "insert.p4 is not in ModDest::kTable");
    static_assert(pitch_envDepth >= 0, "pitch.envDepth is not in ModDest::kTable");
    static_assert(pitch_octave >= 0, "pitch.octave is not in ModDest::kTable");
    static_assert(pitch_semitones >= 0, "pitch.semitones is not in ModDest::kTable");
    static_assert(send_delay >= 0, "send.delay is not in ModDest::kTable");
    static_assert(send_effect >= 0, "send.effect is not in ModDest::kTable");
    static_assert(send_reverb >= 0, "send.reverb is not in ModDest::kTable");
}

// Phase 1 — seed every destination with its knob value as the matrix expects it: a proportion
// of the knob's range for skewed / step-count-dependent knobs, the offset 0 for pitch.
void PluginProcessor::seedModulation(int r, const Rhythm& rhythm, const VoiceParams& modParams)
{
    // PROPORTION-SPACE modulation for skewed-slider destinations:
    // additive-in-display-units modulation on a skewed slider gives
    // variable visual arc length (the same display delta covers a
    // different visual proportion at different knob positions). Seed
    // these destinations as the slider's proportion (0..1), apply
    // modulation additively in proportion-space, then convert back
    // via the slider's skew at write-back. ADSR times use skewFactor
    // 0.3 on a 0..10 range; filter.lowCut uses skewFactor 0.35 on 0..1000.
    // amp.level is dB-linear (-60..+6) so the slider's "proportion" is
    // (dB + 60) / 66 — modulate in dB.
    // Skew conversions (forward + inverse) live in ModulationSkew.h so the
    // seed / snapshot / write-back blocks share one definition (test C5).
    using namespace mu_clid::mod_skew;   // skew conversions + linear knob ranges (kSustain, kPad, ...)

    mv(md::amp_attack)       = propFromAdsr(modParams.ampEnvAtk);
    mv(md::amp_decay)        = propFromAdsr(modParams.ampEnvDec);
    mv(md::amp_sustain)      = kSustain.prop(modParams.ampEnvSus);
    // amp.release is not a modulation target (no note-off on a step
    // trigger, so the release stage is never entered; see Finding 2).
    mv(md::filter_cutoff)    = propFromCutoff(modParams.filterCutoff);  // proportion-space, log-skewed
    mv(md::filter_resonance) = kResonance.prop(modParams.filterRes);
    mv(md::fenv_attack)      = propFromAdsr(modParams.filterEnvAtk);
    mv(md::fenv_decay)       = propFromAdsr(modParams.filterEnvDec);
    mv(md::fenv_depth)       = kFenvDepth.prop(modParams.filterEnvDepth);
    mv(md::filter_lowCut)    = propFromLowCut(modParams.filterLowCutHz);
    // pitch.octave and pitch.semitones are OFFSETS from the knob (seeded 0, in proportion
    // of the knob's range); summed in semitones at write-back → pitchMod.
    mv(md::pitch_semitones)  = 0.0f;
    mv(md::pitch_octave)     = 0.0f;
    // Stage 36: insert mod targets the 4 generic slots directly.
    // Each algorithm's process() converts slot ↔ actual via the
    // per-algo config table; modulation only sees normalised 0..1
    // so the same destination name (`insert.p1`) means "knob 1
    // of the active algorithm" regardless of which algo is loaded.
    mv(md::insert_p1) = modParams.insertParam[0];
    mv(md::insert_p2) = modParams.insertParam[1];
    mv(md::insert_p3) = modParams.insertParam[2];
    mv(md::insert_p4) = modParams.insertParam[3];
    // new destinations
    mv(md::pitch_envDepth)   = kPitchEnvDepth.prop(modParams.pitchEnvDepth);
    mv(md::amp_level)        = kAmpLevel.prop(modParams.ampLevel);
    mv(md::accentDb)         = kAccent.prop(modParams.accentDb);
    // Stage A: seed euclid pattern destinations with base gen values.
    // hits/rotate/insSt use PROPORTION-SPACE modulation because their
    // slider ranges depend on the current step count — proportion-space gives
    // 100%-mod = 100%-knob-turn regardless of step count. prePad/postPad/insLen are
    // likewise proportions of their knobs' current ranges (HitGenerator::padKnobMaxima).
    const int stepsA_seed = juce::jmax(1, rhythm.genA.steps);
    const int stepsB_seed = juce::jmax(1, rhythm.genB.steps);
    const int stepsC_seed = juce::jmax(1, rhythm.genC.steps);
    const auto padMaxA = rhythm.genA.padKnobMaxima();
    const auto padMaxB = rhythm.genB.padKnobMaxima();
    const auto padMaxC = rhythm.genC.padKnobMaxima();
    auto propOf = [](int v, int max) { return max > 0 ? (float) v / (float) max : 0.0f; };
    mv(md::euclid_a_hits)    = (float) rhythm.genA.hits         / (float) stepsA_seed;
    mv(md::euclid_a_rotate)  = (float) rhythm.genA.rotate       / (float) juce::jmax(1, stepsA_seed - 1);
    mv(md::euclid_a_prePad)  = propOf(rhythm.genA.prePad,       padMaxA.prePad);
    mv(md::euclid_a_postPad) = propOf(rhythm.genA.postPad,      padMaxA.postPad);
    mv(md::euclid_a_insSt)   = (float) rhythm.genA.insertStart  / (float) juce::jmax(1, stepsA_seed - 1);
    mv(md::euclid_a_insLen)  = propOf(rhythm.genA.insertLength, padMaxA.insertLength);
    mv(md::euclid_b_hits)    = (float) rhythm.genB.hits         / (float) stepsB_seed;
    mv(md::euclid_b_rotate)  = (float) rhythm.genB.rotate       / (float) juce::jmax(1, stepsB_seed - 1);
    mv(md::euclid_b_prePad)  = propOf(rhythm.genB.prePad,       padMaxB.prePad);
    mv(md::euclid_b_postPad) = propOf(rhythm.genB.postPad,      padMaxB.postPad);
    mv(md::euclid_b_insSt)   = (float) rhythm.genB.insertStart  / (float) juce::jmax(1, stepsB_seed - 1);
    mv(md::euclid_b_insLen)  = propOf(rhythm.genB.insertLength, padMaxB.insertLength);
    mv(md::euclid_c_hits)    = (float) rhythm.genC.hits         / (float) stepsC_seed;
    mv(md::euclid_c_rotate)  = (float) rhythm.genC.rotate       / (float) juce::jmax(1, stepsC_seed - 1);
    mv(md::euclid_c_prePad)  = propOf(rhythm.genC.prePad,       padMaxC.prePad);
    mv(md::euclid_c_postPad) = propOf(rhythm.genC.postPad,      padMaxC.postPad);
    mv(md::euclid_c_insSt)   = (float) rhythm.genC.insertStart  / (float) juce::jmax(1, stepsC_seed - 1);
    mv(md::euclid_c_insLen)  = propOf(rhythm.genC.insertLength, padMaxC.insertLength);

    // Mixer strip: pan + FX sends, seeded from the mixer channel as proportions of
    // their knobs (pan -1..+1 → 0..1; sends are 0..1 already).
    auto& strip = mixerEngine.channels[(size_t) r];
    mv(md::amp_pan)     = (strip.pan.load(std::memory_order_relaxed) + 1.0f) * 0.5f;
    mv(md::send_effect) = strip.sendEffect.load(std::memory_order_relaxed);
    mv(md::send_delay)  = strip.sendDelay.load(std::memory_order_relaxed);
    mv(md::send_reverb) = strip.sendReverb.load(std::memory_order_relaxed);
}

// Phase 3 — hand the modulated pan / sends to the mixer channel for this block.
PluginProcessor::StripMod PluginProcessor::applyStripModulation(int r)
{
    auto& strip = mixerEngine.channels[(size_t) r];
    // Hand the modulated strip values to the mixer for this block.
    const float modPan = juce::jlimit(-1.0f, 1.0f, mv(md::amp_pan) * 2.0f - 1.0f);
    const float modEff = juce::jlimit(0.0f, 1.0f, mv(md::send_effect));
    const float modDly = juce::jlimit(0.0f, 1.0f, mv(md::send_delay));
    const float modRev = juce::jlimit(0.0f, 1.0f, mv(md::send_reverb));
    strip.panMod       .store(modPan, std::memory_order_relaxed);
    strip.sendEffectMod.store(modEff, std::memory_order_relaxed);
    strip.sendDelayMod .store(modDly, std::memory_order_relaxed);
    strip.sendReverbMod.store(modRev, std::memory_order_relaxed);
    return { modPan, modEff, modDly, modRev };
}

// Phase 4 — publish each destination's modulated ACTUAL value for the knobs' live arcs.
void PluginProcessor::publishModSnapshot(int r, const Rhythm& rhythm, const VoiceParams& modParams,
                                         const StripMod& stripMod)
{
    using namespace mu_clid::mod_skew;
    const auto padMaxA = rhythm.genA.padKnobMaxima();
    const auto padMaxB = rhythm.genB.padKnobMaxima();
    const auto padMaxC = rhythm.genC.padKnobMaxima();
    const float modPan = stripMod.pan, modEff = stripMod.effect, modDly = stripMod.delay, modRev = stripMod.reverb;
    // Snapshot pre-normalised values for the UI live-arc indicator.
    {
        auto& snap = modSnapshot[r];
        // Proportion-space destinations — modParamValues holds slider proportion 0..1.
        // Snap stores the ACTUAL value (seconds / Hz / dB) so the UI's setModulatedActual
        // routes via valueToProportionOfLength and matches the needle's visual position
        // by construction. Same pattern as filter.cutoff and insert.pN (Stage 36).
        // adsrFromProp / lowCutFromProp / cutoffFromProp come from ModulationSkew.h
        // (brought in via the using-declarations above).
        snap[kSnapAmpAtk]      .store(adsrFromProp(mv(md::amp_attack)));
        snap[kSnapAmpDec]      .store(adsrFromProp(mv(md::amp_decay)));
        snap[kSnapAmpSus]      .store(juce::jlimit(0.0f, 1.0f, mv(md::amp_sustain)));
        // Filter Cutoff: proportion-space modulation — snap stores ACTUAL Hz
        // converted from the proportion, so the UI's setModulatedActual goes
        // through the slider's setSkewFactorFromMidPoint(640) via valueToProportionOfLength
        // and the arc matches the visual knob by construction.
        snap[kSnapFilterCutoff].store(cutoffFromProp(mv(md::filter_cutoff)));
        snap[kSnapFilterRes]   .store(juce::jlimit(0.0f, 1.0f, mv(md::filter_resonance)));
        // Filter ADSR times: proportion-space modulation — convert back to actual seconds.
        snap[kSnapFenvAtk]     .store(adsrFromProp(mv(md::fenv_attack)));
        snap[kSnapFenvDec]     .store(adsrFromProp(mv(md::fenv_decay)));
        // fenv.depth, pitch.envDepth, accentDb: voiceParams units (semis or dB) differ from the
        // slider's 0..100 display. Store the DISPLAY value (slider units) so setModulatedActual
        // routes through the slider's valueToProportionOfLength correctly.
        snap[kSnapFenvDepth]   .store(kFenvDepth.value(mv(md::fenv_depth)));     // semitones 0..48
        // pitch.semitones: snap stores BASE + OFFSET (in semitones) so the arc tracks the modulated
        // knob position regardless of where the base sits. Pre-fix stored only the offset, so a
        // negative mod read as ABOVE the needle when base was negative (proportion-space follow-up).
        snap[kSnapPitchSemi]   .store(modParams.pitchSemitones + mv(md::pitch_semitones) * kPitchSemi.width());
        // Insert mod snapshots store ACTUAL slider values (per
        // the active algo's slot range / skew) so the UI can run
        // them through `slider.valueToProportionOfLength` via
        // setModulatedActual. Same reasoning as filter cutoff:
        // the slider's log-skew (setSkewFactorFromMidPoint(sqrt(min·max)))
        // is NOT the same curve as the storage-space norm-to-actual
        // (lo · (max/lo)^norm), so a raw normalised snapshot would
        // disagree with the visual needle position. Converting to
        // actual + delegating proportion lookup to the slider
        // guarantees agreement regardless of slot skew (Linear,
        // Log, or IntStep) and regardless of which algorithm is
        // active.
        const int algForSnap = (int) modParams.insertAlgo;
        snap[kSnapInsP1].store(mu_ui::normToActual(mv(md::insert_p1), algForSnap, 0));
        snap[kSnapInsP2].store(mu_ui::normToActual(mv(md::insert_p2), algForSnap, 1));
        snap[kSnapInsP3].store(mu_ui::normToActual(mv(md::insert_p3), algForSnap, 2));
        snap[kSnapInsP4].store(mu_ui::normToActual(mv(md::insert_p4), algForSnap, 3));
        // new destinations — sliders now match voiceParams units (Step 0),
        // so snapshots store the raw value and setModulatedActual routes through the
        // slider's valueToProportionOfLength directly.
        snap[kSnapPitchEnvDep] .store(kPitchEnvDepth.value(mv(md::pitch_envDepth)));  // semitones 0..24
        snap[kSnapAmpLvl]      .store(kAmpLevel.value(mv(md::amp_level)));            // dB -60..+6
        snap[kSnapAccent]      .store(kAccent.value(mv(md::accentDb)));               // dB 0..12
        // filter.lowCut: proportion-space modulation → actual Hz for setModulatedActual.
        snap[kSnapFilterLowCut].store(lowCutFromProp(mv(md::filter_lowCut)));
        // T5 follow-up — pitch.octave: modParamValues holds the modulation offset in SEMITONES (write-back
        // sums it with pitch.semitones into pitchMod). To show the arc on the pitchOctave knob (range -4..+4
        // octaves, linear), store base octave value + offset/12. UI uses setModulatedActual.
        snap[kSnapPitchOctave] .store(modParams.pitchOctave + mv(md::pitch_octave) * kPitchOctave.width());
        // Mixer strip: the actual pan (-1..+1) and send (0..1) values.
        snap[kSnapPan]         .store(modPan);
        snap[kSnapSendEff]     .store(modEff);
        snap[kSnapSendDly]     .store(modDly);
        snap[kSnapSendRev]     .store(modRev);
        // Euclid pattern destinations:
        //   hits/rotate/insSt: proportion-space mod (modParamValues already holds 0..1
        //     slider proportion). snap stores the proportion directly — UI uses
        //     setModulatedNorm.
        //   prePad/postPad/insLen: proportions of the knob's current range; snap
        //     stores the actual step value — UI uses setModulatedActual and maps it
        //     onto that range.
        auto prop = [](float v) { return juce::jlimit(0.0f, 1.0f, v); };
        auto act  = [](float v, int max) { return juce::jlimit(0.0f, 1.0f, v) * (float) max; };
        snap[kSnapEucAHits]    .store(prop(mv(md::euclid_a_hits)));
        snap[kSnapEucARotate]  .store(prop(mv(md::euclid_a_rotate)));
        snap[kSnapEucAPrePad]  .store(act(mv(md::euclid_a_prePad),  padMaxA.prePad));
        snap[kSnapEucAPostPad] .store(act(mv(md::euclid_a_postPad), padMaxA.postPad));
        snap[kSnapEucAInsSt]   .store(prop(mv(md::euclid_a_insSt)));
        snap[kSnapEucAInsLen]  .store(act(mv(md::euclid_a_insLen),  padMaxA.insertLength));
        snap[kSnapEucBHits]    .store(prop(mv(md::euclid_b_hits)));
        snap[kSnapEucBRotate]  .store(prop(mv(md::euclid_b_rotate)));
        snap[kSnapEucBPrePad]  .store(act(mv(md::euclid_b_prePad),  padMaxB.prePad));
        snap[kSnapEucBPostPad] .store(act(mv(md::euclid_b_postPad), padMaxB.postPad));
        snap[kSnapEucBInsSt]   .store(prop(mv(md::euclid_b_insSt)));
        snap[kSnapEucBInsLen]  .store(act(mv(md::euclid_b_insLen),  padMaxB.insertLength));
        snap[kSnapEucCHits]    .store(prop(mv(md::euclid_c_hits)));
        snap[kSnapEucCRotate]  .store(prop(mv(md::euclid_c_rotate)));
        snap[kSnapEucCPrePad]  .store(act(mv(md::euclid_c_prePad),  padMaxC.prePad));
        snap[kSnapEucCPostPad] .store(act(mv(md::euclid_c_postPad), padMaxC.postPad));
        snap[kSnapEucCInsSt]   .store(prop(mv(md::euclid_c_insSt)));
        snap[kSnapEucCInsLen]  .store(act(mv(md::euclid_c_insLen),  padMaxC.insertLength));
    }
}

// Phase 5 — write the modulated values back: the voice params for this block, and the euclid
// overrides (whole steps) that drive the pattern recompute.
void PluginProcessor::writeBackModulation(int r, const Rhythm& rhythm, VoiceParams& modParams)
{
    using namespace mu_clid::mod_skew;
    const auto padMaxA = rhythm.genA.padKnobMaxima();
    const auto padMaxB = rhythm.genB.padKnobMaxima();
    const auto padMaxC = rhythm.genC.padKnobMaxima();
    // Write modulated values back, clamping to safe ranges. Proportion-space
    // destinations convert prop → actual via the shared inverse-skew
    // helpers in ModulationSkew.h (adsrFromProp / lowCutFromProp / cutoffFromProp).
    modParams.ampEnvAtk      = juce::jmax(0.001f, adsrFromProp(mv(md::amp_attack)));
    modParams.ampEnvDec      = juce::jmax(0.001f, adsrFromProp(mv(md::amp_decay)));
    modParams.ampEnvSus      = kSustain.value(mv(md::amp_sustain));
    modParams.filterCutoff   = juce::jlimit(20.0f, 20000.0f, cutoffFromProp(mv(md::filter_cutoff)));
    modParams.filterRes      = kResonance.value(mv(md::filter_resonance));
    modParams.filterEnvAtk   = juce::jmax(0.001f, adsrFromProp(mv(md::fenv_attack)));
    modParams.filterEnvDec   = juce::jmax(0.001f, adsrFromProp(mv(md::fenv_decay)));
    modParams.filterEnvDepth = kFenvDepth.value(mv(md::fenv_depth));
    modParams.filterLowCutHz = lowCutFromProp(mv(md::filter_lowCut));
    // single pitch destination, no more octave×12 + fine/100 stacking.
    // Offsets in proportion of each knob's range → semitones (octave knob ±3 oct = 72 st).
    modParams.pitchMod       = juce::jlimit(-48.0f, 48.0f,
                                             mv(md::pitch_octave) * kPitchOctave.width() * 12.0f
                                           + mv(md::pitch_semitones) * kPitchSemi.width());
    // Stage 36: insert mod write-back to the 4 generic slots.
    // Values stay normalised 0..1; per-algo de-normalisation
    // happens inside each InsertAlgorithm::process via the config
    // table. No algorithm-specific branching needed.
    modParams.insertParam[0] = juce::jlimit(0.0f, 1.0f, mv(md::insert_p1));
    modParams.insertParam[1] = juce::jlimit(0.0f, 1.0f, mv(md::insert_p2));
    modParams.insertParam[2] = juce::jlimit(0.0f, 1.0f, mv(md::insert_p3));
    modParams.insertParam[3] = juce::jlimit(0.0f, 1.0f, mv(md::insert_p4));
    // new destinations write-back
    modParams.pitchEnvDepth  = kPitchEnvDepth.value(mv(md::pitch_envDepth));
    modParams.ampLevel       = kAmpLevel.value(mv(md::amp_level));
    modParams.accentDb       = kAccent.value(mv(md::accentDb));

    // Stage A: write modulated euclid values back to the per-rhythm
    // overrides snapshot. hits/rotate/insSt are proportions of the current step
    // count; prePad/postPad/insLen are proportions of their knobs' current ranges.
    // Both convert back to whole steps.
    auto modPropToSteps = [&](int slot, int steps) {
        return juce::roundToInt(juce::jlimit(0.0f, 1.0f, mv(slot)) * (float) steps);
    };
    const int stepsA_wb = juce::jmax(1, rhythm.genA.steps);
    const int stepsB_wb = juce::jmax(1, rhythm.genB.steps);
    const int stepsC_wb = juce::jmax(1, rhythm.genC.steps);
    lastEuclidOverrides[r].a.hits         = juce::jlimit(0, stepsA_wb,        modPropToSteps(md::euclid_a_hits,  stepsA_wb));
    lastEuclidOverrides[r].a.rotate       = juce::jlimit(0, stepsA_wb - 1,    modPropToSteps(md::euclid_a_rotate, stepsA_wb - 1));
    lastEuclidOverrides[r].a.prePad       = modPropToSteps(md::euclid_a_prePad, padMaxA.prePad);
    lastEuclidOverrides[r].a.postPad      = modPropToSteps(md::euclid_a_postPad, padMaxA.postPad);
    lastEuclidOverrides[r].a.insertStart  = juce::jlimit(0, stepsA_wb - 1,    modPropToSteps(md::euclid_a_insSt, stepsA_wb - 1));
    lastEuclidOverrides[r].a.insertLength = modPropToSteps(md::euclid_a_insLen, padMaxA.insertLength);
    lastEuclidOverrides[r].b.hits         = juce::jlimit(0, stepsB_wb,        modPropToSteps(md::euclid_b_hits,  stepsB_wb));
    lastEuclidOverrides[r].b.rotate       = juce::jlimit(0, stepsB_wb - 1,    modPropToSteps(md::euclid_b_rotate, stepsB_wb - 1));
    lastEuclidOverrides[r].b.prePad       = modPropToSteps(md::euclid_b_prePad, padMaxB.prePad);
    lastEuclidOverrides[r].b.postPad      = modPropToSteps(md::euclid_b_postPad, padMaxB.postPad);
    lastEuclidOverrides[r].b.insertStart  = juce::jlimit(0, stepsB_wb - 1,    modPropToSteps(md::euclid_b_insSt, stepsB_wb - 1));
    lastEuclidOverrides[r].b.insertLength = modPropToSteps(md::euclid_b_insLen, padMaxB.insertLength);
    lastEuclidOverrides[r].c.hits         = juce::jlimit(0, stepsC_wb,        modPropToSteps(md::euclid_c_hits,  stepsC_wb));
    lastEuclidOverrides[r].c.rotate       = juce::jlimit(0, stepsC_wb - 1,    modPropToSteps(md::euclid_c_rotate, stepsC_wb - 1));
    lastEuclidOverrides[r].c.prePad       = modPropToSteps(md::euclid_c_prePad, padMaxC.prePad);
    lastEuclidOverrides[r].c.postPad      = modPropToSteps(md::euclid_c_postPad, padMaxC.postPad);
    lastEuclidOverrides[r].c.insertStart  = juce::jlimit(0, stepsC_wb - 1,    modPropToSteps(md::euclid_c_insSt, stepsC_wb - 1));
    lastEuclidOverrides[r].c.insertLength = modPropToSteps(md::euclid_c_insLen, padMaxC.insertLength);
}

void PluginProcessor::applyRhythmModulation(int r, double beatPos)
{
    Rhythm& rhythm = sequencer.getRhythm(r);
    // Snapshot voiceParams under voiceParamsLock so a concurrent
    // message-thread apply (syncRhythmParam / forceSyncRhythmFromAPVTS)
    // can't interleave a torn write. Held for ~struct-copy time only.
    VoiceParams modParams;
    {
        mu_core::spinLock(rhythm.voiceParamsLock);
        modParams = rhythm.voiceParams;
        mu_core::spinUnlock(rhythm.voiceParamsLock);
    }

    // gate the modulation pass on "matrix has assignments now, OR had
    // assignments last block". The first half is the normal case. The second half
    // runs one final pass on the block AFTER assignment removal so the write-back
    // re-seeds lastEuclidOverrides[r] to base values; Stage B's change-detection
    // then recomputes the safe pattern back to base. Without that transition pass,
    // a never-modulated rhythm pays no per-block cost, but a rhythm whose last
    // assignment was just removed would leave lastEuclidOverrides stuck on the old
    // modulated values, freezing the pattern.
    const bool matrixHasAssignments = !rhythm.modulationMatrix.getAssignments().empty();
    const bool runModulationPass    = matrixHasAssignments || prevMatrixHadAssignments[r];
    prevMatrixHadAssignments[r]     = matrixHasAssignments;

    if (runModulationPass)
    {
        if (mu_core::trySpinLock(rhythm.modLock))
        {
            seedModulation(r, rhythm, modParams);                                      // phase 1
            rhythm.modulationMatrix.process(rhythm.controlSequences, beatPos, modParamValues);   // phase 2
            const StripMod stripMod = applyStripModulation(r);                         // phase 3
            mu_core::spinUnlock(rhythm.modLock);
            publishModSnapshot(r, rhythm, modParams, stripMod);                        // phase 4
            writeBackModulation(r, rhythm, modParams);                                 // phase 5
        }
    }

    // No modulation this block: the mixer uses the strip's own pan / sends.
    if (! runModulationPass)
    {
        auto& strip = mixerEngine.channels[(size_t) r];
        for (auto* m : { &strip.panMod, &strip.sendEffectMod, &strip.sendDelayMod, &strip.sendReverbMod })
            m->store(MixerEngine::ChannelState::kNoMod, std::memory_order_relaxed);
    }

    // Stage B: trigger pattern recompute when integer-rounded overrides changed
    // since last block. Skips recompute on every block where modulation is sub-step
    // (typical case for a slow LFO). tryUpdatePatternFromModulation is non-blocking —
    // a missed try-lock just defers to the next block; prevEuclidOverrides only
    // advances when the recompute actually applied so the change-detection retries.
    if (lastEuclidOverrides[r] != prevEuclidOverrides[r])
    {
        if (sequencer.tryUpdatePatternFromModulation(r, lastEuclidOverrides[r]))
            prevEuclidOverrides[r] = lastEuclidOverrides[r];
    }

    // only override activeParams when the modulation pass actually ran. For an
    // unmodulated rhythm modParams ≡ rhythm.voiceParams ≡ activeParams (already kept in
    // sync by VoiceEngine::applyPendingParams' dirty-flag path), so the call would be
    // pure waste — re-syncing ADSR + filter every block for no change.
    if (runModulationPass && voiceEngines[r])
        voiceEngines[r]->setActiveParams(modParams);
}

double PluginProcessor::deriveEffectiveBpm()
{
    // Effective BPM for tempo-synced FX (Delay, Echo): host playhead takes priority
    // in DAW mode, MIDI clock estimate when locked in standalone, otherwise the
    // internal transport. Previously this always used internalBpm, so DAW-hosted
    // sessions saw the delay always tempo-synced to 120 regardless of host tempo.
    double effectiveBpm = internalBpm.load(std::memory_order_relaxed);
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
            if (auto hostBpm = pos->getBpm())
                effectiveBpm = *hostBpm;
    if (midiClockSync.isEnabled()
        && wrapperType == wrapperType_Standalone
        && midiClockSync.isPlaying())
        effectiveBpm = midiClockSync.getBpm();
    return effectiveBpm;
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

    samplePreview.mixInto(masterBus, buffer.getNumSamples());

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
void PluginProcessor::addRhythm(const Rhythm& r)
{
    int ri = sequencer.getNumRhythms();
    if (ri >= SequencerEngine::MaxRhythms) return;
    hotSwapStager.cancelPendingIfAny(ri);          // the new slot must carry no stale staged swap
    voiceEngines[ri] = std::make_unique<VoiceEngine>();
    voiceEngines[ri]->prepareToPlay(currentSampleRate, currentBlockSize);
    midiEngines[ri].prepare(currentSampleRate, currentBlockSize);
    {
        const juce::ScopedLock sl(rhythmsLock);
        sequencer.addRhythm(r);
        numActiveRhythms.store(sequencer.getNumRhythms(), std::memory_order_release);
    }
    if (ri < loadedSamplePaths.size())
        loadedSamplePaths.set(ri, juce::String());
    {
        mu_core::ScopedApvtsLoading guard(apvtsLoading);
        pushRhythmToAPVTS(ri);
    }
}

void PluginProcessor::resetPlayState(int idx)
{
    if (idx < 0 || idx >= SequencerEngine::MaxRhythms) return;
    auto& s = rhythmPlayState[idx];
    s.currentStep  .store(0);
    s.patternLength.store(1);
    s.stepsA       .store(1);
    s.stepsB       .store(1);
    s.stepsC       .store(1);
    s.hitCount     .store(0);
}
bool PluginProcessor::swapRhythms(int i, int j)
{
    const int n = numActiveRhythms.load(std::memory_order_acquire);
    if (i < 0 || j < 0 || i >= n || j >= n || i == j) return false;

    // Pending per-rhythm staged swaps are keyed by index; swapping the slots would
    // misdirect them, so drop both. (A staged full preset is index-independent — it
    // replaces every slot at commit — so it is left to win.)
    hotSwapStager.cancelPendingIfAny(i);
    hotSwapStager.cancelPendingIfAny(j);

    // suspendProcessing only sets a flag — it does NOT block the in-flight
    // processBlock callback (see PluginProcessor.h:266). Take rhythmsLock to
    // serialise with the audio thread, the same way removeRhythm and
    // handleAsyncUpdate do. The audio thread's ScopedTryLock in processBlock will
    // bail (silent block) while we hold the lock — clean atomic swap.
    suspendProcessing(true);
    {
        const juce::ScopedLock sl(rhythmsLock);

        sequencer.swapRhythmSlots(i, j);
        std::swap(voiceEngines[i], voiceEngines[j]);
        std::swap(midiEngines[i],  midiEngines[j]);
        resetPlayState(i);
        resetPlayState(j);

        juce::String tmp = loadedSamplePaths[i];
        loadedSamplePaths.set(i, loadedSamplePaths[j]);
        loadedSamplePaths.set(j, tmp);

        // Re-translate sidechain source indices BEFORE the channel swap, so any
        // channel referring to the swapped slots keeps pointing at the same logical
        // rhythm after the swap.
        for (int c = 0; c < n; ++c)
        {
            auto& src = mixerEngine.channels[c].sidechainSource;
            const int s = src.load(std::memory_order_relaxed);
            if      (s == i) src.store(j, std::memory_order_relaxed);
            else if (s == j) src.store(i, std::memory_order_relaxed);
        }

        mixerEngine.swapChannelState(i, j);

        // Reset envelope follower state on both moved slots so the previous tenant's
        // ducking envelope doesn't bleed into the freshly arrived rhythm.
        mixerEngine.resetSidechainEnv(i);
        mixerEngine.resetSidechainEnv(j);
    }
    suspendProcessing(false);

    swapAPVTSForRhythms(i, j);
    return true;
}

void PluginProcessor::removeRhythm(int index)
{
    if (index < 0 || index >= sequencer.getNumRhythms()) return;
    const int newN = sequencer.getNumRhythms() - 1;

    // The down-shift renumbers every rhythm from `index` upward, so any pending
    // per-rhythm staged swap (keyed by index) would land on the wrong slot — drop
    // them all. A staged full preset is index-independent, so it is left to win.
    for (int r = 0; r < SequencerEngine::MaxRhythms; ++r)
        hotSwapStager.cancelPendingIfAny(r);

    // suspendProcessing ensures no in-progress processBlock holds a stale
    // numActiveRhythms snapshot and reads rhythms[r] while we erase and shift.
    suspendProcessing(true);
    {
        const juce::ScopedLock sl(rhythmsLock);
        numActiveRhythms.store(newN, std::memory_order_release);
        sequencer.removeRhythm(index);
        for (int i = index; i < newN; ++i)
        {
            voiceEngines[i] = std::move(voiceEngines[i + 1]);
            midiEngines[i]  = std::move(midiEngines[i + 1]);
            mixerEngine.channels[i].copyFrom(mixerEngine.channels[i + 1]);
        }
        voiceEngines[newN].reset();
        midiEngines[newN] = MidiOutputEngine{};
        mixerEngine.channels[newN].reset();
    }
    suspendProcessing(false);
}

void PluginProcessor::resetRhythm(int index)
{
    if (index < 0 || index >= sequencer.getNumRhythms()) return;
    hotSwapStager.cancelPendingIfAny(index);   // a staged swap would re-fill what we're clearing

    // was a UI-thread spin on rhythm.modLock + concurrent vector destruction
    // risk in ModulationMatrix::process. Now uses the same suspendProcessing +
    // rhythmsLock pattern as removeRhythm — clean atomic swap, no UI freeze even
    // if the audio thread is preempted while holding modLock.
    suspendProcessing(true);
    {
        const juce::ScopedLock sl(rhythmsLock);
        auto& r = sequencer.getRhythm(index);
        auto savedName   = r.name;
        auto savedColour = r.colourIndex;
        r = Rhythm{};
        r.name        = savedName;
        r.colourIndex = savedColour;
    }
    suspendProcessing(false);

    sequencer.updatePattern(index);
}

void PluginProcessor::renameRhythm(int index, const juce::String& newName)
{
    if (index < 0 || index >= sequencer.getNumRhythms()) return;

    // rhythmsLock serialises with the audio thread's ScopedTryLock in
    // processBlock. No audio-thread reader of name today, but the lock matches the
    // project's "message thread mutates Rhythm" convention and stays correct if a
    // future audio-path consumer (e.g. MIDI program-change preset matcher) reads it.
    const juce::ScopedLock sl(rhythmsLock);
    sequencer.getRhythm(index).name = newName.toStdString();
}

//==============================================================================
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

void PluginProcessor::setMultiBusEnabled(bool on)
{
    multiBusEnabled.store(on, std::memory_order_relaxed);
    appSettings->setValue("multiBusEnabled", on);
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
void PluginProcessor::loadSampleForRhythm(int rhythmIndex, const juce::File& file)
{
    if (rhythmIndex < 0 || rhythmIndex >= numActiveRhythms.load(std::memory_order_acquire)) return;
    voiceEngines[rhythmIndex]->loadFile(file);
    loadedSamplePaths.set(rhythmIndex, file.getFullPathName());
}

void PluginProcessor::startSamplePreview(const juce::File& file) { samplePreview.start(file); }
void PluginProcessor::stopSamplePreview()                        { samplePreview.stop(); }

//==============================================================================
// Hot-swap: stage a rhythm preset for atomic commit at the next loop boundary.
// or a MIDI program change was queued.
void PluginProcessor::commitDeferredWork()
{
    // Drain retired engines + commit pending swaps (both sides of the hot-swap lifecycle).
    // ProcessorBase then drains the MIDI program-change queue.
    hotSwapStager.processSwaps();
}

//==============================================================================
juce::File PluginProcessor::getPresetsDir() const { return getContentDir().getChildFile("Presets"); }
juce::File PluginProcessor::getRhythmsDir() const { return getContentDir().getChildFile("Rhythms"); }
juce::File PluginProcessor::getSamplesDir() const { return getContentDir().getChildFile("Samples"); }

void PluginProcessor::setContentDir(const juce::File& dir)
{
    if (appSettings != nullptr)
    {
        appSettings->setValue("contentDir", dir.getFullPathName());
        appSettings->saveIfNeeded();
    }
    ensureContentFoldersExist();
}

// primary sample library — user's personal sample folder, distinct
// from the My Documents content dir (which hosts factory / preset-linked
// material). Default-unset returns the OS Music directory so the sample-load
// dialog opens somewhere sensible even on first launch.
juce::File PluginProcessor::getPrimarySampleDir() const
{
    if (appSettings != nullptr)
    {
        const juce::String stored = appSettings->getValue("primarySampleDir");
        if (stored.isNotEmpty())
            return juce::File(stored);
    }
    return juce::File::getSpecialLocation(juce::File::userMusicDirectory);
}

void PluginProcessor::setPrimarySampleDir(const juce::File& dir)
{
    if (appSettings != nullptr)
    {
        // Empty string clears the override → next getPrimarySampleDir() returns
        // the default (user Music). Lets the SettingsOverlay "Default" button
        // reuse the same setter.
        appSettings->setValue("primarySampleDir",
                              dir == juce::File{} ? juce::String{} : dir.getFullPathName());
        appSettings->saveIfNeeded();
    }
}

void PluginProcessor::ensureContentFoldersExist()
{
    getPresetsDir().createDirectory();
    getRhythmsDir().createDirectory();
    getSamplesDir().createDirectory();
}
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new PluginProcessor();
}
