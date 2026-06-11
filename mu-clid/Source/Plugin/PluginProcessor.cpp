#include "PluginProcessor.h"
#include "PluginProcessor_Internal.h"
#include "Audio/InsertSlotConfig.h"
#include "Plugin/ModulationSkew.h"  // proportion-space skew helpers (shared with test C5)
#include "Modulation/MuClidModDest.h"  // mu_clid::registerDepthScales (mu-core depth-scale reg)
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
    mu_clid::registerDepthScales();

    // Initialise ApplicationProperties (needed by getContentDir/getPresetsDir).
    {
        juce::PropertiesFile::Options opts;
        opts.applicationName     = "muClid";
        opts.filenameSuffix      = "xml";
        opts.folderName          = "TDP";
        opts.osxLibrarySubFolder = "Application Support";
        auto settingsFile = opts.getDefaultFile();
        settingsFile.getParentDirectory().createDirectory();
        appSettings = std::make_unique<juce::PropertiesFile>(settingsFile, opts);
    }

    // Load MIDI sync settings.
    midiClockSync.setEnabled (appSettings->getBoolValue("midiSyncEnabled",  false));
    midiClockSync.setMessages(appSettings->getIntValue ("midiSyncMessages", 2));
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

    // UI scale (Medium=1.0, Large=1.25). Persisted across plugin instances;
    // the editor consults `getUiScale()` at ctor time so a fresh-open picks up
    // the right scale BEFORE constructing children (fixes #574 for cold opens).
    {
        const double stored = appSettings->getDoubleValue("uiScale", (double) kUiScaleMedium);
        uiScale = juce::jlimit(kUiScaleMedium, kUiScaleLarge, (float) stored);
    }

    // Check license file — must run after appSettings so getContentDir() works.
    licenseInfo = mu_core::LicenseManager::check(getContentDir(),
                                                 mu_clid::kLicenseProductId,
                                                 mu_clid::kLicenseFilename,
                                                 mu_clid::kLicensePublicKey);

    // Online activation (Lemon Squeezy). Startup uses a LOCAL-only check so plugin load never
    // blocks on the network; the overlay's activateOnlineFn does the real network activate.
    if (mu_core::OnlineActivation::hasLocalActivation(getContentDir(), kActivationFilename))
        onlineActivated.store(true, std::memory_order_relaxed);
    activateOnlineFn = [this](const juce::String& key) {
        auto o = mu_core::OnlineActivation::activate(getContentDir(), kActivationFilename, key);
        if (o.ok) onlineActivated.store(true, std::memory_order_relaxed);
        return o;
    };

    // Register listener for every parameter.
    for (auto* param : getParameters())
        if (auto* p = dynamic_cast<juce::AudioProcessorParameterWithID*>(param))
            apvts.addParameterListener(p->getParameterID(), this);

    // Initialise sample-path slots.
    for (int i = 0; i < SequencerEngine::MaxRhythms; ++i)
        loadedSamplePaths.add(juce::String());

    // Pre-populate modulation param map so lookups never allocate on the audio thread.
    modParamValues.reserve(50);
    for (const char* key : { "amp.attack", "amp.decay", "amp.sustain",  // amp.release retired (#668)
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
                              "euclid.c.insSt", "euclid.c.insLen" })
        modParamValues[key] = 0.0f;

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
    cancelPendingUpdate();
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
// Lite processBlock helpers (#828).
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
// processBlock phases (#665). Behaviour-preserving extraction of the full-build
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
    if (scanMidiProgramChanges(midiMessages))
        triggerAsyncUpdate();

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
            rhythmPlayState[r].hitCount.store(rhythmPlayState[r].hitCount.load() + 1); // Issue #43: monotonic counter for race-free UI hit detection (#412: hitFired legacy field removed)
        }
    }
}

void PluginProcessor::applyRhythmModulation(int r, double beatPos)
{
    Rhythm& rhythm = sequencer.getRhythm(r);
    // Snapshot voiceParams under voiceParamsLock so a concurrent
    // message-thread apply (syncRhythmParam / forceSyncRhythmFromAPVTS)
    // can't interleave a torn write. Held for ~struct-copy time only.
    VoiceParams modParams;
    {
        bool expected = false;
        while (! rhythm.voiceParamsLock.compare_exchange_strong(expected, true, std::memory_order_acquire))
            expected = false;
        modParams = rhythm.voiceParams;
        rhythm.voiceParamsLock.store(false, std::memory_order_release);
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
        bool expected = false;
        if (rhythm.modLock.compare_exchange_strong(expected, true, std::memory_order_acquire))
        {
            // PROPORTION-SPACE modulation for skewed-slider destinations (#638):
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
            using mu_clid::mod_skew::propFromAdsr;
            using mu_clid::mod_skew::propFromLowCut;
            using mu_clid::mod_skew::propFromCutoff;
            using mu_clid::mod_skew::adsrFromProp;
            using mu_clid::mod_skew::lowCutFromProp;
            using mu_clid::mod_skew::cutoffFromProp;

            modParamValues["amp.attack"]       = propFromAdsr(modParams.ampEnvAtk);
            modParamValues["amp.decay"]        = propFromAdsr(modParams.ampEnvDec);
            modParamValues["amp.sustain"]      = modParams.ampEnvSus   * 100.0f;  // linear, unchanged
            // amp.release is not a modulation target (#668 — no note-off on a step
            // trigger, so the release stage is never entered; see Finding 2).
            modParamValues["filter.cutoff"]    = propFromCutoff(modParams.filterCutoff);  // proportion-space, log-skewed
            modParamValues["filter.resonance"] = modParams.filterRes;             // linear, slider 0..0.99
            modParamValues["fenv.attack"]      = propFromAdsr(modParams.filterEnvAtk);
            modParamValues["fenv.decay"]       = propFromAdsr(modParams.filterEnvDec);
            modParamValues["fenv.depth"]       = modParams.filterEnvDepth;
            modParamValues["filter.lowCut"]    = propFromLowCut(modParams.filterLowCutHz);
            // pitch.octave and pitch.semitones both start at 0; summed at write-back → pitchMod.
            modParamValues["pitch.semitones"]  = 0.0f;
            modParamValues["pitch.octave"]     = 0.0f;
            // Stage 36: insert mod targets the 4 generic slots directly.
            // Each algorithm's process() converts slot ↔ actual via the
            // per-algo config table; modulation only sees normalised 0..1
            // so the same destination name (`insert.p1`) means "knob 1
            // of the active algorithm" regardless of which algo is loaded.
            modParamValues["insert.p1"] = modParams.insertParam[0];
            modParamValues["insert.p2"] = modParams.insertParam[1];
            modParamValues["insert.p3"] = modParams.insertParam[2];
            modParamValues["insert.p4"] = modParams.insertParam[3];
            // new destinations
            modParamValues["pitch.envDepth"]   = modParams.pitchEnvDepth;
            modParamValues["amp.level"]        = modParams.ampLevel;               // additive in dB; slider -60..+6 is linear
            modParamValues["accentDb"]         = modParams.accentDb;
            // Stage A: seed euclid pattern destinations with base gen values.
            // hits/rotate/insSt use PROPORTION-SPACE modulation (#641) because their
            // slider ranges depend on the current step count — proportion-space gives
            // 100%-mod = 100%-knob-turn regardless of step count. prePad/postPad/insLen
            // have FIXED slider ranges so additive-in-step-units works (scale = range).
            const int stepsA_seed = juce::jmax(1, rhythm.genA.steps);
            const int stepsB_seed = juce::jmax(1, rhythm.genB.steps);
            const int stepsC_seed = juce::jmax(1, rhythm.genC.steps);
            modParamValues["euclid.a.hits"]    = (float) rhythm.genA.hits         / (float) stepsA_seed;
            modParamValues["euclid.a.rotate"]  = (float) rhythm.genA.rotate       / (float) juce::jmax(1, stepsA_seed - 1);
            modParamValues["euclid.a.prePad"]  = (float) rhythm.genA.prePad;
            modParamValues["euclid.a.postPad"] = (float) rhythm.genA.postPad;
            modParamValues["euclid.a.insSt"]   = (float) rhythm.genA.insertStart  / (float) juce::jmax(1, stepsA_seed - 1);
            modParamValues["euclid.a.insLen"]  = (float) rhythm.genA.insertLength;
            modParamValues["euclid.b.hits"]    = (float) rhythm.genB.hits         / (float) stepsB_seed;
            modParamValues["euclid.b.rotate"]  = (float) rhythm.genB.rotate       / (float) juce::jmax(1, stepsB_seed - 1);
            modParamValues["euclid.b.prePad"]  = (float) rhythm.genB.prePad;
            modParamValues["euclid.b.postPad"] = (float) rhythm.genB.postPad;
            modParamValues["euclid.b.insSt"]   = (float) rhythm.genB.insertStart  / (float) juce::jmax(1, stepsB_seed - 1);
            modParamValues["euclid.b.insLen"]  = (float) rhythm.genB.insertLength;
            modParamValues["euclid.c.hits"]    = (float) rhythm.genC.hits         / (float) stepsC_seed;
            modParamValues["euclid.c.rotate"]  = (float) rhythm.genC.rotate       / (float) juce::jmax(1, stepsC_seed - 1);
            modParamValues["euclid.c.prePad"]  = (float) rhythm.genC.prePad;
            modParamValues["euclid.c.postPad"] = (float) rhythm.genC.postPad;
            modParamValues["euclid.c.insSt"]   = (float) rhythm.genC.insertStart  / (float) juce::jmax(1, stepsC_seed - 1);
            modParamValues["euclid.c.insLen"]  = (float) rhythm.genC.insertLength;

            rhythm.modulationMatrix.process(rhythm.controlSequences, beatPos, modParamValues);

            rhythm.modLock.store(false, std::memory_order_release);

            // Snapshot pre-normalised values for the UI live-arc indicator (#133).
            {
                auto& snap = modSnapshot[r];
                auto sn = [](float v, float mn, float mx) { return juce::jlimit(0.0f, 1.0f, (v - mn) / (mx - mn)); };
                // Proportion-space destinations (#638) — modParamValues holds slider proportion 0..1.
                // Snap stores the ACTUAL value (seconds / Hz / dB) so the UI's setModulatedActual
                // routes via valueToProportionOfLength and matches the needle's visual position
                // by construction. Same pattern as filter.cutoff (#612) and insert.pN (Stage 36).
                // adsrFromProp / lowCutFromProp / cutoffFromProp come from ModulationSkew.h
                // (brought in via the using-declarations above).
                snap[kSnapAmpAtk]      .store(adsrFromProp(modParamValues["amp.attack"]));
                snap[kSnapAmpDec]      .store(adsrFromProp(modParamValues["amp.decay"]));
                snap[kSnapAmpSus]      .store(sn(modParamValues["amp.sustain"], 0.0f, 100.0f));
                // Filter Cutoff: proportion-space modulation (#639) — snap stores ACTUAL Hz
                // converted from the proportion, so the UI's setModulatedActual goes
                // through the slider's setSkewFactorFromMidPoint(640) via valueToProportionOfLength
                // and the arc matches the visual knob by construction.
                snap[kSnapFilterCutoff].store(cutoffFromProp(modParamValues["filter.cutoff"]));
                snap[kSnapFilterRes]   .store(sn(modParamValues["filter.resonance"], 0.0f, 0.99f));
                // Filter ADSR times: proportion-space modulation (#638) — convert back to actual seconds.
                snap[kSnapFenvAtk]     .store(adsrFromProp(modParamValues["fenv.attack"]));
                snap[kSnapFenvDec]     .store(adsrFromProp(modParamValues["fenv.decay"]));
                // fenv.depth, pitch.envDepth, accentDb: voiceParams units (semis or dB) differ from the
                // slider's 0..100 display. Store the DISPLAY value (slider units) so setModulatedActual
                // routes through the slider's valueToProportionOfLength correctly.
                snap[kSnapFenvDepth]   .store(modParamValues["fenv.depth"]);     // semitones 0..48
                // pitch.semitones: snap stores BASE + OFFSET (in semitones) so the arc tracks the modulated
                // knob position regardless of where the base sits. Pre-fix stored only the offset, so a
                // negative mod read as ABOVE the needle when base was negative (#638-related; T6 follow-up).
                snap[kSnapPitchSemi]   .store(modParams.pitchSemitones + modParamValues["pitch.semitones"]);
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
                snap[kSnapInsP1].store(mu_ui::normToActual(modParamValues["insert.p1"], algForSnap, 0));
                snap[kSnapInsP2].store(mu_ui::normToActual(modParamValues["insert.p2"], algForSnap, 1));
                snap[kSnapInsP3].store(mu_ui::normToActual(modParamValues["insert.p3"], algForSnap, 2));
                snap[kSnapInsP4].store(mu_ui::normToActual(modParamValues["insert.p4"], algForSnap, 3));
                // new destinations — sliders now match voiceParams units (Step 0 of #598),
                // so snapshots store the raw value and setModulatedActual routes through the
                // slider's valueToProportionOfLength directly.
                snap[kSnapPitchEnvDep] .store(modParamValues["pitch.envDepth"]);  // semitones 0..24
                snap[kSnapAmpLvl]      .store(modParamValues["amp.level"]);       // dB -60..+6
                snap[kSnapAccent]      .store(modParamValues["accentDb"]);        // dB 0..12
                // filter.lowCut: proportion-space modulation → actual Hz for setModulatedActual.
                snap[kSnapFilterLowCut].store(lowCutFromProp(modParamValues["filter.lowCut"]));
                // T5 follow-up — pitch.octave: modParamValues holds the modulation offset in SEMITONES (write-back
                // sums it with pitch.semitones into pitchMod). To show the arc on the pitchOctave knob (range -4..+4
                // octaves, linear), store base octave value + offset/12. UI uses setModulatedActual.
                snap[kSnapPitchOctave] .store(modParams.pitchOctave + modParamValues["pitch.octave"] / 12.0f);
                // Euclid pattern destinations (#641):
                //   hits/rotate/insSt: proportion-space mod (modParamValues already holds 0..1
                //     slider proportion). snap stores the proportion directly — UI uses
                //     setModulatedNorm.
                //   prePad/postPad/insLen: additive-in-step-units, FIXED slider ranges (0..12 /
                //     0..12 / 0..8). snap normalises to 0..1 for setModulatedNorm.
                auto prop = [](float v) { return juce::jlimit(0.0f, 1.0f, v); };
                snap[kSnapEucAHits]    .store(prop(modParamValues["euclid.a.hits"]));
                snap[kSnapEucARotate]  .store(prop(modParamValues["euclid.a.rotate"]));
                snap[kSnapEucAPrePad]  .store(sn(modParamValues["euclid.a.prePad"],  0.0f, 12.0f));
                snap[kSnapEucAPostPad] .store(sn(modParamValues["euclid.a.postPad"], 0.0f, 12.0f));
                snap[kSnapEucAInsSt]   .store(prop(modParamValues["euclid.a.insSt"]));
                snap[kSnapEucAInsLen]  .store(sn(modParamValues["euclid.a.insLen"],  0.0f,  8.0f));
                snap[kSnapEucBHits]    .store(prop(modParamValues["euclid.b.hits"]));
                snap[kSnapEucBRotate]  .store(prop(modParamValues["euclid.b.rotate"]));
                snap[kSnapEucBPrePad]  .store(sn(modParamValues["euclid.b.prePad"],  0.0f, 12.0f));
                snap[kSnapEucBPostPad] .store(sn(modParamValues["euclid.b.postPad"], 0.0f, 12.0f));
                snap[kSnapEucBInsSt]   .store(prop(modParamValues["euclid.b.insSt"]));
                snap[kSnapEucBInsLen]  .store(sn(modParamValues["euclid.b.insLen"],  0.0f,  8.0f));
                snap[kSnapEucCHits]    .store(prop(modParamValues["euclid.c.hits"]));
                snap[kSnapEucCRotate]  .store(prop(modParamValues["euclid.c.rotate"]));
                snap[kSnapEucCPrePad]  .store(sn(modParamValues["euclid.c.prePad"],  0.0f, 12.0f));
                snap[kSnapEucCPostPad] .store(sn(modParamValues["euclid.c.postPad"], 0.0f, 12.0f));
                snap[kSnapEucCInsSt]   .store(prop(modParamValues["euclid.c.insSt"]));
                snap[kSnapEucCInsLen]  .store(sn(modParamValues["euclid.c.insLen"],  0.0f,  8.0f));
            }

            // Write modulated values back, clamping to safe ranges. Proportion-space
            // destinations (#638) convert prop → actual via the shared inverse-skew
            // helpers in ModulationSkew.h (adsrFromProp / lowCutFromProp / cutoffFromProp).
            modParams.ampEnvAtk      = juce::jmax(0.001f, adsrFromProp(modParamValues["amp.attack"]));
            modParams.ampEnvDec      = juce::jmax(0.001f, adsrFromProp(modParamValues["amp.decay"]));
            modParams.ampEnvSus      = juce::jlimit(0.0f, 1.0f, modParamValues["amp.sustain"] / 100.0f);
            modParams.filterCutoff   = juce::jlimit(20.0f, 20000.0f, cutoffFromProp(modParamValues["filter.cutoff"]));
            modParams.filterRes      = juce::jlimit(0.0f, 0.99f, modParamValues["filter.resonance"]);
            modParams.filterEnvAtk   = juce::jmax(0.001f, adsrFromProp(modParamValues["fenv.attack"]));
            modParams.filterEnvDec   = juce::jmax(0.001f, adsrFromProp(modParamValues["fenv.decay"]));
            modParams.filterEnvDepth = juce::jlimit(0.0f, 48.0f, modParamValues["fenv.depth"]);
            modParams.filterLowCutHz = lowCutFromProp(modParamValues["filter.lowCut"]);
            // single pitch destination, no more octave×12 + fine/100 stacking.
            modParams.pitchMod       = juce::jlimit(-48.0f, 48.0f,
                                                     modParamValues["pitch.octave"]
                                                   + modParamValues["pitch.semitones"]);
            // Stage 36: insert mod write-back to the 4 generic slots.
            // Values stay normalised 0..1; per-algo de-normalisation
            // happens inside each InsertAlgorithm::process via the config
            // table. No algorithm-specific branching needed.
            modParams.insertParam[0] = juce::jlimit(0.0f, 1.0f, modParamValues["insert.p1"]);
            modParams.insertParam[1] = juce::jlimit(0.0f, 1.0f, modParamValues["insert.p2"]);
            modParams.insertParam[2] = juce::jlimit(0.0f, 1.0f, modParamValues["insert.p3"]);
            modParams.insertParam[3] = juce::jlimit(0.0f, 1.0f, modParamValues["insert.p4"]);
            // new destinations write-back
            modParams.pitchEnvDepth  = juce::jlimit(0.0f,   24.0f,    modParamValues["pitch.envDepth"]);
            modParams.ampLevel       = juce::jlimit(-60.0f,  6.0f,    modParamValues["amp.level"]);                  // dB additive
            modParams.accentDb       = juce::jlimit(0.0f,   12.0f,    modParamValues["accentDb"]);

            // Stage A: write modulated euclid values back to the per-rhythm
            // overrides snapshot. #641: hits/rotate/insSt are proportion-space mod —
            // convert the [0..1] proportion back to integer step count using the
            // current step count of each gen. prePad/postPad/insLen are additive in
            // step units; just round + clamp.
            auto modPropToSteps = [&](const char* key, int steps) {
                return juce::roundToInt(juce::jlimit(0.0f, 1.0f, modParamValues[key]) * (float) steps);
            };
            auto modI = [&](const char* key) {
                return juce::roundToInt(modParamValues[key]);
            };
            const int stepsA_wb = juce::jmax(1, rhythm.genA.steps);
            const int stepsB_wb = juce::jmax(1, rhythm.genB.steps);
            const int stepsC_wb = juce::jmax(1, rhythm.genC.steps);
            lastEuclidOverrides[r].a.hits         = juce::jlimit(0, stepsA_wb,        modPropToSteps("euclid.a.hits",  stepsA_wb));
            lastEuclidOverrides[r].a.rotate       = juce::jlimit(0, stepsA_wb - 1,    modPropToSteps("euclid.a.rotate", stepsA_wb - 1));
            lastEuclidOverrides[r].a.prePad       = juce::jlimit(0, 12, modI("euclid.a.prePad"));
            lastEuclidOverrides[r].a.postPad      = juce::jlimit(0, 12, modI("euclid.a.postPad"));
            lastEuclidOverrides[r].a.insertStart  = juce::jlimit(0, stepsA_wb - 1,    modPropToSteps("euclid.a.insSt", stepsA_wb - 1));
            lastEuclidOverrides[r].a.insertLength = juce::jlimit(0,  8, modI("euclid.a.insLen"));
            lastEuclidOverrides[r].b.hits         = juce::jlimit(0, stepsB_wb,        modPropToSteps("euclid.b.hits",  stepsB_wb));
            lastEuclidOverrides[r].b.rotate       = juce::jlimit(0, stepsB_wb - 1,    modPropToSteps("euclid.b.rotate", stepsB_wb - 1));
            lastEuclidOverrides[r].b.prePad       = juce::jlimit(0, 12, modI("euclid.b.prePad"));
            lastEuclidOverrides[r].b.postPad      = juce::jlimit(0, 12, modI("euclid.b.postPad"));
            lastEuclidOverrides[r].b.insertStart  = juce::jlimit(0, stepsB_wb - 1,    modPropToSteps("euclid.b.insSt", stepsB_wb - 1));
            lastEuclidOverrides[r].b.insertLength = juce::jlimit(0,  8, modI("euclid.b.insLen"));
            lastEuclidOverrides[r].c.hits         = juce::jlimit(0, stepsC_wb,        modPropToSteps("euclid.c.hits",  stepsC_wb));
            lastEuclidOverrides[r].c.rotate       = juce::jlimit(0, stepsC_wb - 1,    modPropToSteps("euclid.c.rotate", stepsC_wb - 1));
            lastEuclidOverrides[r].c.prePad       = juce::jlimit(0, 12, modI("euclid.c.prePad"));
            lastEuclidOverrides[r].c.postPad      = juce::jlimit(0, 12, modI("euclid.c.postPad"));
            lastEuclidOverrides[r].c.insertStart  = juce::jlimit(0, stepsC_wb - 1,    modPropToSteps("euclid.c.insSt", stepsC_wb - 1));
            lastEuclidOverrides[r].c.insertLength = juce::jlimit(0,  8, modI("euclid.c.insLen"));
        }
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
        const int busIdx = kFirstDirectOutBus + i;
        if (busIdx < getBusCount(false))
            if (auto* bus = getBus(false, busIdx))
                if (bus->isEnabled())
                {
                    directBufs[(size_t) i] = getBusBuffer(buffer, false, busIdx);
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
void PluginProcessor::setMidiSyncEnabled(bool on)
{
    midiClockSync.setEnabled(on);
    appSettings->setValue("midiSyncEnabled", on);
    appSettings->saveIfNeeded();
}

void PluginProcessor::setMidiSyncMessages(int mode)
{
    midiClockSync.setMessages(mode);
    appSettings->setValue("midiSyncMessages", mode);
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

void PluginProcessor::setMultiBusEnabled(bool on)
{
    multiBusEnabled.store(on, std::memory_order_relaxed);
    appSettings->setValue("multiBusEnabled", on);
    appSettings->saveIfNeeded();
}

void PluginProcessor::setUiScale(float scale)
{
    const float clamped = juce::jlimit(kUiScaleMedium, kUiScaleLarge, scale);
    if (uiScale == clamped) return;
    // Persist before delegating — the base fires onUiScaleChanged after the
    // store, so a listener that re-reads from appSettings sees the new value.
    if (appSettings != nullptr)
    {
        appSettings->setValue("uiScale", (double) clamped);
        appSettings->saveIfNeeded();
    }
    ProcessorBase::setUiScale(clamped);
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
void PluginProcessor::handleAsyncUpdate()
{
    // Drain retired engines + commit pending swaps (both sides of the hot-swap lifecycle).
    hotSwapStager.processSwaps();

    // Drain MIDI program-change queue → dispatch via the virtual hooks. The
    // base method calls applyMidiPresetSlot / applyFullMidiPreset for each
    // queued event; our overrides stage a rhythm preset or load a full preset.
    drainPendingMidiProgramChanges();
}

//==============================================================================
juce::File PluginProcessor::getContentDir() const
{
    if (appSettings != nullptr)
    {
        const juce::String stored = appSettings->getValue("contentDir");
        if (stored.isNotEmpty())
            return juce::File(stored);
    }
    return juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
               .getChildFile("TDP").getChildFile("muClid");
}

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
