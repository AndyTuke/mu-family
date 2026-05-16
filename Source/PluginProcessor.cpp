#include "PluginProcessor.h"
#if MUCLID_LITE_BUILD
#include "LiteEditor.h"
#else
#include "PluginEditor.h"
#endif
#include "Sequencer/Rhythm.h"
#include "FX/FXAlgorithmDef.h"

#include <thread>   // #237: std::this_thread::yield in modulator deserialise lock-spin

// Declare 10 stereo output buses: Master (always enabled), Out 1..8 + FX Returns
// (disabled by default so a fresh project loads with just one stereo output, matching
// pre-multi-bus behaviour). Hosts that support it can enable the extra buses.
PluginProcessor::PluginProcessor()
#if MUCLID_LITE_BUILD
    : ProcessorBase(BusesProperties()),
#else
    : ProcessorBase(BusesProperties()
          .withOutput("Master",     juce::AudioChannelSet::stereo(), true)
          .withOutput("Out 1",      juce::AudioChannelSet::stereo(), false)
          .withOutput("Out 2",      juce::AudioChannelSet::stereo(), false)
          .withOutput("Out 3",      juce::AudioChannelSet::stereo(), false)
          .withOutput("Out 4",      juce::AudioChannelSet::stereo(), false)
          .withOutput("Out 5",      juce::AudioChannelSet::stereo(), false)
          .withOutput("Out 6",      juce::AudioChannelSet::stereo(), false)
          .withOutput("Out 7",      juce::AudioChannelSet::stereo(), false)
          .withOutput("Out 8",      juce::AudioChannelSet::stereo(), false)
          .withOutput("FX Returns", juce::AudioChannelSet::stereo(), false)),
#endif
      apvts(*this, nullptr, "MuClidState", createParameterLayout())
{
    previewFormatManager.registerBasicFormats();

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
    midiSyncEnabled .store(appSettings->getBoolValue("midiSyncEnabled",  false),
                           std::memory_order_relaxed);
    midiSyncMessages.store(appSettings->getIntValue ("midiSyncMessages", 2),
                           std::memory_order_relaxed);

    // Load MIDI program-change preset map (lives in its own JSON file).
    midiPresetMap.load();

    // Multi-bus output toggle (DAW). Default: on.
    multiBusEnabled.store(appSettings->getBoolValue("multiBusEnabled", true),
                          std::memory_order_relaxed);

    // Check license file — must run after appSettings so getContentDir() works.
    licenseInfo = LicenseChecker::check(getContentDir());

    // Register listener for every parameter.
    for (auto* param : getParameters())
        if (auto* p = dynamic_cast<juce::AudioProcessorParameterWithID*>(param))
            apvts.addParameterListener(p->getParameterID(), this);

    // Initialise sample-path slots.
    for (int i = 0; i < SequencerEngine::MaxRhythms; ++i)
        loadedSamplePaths.add(juce::String());

    // Pre-populate modulation param map so lookups never allocate on the audio thread.
    // pitch.fine deprecated by #218 — not in the map; legacy assignments silently no-op.
    modParamValues.reserve(44);
    for (const char* key : { "amp.attack", "amp.decay", "amp.sustain", "amp.release",
                              "filter.cutoff", "filter.resonance",
                              "fenv.attack", "fenv.decay", "fenv.depth",
                              "pitch.semitones", "pitch.octave",
                              "insert.drive", "insert.output",
                              "insert.bits", "insert.rate", "insert.dither", "insert.lpf",
                              // #223 additions
                              "pitch.envDepth", "amp.level", "accentDb",
                              // #336: euclid pattern destinations (Stage A wiring)
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
    numActiveRhythms.store(1, std::memory_order_release);

    apvtsLoading = true;
    pushRhythmToAPVTS(0);
    apvtsLoading = false;

#if !MUCLID_LITE_BUILD
    // Ensure user content folders exist and load the default preset if present.
    ensureContentFoldersExist();
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
#if MUCLID_LITE_BUILD
    return "mu-Clid Lite";
#else
    return "mu-Clid";
#endif
}
double PluginProcessor::getTailLengthSeconds() const
{
    // #367: cover worst-case wet tail so hosts don't crop the audio after transport stop.
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
    fxChain.prepare(sampleRate, samplesPerBlock);
    mixerEngine.prepare(sampleRate, samplesPerBlock);
    previewTransport.prepareToPlay(samplesPerBlock, sampleRate);
    previewScratchBuffer.setSize(2, samplesPerBlock, false, true, true);
#endif
}

void PluginProcessor::releaseResources()
{
    previewTransport.releaseResources();
}

void PluginProcessor::processBlock(juce::AudioBuffer<float>& buffer,
                                   juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();

#if MUCLID_LITE_BUILD
    // Lite mode: MIDI-only sequencing, no audio processing.
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
        const int ns = juce::jmax(1, buffer.getNumSamples());
        internalBeatPos.store(pos + (ns / currentSampleRate) * (internalBpm / 60.0),
                              std::memory_order_relaxed);
    }

    sequencerPlaying.set(playing);
    lastBeatPos.set(beatPos);

    const juce::ScopedTryLock rLock(rhythmsLock);
    if (!rLock.isLocked()) return;
    const int numRhythms = numActiveRhythms.load(std::memory_order_acquire);

    if (playing)
    {
        const auto blockResult = sequencer.processBlock(beatPos);
        {
            const int   midiNote  = (int)apvts.getRawParameterValue("lite_midiNote")->load();
            const float accentAmt = apvts.getRawParameterValue("lite_accentAmt")->load();

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
                    const bool  isAccented = (blockResult.accentMask & (1 << r)) != 0;
                    midiEngines[r].trigger(midiMessages, 0, midiNote, 1,
                                           isAccented ? accentedVel : normalVel);
                    rhythmPlayState[r].hitFired.set(true);
                    rhythmPlayState[r].hitCount.set(rhythmPlayState[r].hitCount.get() + 1);
                }
            }
        }
        const float frac = static_cast<float>(
            std::fmod(beatPos / SequencerEngine::StepLengthBeats, 1.0));
        beatFraction.set(frac);
        for (int r = 0; r < numRhythms; ++r)
        {
            rhythmPlayState[r].currentStep  .set(sequencer.getLastStepIndex(r));
            rhythmPlayState[r].currentStepC .set(sequencer.getLastAccentStepIndex(r));
            rhythmPlayState[r].patternLength.set(sequencer.getPatternLength(r));
            const Rhythm& rhy = sequencer.getRhythm(r);
            rhythmPlayState[r].stepsA.set(juce::jmax(1, rhy.genA.steps));
            rhythmPlayState[r].stepsB.set(juce::jmax(1, rhy.genB.steps));
            rhythmPlayState[r].stepsC.set(juce::jmax(1, rhy.genC.steps));
        }
    }
    for (int r = 0; r < numRhythms; ++r)
        midiEngines[r].processBlock(midiMessages, juce::jmax(1, buffer.getNumSamples()));
#else

    // MIDI clock sync: scan system real-time messages before beat-pos determination.
    double midiClockBlockBeatPos = 0.0;
    if (midiSyncEnabled.load(std::memory_order_relaxed) && wrapperType == wrapperType_Standalone)
    {
        const bool doTick      = (midiSyncMessages.load(std::memory_order_relaxed) != 1);
        const bool doTransport = (midiSyncMessages.load(std::memory_order_relaxed) != 0);
        const int  numSamples  = buffer.getNumSamples();
        midiClockBlockBeatPos  = midiClockBeatPos;  // start-of-block position
        int prevTickSo = 0;

        for (const auto& msgRef : midiMessages)
        {
            const auto& m = msgRef.getMessage();
            if (m.getRawDataSize() != 1) continue;
            const juce::uint8 b  = m.getRawData()[0];
            const int   so = msgRef.samplePosition;

            if (doTransport)
            {
                if (b == 0xFA)
                {
                    midiClockBeatPos = 0.0;  midiClockBlockBeatPos = 0.0;
                    midiClockRingCount = 0;  midiClockSamplesSinceLastTick = 0;
                    prevTickSo = 0;
                    midiClockIsPlaying.set(true);
                }
                else if (b == 0xFB) { midiClockIsPlaying.set(true); }
                else if (b == 0xFC) { midiClockIsPlaying.set(false); }
            }

            if (doTick && b == 0xF8)
            {
                const int interval = midiClockSamplesSinceLastTick + (so - prevTickSo);
                if (midiClockRingCount > 0 && interval > 10)
                {
                    midiClockTickIntervals[midiClockRingHead] = interval;
                    midiClockRingHead = (midiClockRingHead + 1) % 24;
                    if (midiClockRingCount < 24) ++midiClockRingCount;
                    double sum = 0.0;
                    for (int i = 0; i < midiClockRingCount; ++i) sum += midiClockTickIntervals[i];
                    midiClockBpmEst.set(juce::jlimit(20.0, 300.0,
                        60.0 * currentSampleRate / ((sum / midiClockRingCount) * 24.0)));
                }
                else if (midiClockRingCount == 0) { ++midiClockRingCount; }
                midiClockSamplesSinceLastTick = 0;
                prevTickSo = so;
                midiClockBeatPos += 1.0 / 24.0;
            }
        }
        midiClockSamplesSinceLastTick += numSamples - prevTickSo;
        midiClockBeatPosUI.store(midiClockBeatPos, std::memory_order_relaxed);
    }

    // MIDI program change → rhythm preset (channel N → slot N-1, program = preset index).
    // Audio thread enqueues into a lock-free FIFO; handleAsyncUpdate drains on the message
    // thread and calls stageRhythmPreset (which can do file I/O).
    {
        const uint8_t chMask = midiPresetMap.getChannelMask();
        if (chMask != 0)
        {
            bool needPC = false;
            for (const auto& msgRef : midiMessages)
            {
                const auto& m = msgRef.getMessage();
                if (! m.isProgramChange()) continue;
                const int ch = m.getChannel();      // 1-based, 1..16
                if (ch < 1 || ch > 8) continue;
                if (! (chMask & (1 << (ch - 1)))) continue;
                int start1, size1, start2, size2;
                pcFifo.prepareToWrite(1, start1, size1, start2, size2);
                if (size1 + size2 > 0)
                {
                    const int dst = (size1 > 0) ? start1 : start2;
                    pcQueue[(size_t) dst] = { ch - 1, m.getProgramChangeNumber() };
                    pcFifo.finishedWrite(1);
                    needPC = true;
                }
            }
            if (needPC) triggerAsyncUpdate();
        }
    }

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

    if (!playing && midiSyncEnabled.load(std::memory_order_relaxed)
                 && wrapperType == wrapperType_Standalone
                 && (midiClockIsPlaying.get() || internalPlaying.load(std::memory_order_relaxed)))
    {
        playing = true;
        beatPos = midiClockBlockBeatPos;
    }
    else if (!playing && internalPlaying.load(std::memory_order_relaxed))
    {
        playing  = true;
        const double pos = internalBeatPos.load(std::memory_order_relaxed);
        beatPos  = pos;
        internalBeatPos.store(pos + (buffer.getNumSamples() / currentSampleRate) * (internalBpm / 60.0),
                              std::memory_order_relaxed);
    }

    // #231: detect transport stop→start edge and reset the sequencer's wrap detector
    // so the first block after restart can't emit a false masterLoopWrapped from
    // a stale lastEffectiveStep.
    {
        const bool wasPlaying = sequencerPlaying.get();
        if (playing && !wasPlaying)
            sequencer.resetWrapDetector();
    }
    sequencerPlaying.set(playing);
    lastBeatPos.set(beatPos);

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
    {
        const auto blockResult = sequencer.processBlock(beatPos);
        for (int r = 0; r < numRhythms; ++r)
            if ((blockResult.firedMask & (1 << r)) && voiceEngines[r])
                voiceEngines[r]->trigger(blockResult.accentMask & (1 << r));

        // Hot-swap: check if any staged rhythm preset should be committed at this boundary.
        const int mode = swapModeAtomic.load(std::memory_order_relaxed);
        bool needAsync = false;
        for (int r = 0; r < numRhythms; ++r)
        {
            auto& sw = pendingSwaps[r];
            if (sw.isReady.load(std::memory_order_acquire)
                && !sw.boundaryReached.load(std::memory_order_relaxed))
            {
                const bool wrap = (mode == 0) ? blockResult.masterLoopWrapped
                                              : ((blockResult.rhythmLoopWrapMask & (1 << r)) != 0);
                if (wrap)
                {
                    sw.boundaryReached.store(true, std::memory_order_release);
                    needAsync = true;
                }
            }
        }
        if (needAsync)
            triggerAsyncUpdate();

        // Update UI play-state atomics.
        const float frac = static_cast<float>(
            std::fmod(beatPos / SequencerEngine::StepLengthBeats, 1.0));
        beatFraction.set(frac);
        for (int r = 0; r < numRhythms; ++r)
        {
            rhythmPlayState[r].currentStep  .set(sequencer.getLastStepIndex(r));
            rhythmPlayState[r].currentStepC .set(sequencer.getLastAccentStepIndex(r));
            rhythmPlayState[r].patternLength.set(sequencer.getPatternLength(r));
            const Rhythm& rhy = sequencer.getRhythm(r);
            rhythmPlayState[r].stepsA.set(juce::jmax(1, rhy.genA.steps));
            rhythmPlayState[r].stepsB.set(juce::jmax(1, rhy.genB.steps));
            rhythmPlayState[r].stepsC.set(juce::jmax(1, rhy.genC.steps));
            if (blockResult.firedMask & (1 << r))
            {
                rhythmPlayState[r].hitFired.set(true);
                rhythmPlayState[r].hitCount.set(rhythmPlayState[r].hitCount.get() + 1); // Issue #43: monotonic counter for race-free UI hit detection
            }
        }
    }

    // Apply modulation: compute per-rhythm modulated VoiceParams from the modulation matrix.
    // Uses a try-lock so the audio thread never blocks on the message thread.
    for (int r = 0; r < numRhythms; ++r)
    {
        Rhythm& rhythm = sequencer.getRhythm(r);
        VoiceParams modParams = rhythm.voiceParams;

        // #345 / #336: gate the modulation pass on "matrix has assignments now, OR had
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
                // Fill param map with base values in 0–100 display scale.
                modParamValues["amp.attack"]       = modParams.ampEnvAtk   * (100.0f/3.0f);
                modParamValues["amp.decay"]        = modParams.ampEnvDec   * (100.0f/3.0f);
                modParamValues["amp.sustain"]      = modParams.ampEnvSus   * 100.0f;
                modParamValues["amp.release"]      = modParams.ampEnvRel   * (100.0f/3.0f);
                modParamValues["filter.cutoff"]    = modParams.filterCutoff;
                modParamValues["filter.resonance"] = modParams.filterRes   * 100.0f;
                modParamValues["fenv.attack"]      = modParams.filterEnvAtk * (100.0f/3.0f);
                modParamValues["fenv.decay"]       = modParams.filterEnvDec * (100.0f/3.0f);
                modParamValues["fenv.depth"]       = modParams.filterEnvDepth;
                // pitch.octave and pitch.semitones both start at 0; summed at write-back → pitchMod.
                // pitch.fine is deprecated (#218) — not in map, legacy assignments silently no-op.
                modParamValues["pitch.semitones"]  = 0.0f;
                modParamValues["pitch.octave"]     = 0.0f;
                modParamValues["insert.drive"]     = modParams.driveDrive;
                modParamValues["insert.output"]    = modParams.driveOutput;
                modParamValues["insert.bits"]      = modParams.drvBits;
                modParamValues["insert.rate"]      = (std::log(modParams.driveRate) - std::log(100.0f)) / (std::log(48000.0f) - std::log(100.0f)) * 100.0f;
                modParamValues["insert.dither"]    = modParams.drvDither;
                modParamValues["insert.lpf"]       = modParams.driveTone;
                // #223 new destinations
                modParamValues["pitch.envDepth"]   = modParams.pitchEnvDepth;
                modParamValues["amp.level"]        = modParams.ampLevel;
                modParamValues["accentDb"]         = modParams.accentDb;
                // #336 Stage A: seed euclid pattern destinations with base gen values.
                modParamValues["euclid.a.hits"]    = (float) rhythm.genA.hits;
                modParamValues["euclid.a.rotate"]  = (float) rhythm.genA.rotate;
                modParamValues["euclid.a.prePad"]  = (float) rhythm.genA.prePad;
                modParamValues["euclid.a.postPad"] = (float) rhythm.genA.postPad;
                modParamValues["euclid.a.insSt"]   = (float) rhythm.genA.insertStart;
                modParamValues["euclid.a.insLen"]  = (float) rhythm.genA.insertLength;
                modParamValues["euclid.b.hits"]    = (float) rhythm.genB.hits;
                modParamValues["euclid.b.rotate"]  = (float) rhythm.genB.rotate;
                modParamValues["euclid.b.prePad"]  = (float) rhythm.genB.prePad;
                modParamValues["euclid.b.postPad"] = (float) rhythm.genB.postPad;
                modParamValues["euclid.b.insSt"]   = (float) rhythm.genB.insertStart;
                modParamValues["euclid.b.insLen"]  = (float) rhythm.genB.insertLength;
                modParamValues["euclid.c.hits"]    = (float) rhythm.genC.hits;
                modParamValues["euclid.c.rotate"]  = (float) rhythm.genC.rotate;
                modParamValues["euclid.c.prePad"]  = (float) rhythm.genC.prePad;
                modParamValues["euclid.c.postPad"] = (float) rhythm.genC.postPad;
                modParamValues["euclid.c.insSt"]   = (float) rhythm.genC.insertStart;
                modParamValues["euclid.c.insLen"]  = (float) rhythm.genC.insertLength;

                rhythm.modulationMatrix.process(rhythm.controlSequences, beatPos, modParamValues);

                rhythm.modLock.store(false, std::memory_order_release);

                // Snapshot pre-normalised values for the UI live-arc indicator (#133).
                {
                    auto& snap = modSnapshot[r];
                    auto sn = [](float v, float mn, float mx) { return juce::jlimit(0.0f, 1.0f, (v - mn) / (mx - mn)); };
                    // #216: Hz destinations use log normalisation to match the slider's log skew,
                    // so the live-arc dot tracks the visible knob position.
                    auto snLogHz = [](float v) {
                        return juce::jlimit(0.0f, 1.0f,
                            std::log(juce::jmax(20.0f, v) / 20.0f) / std::log(1000.0f));  // log(20000/20)
                    };
                    snap[kSnapAmpAtk]      .set(sn(modParamValues["amp.attack"],       0.0f,    100.0f));
                    snap[kSnapAmpDec]      .set(sn(modParamValues["amp.decay"],        0.0f,    100.0f));
                    snap[kSnapAmpSus]      .set(sn(modParamValues["amp.sustain"],      0.0f,    100.0f));
                    snap[kSnapAmpRel]      .set(sn(modParamValues["amp.release"],      0.0f,    100.0f));
                    snap[kSnapFilterCutoff].set(snLogHz(modParamValues["filter.cutoff"]));
                    snap[kSnapFilterRes]   .set(sn(modParamValues["filter.resonance"], 0.0f,    100.0f));
                    snap[kSnapFenvAtk]     .set(sn(modParamValues["fenv.attack"],      0.0f,    100.0f));
                    snap[kSnapFenvDec]     .set(sn(modParamValues["fenv.decay"],       0.0f,    100.0f));
                    snap[kSnapFenvDepth]   .set(sn(modParamValues["fenv.depth"],       0.0f,     48.0f));
                    snap[kSnapPitchSemi]   .set(sn(modParamValues["pitch.semitones"], -12.0f,    12.0f));
                    snap[kSnapPitchOct]    .set(sn(modParamValues["pitch.octave"],    -36.0f,    36.0f));
                    snap[kSnapPitchFine]   .set(0.0f);   // deprecated by #218
                    snap[kSnapInsDrive]    .set(sn(modParamValues["insert.drive"],     0.0f,    100.0f));
                    snap[kSnapInsOutput]   .set(sn(modParamValues["insert.output"],   -24.0f,    0.0f));
                    snap[kSnapInsBits]     .set(sn(modParamValues["insert.bits"],      1.0f,     16.0f));
                    snap[kSnapInsDither]   .set(sn(modParamValues["insert.dither"],    0.0f,    100.0f));
                    snap[kSnapInsLpf]      .set(snLogHz(modParamValues["insert.lpf"]));   // #216 log
                    // #223 new destinations
                    snap[kSnapPitchEnvDep] .set(sn(modParamValues["pitch.envDepth"],   0.0f,    24.0f));
                    snap[kSnapAmpLvl]      .set(sn(modParamValues["amp.level"],        0.0f,     2.0f));
                    snap[kSnapAccent]      .set(sn(modParamValues["accentDb"],         0.0f,    12.0f));
                    // #336 Stage C: euclid pattern destinations. hits/rotate/insSt are
                    // normalised against the UI slider's *current* range (which is per-
                    // rhythm: hits=0..steps, rotate/insSt=0..steps-1) so the live arc on
                    // the knob renders in the correct direction. prePad/postPad/insLen
                    // have fixed slider ranges so use the constant values.
                    const int stepsA = juce::jmax(1, rhythm.genA.steps);
                    const int stepsB = juce::jmax(1, rhythm.genB.steps);
                    const int stepsC = juce::jmax(1, rhythm.genC.steps);
                    snap[kSnapEucAHits]    .set(sn(modParamValues["euclid.a.hits"],    0.0f, (float) stepsA));
                    snap[kSnapEucARotate]  .set(sn(modParamValues["euclid.a.rotate"],  0.0f, (float) juce::jmax(1, stepsA - 1)));
                    snap[kSnapEucAPrePad]  .set(sn(modParamValues["euclid.a.prePad"],  0.0f, 12.0f));
                    snap[kSnapEucAPostPad] .set(sn(modParamValues["euclid.a.postPad"], 0.0f, 12.0f));
                    snap[kSnapEucAInsSt]   .set(sn(modParamValues["euclid.a.insSt"],   0.0f, (float) juce::jmax(1, stepsA - 1)));
                    snap[kSnapEucAInsLen]  .set(sn(modParamValues["euclid.a.insLen"],  0.0f,  8.0f));
                    snap[kSnapEucBHits]    .set(sn(modParamValues["euclid.b.hits"],    0.0f, (float) stepsB));
                    snap[kSnapEucBRotate]  .set(sn(modParamValues["euclid.b.rotate"],  0.0f, (float) juce::jmax(1, stepsB - 1)));
                    snap[kSnapEucBPrePad]  .set(sn(modParamValues["euclid.b.prePad"],  0.0f, 12.0f));
                    snap[kSnapEucBPostPad] .set(sn(modParamValues["euclid.b.postPad"], 0.0f, 12.0f));
                    snap[kSnapEucBInsSt]   .set(sn(modParamValues["euclid.b.insSt"],   0.0f, (float) juce::jmax(1, stepsB - 1)));
                    snap[kSnapEucBInsLen]  .set(sn(modParamValues["euclid.b.insLen"],  0.0f,  8.0f));
                    snap[kSnapEucCHits]    .set(sn(modParamValues["euclid.c.hits"],    0.0f, (float) stepsC));
                    snap[kSnapEucCRotate]  .set(sn(modParamValues["euclid.c.rotate"],  0.0f, (float) juce::jmax(1, stepsC - 1)));
                    snap[kSnapEucCPrePad]  .set(sn(modParamValues["euclid.c.prePad"],  0.0f, 12.0f));
                    snap[kSnapEucCPostPad] .set(sn(modParamValues["euclid.c.postPad"], 0.0f, 12.0f));
                    snap[kSnapEucCInsSt]   .set(sn(modParamValues["euclid.c.insSt"],   0.0f, (float) juce::jmax(1, stepsC - 1)));
                    snap[kSnapEucCInsLen]  .set(sn(modParamValues["euclid.c.insLen"],  0.0f,  8.0f));
                }

                // Write modulated values back, clamping to safe ranges.
                modParams.ampEnvAtk      = juce::jmax(0.001f, modParamValues["amp.attack"]   * 0.03f);
                modParams.ampEnvDec      = juce::jmax(0.001f, modParamValues["amp.decay"]    * 0.03f);
                modParams.ampEnvSus      = juce::jlimit(0.0f, 1.0f, modParamValues["amp.sustain"] / 100.0f);
                modParams.ampEnvRel      = juce::jmax(0.001f, modParamValues["amp.release"]  * 0.03f);
                modParams.filterCutoff   = juce::jlimit(20.0f, 20000.0f, modParamValues["filter.cutoff"]);
                modParams.filterRes      = juce::jlimit(0.0f, 0.99f, modParamValues["filter.resonance"] / 100.0f);
                modParams.filterEnvAtk   = juce::jmax(0.001f, modParamValues["fenv.attack"]  * 0.03f);
                modParams.filterEnvDec   = juce::jmax(0.001f, modParamValues["fenv.decay"]   * 0.03f);
                modParams.filterEnvDepth = juce::jlimit(0.0f, 48.0f, modParamValues["fenv.depth"]);
                // #218: single pitch destination, no more octave×12 + fine/100 stacking.
                modParams.pitchMod       = juce::jlimit(-48.0f, 48.0f,
                                                         modParamValues["pitch.octave"]
                                                       + modParamValues["pitch.semitones"]);
                modParams.driveDrive     = juce::jlimit(0.0f,  100.0f,    modParamValues["insert.drive"]);
                modParams.driveOutput    = juce::jlimit(-24.0f, 24.0f,    modParamValues["insert.output"]);
                modParams.drvBits        = juce::jlimit(1.0f,   16.0f,    modParamValues["insert.bits"]);
                modParams.driveRate      = std::exp(std::log(100.0f) + juce::jlimit(0.0f, 100.0f, modParamValues["insert.rate"]) / 100.0f * (std::log(48000.0f) - std::log(100.0f)));
                modParams.drvDither      = juce::jlimit(0.0f,  100.0f,    modParamValues["insert.dither"]);
                modParams.driveTone      = juce::jlimit(20.0f, 20000.0f,  modParamValues["insert.lpf"]);
                // #223 new destinations write-back
                modParams.pitchEnvDepth  = juce::jlimit(0.0f,   24.0f,    modParamValues["pitch.envDepth"]);
                modParams.ampLevel       = juce::jlimit(0.0f,    2.0f,    modParamValues["amp.level"]);
                modParams.accentDb       = juce::jlimit(0.0f,   12.0f,    modParamValues["accentDb"]);

                // #336 Stage A: write modulated euclid values back to the per-rhythm
                // overrides snapshot. Integer-rounded so Stage B change-detection skips
                // sub-step LFO jitter. Clamps applied per the destination ranges in
                // ModDest::kTable (hits/rotate: 0..steps; pad: 0..12 / 0..63 / 0..8).
                auto modI = [&](const char* key) {
                    return juce::roundToInt(modParamValues[key]);
                };
                lastEuclidOverrides[r].a.hits         = juce::jlimit(0, 64, modI("euclid.a.hits"));
                lastEuclidOverrides[r].a.rotate       = juce::jlimit(0, 63, modI("euclid.a.rotate"));
                lastEuclidOverrides[r].a.prePad       = juce::jlimit(0, 12, modI("euclid.a.prePad"));
                lastEuclidOverrides[r].a.postPad      = juce::jlimit(0, 12, modI("euclid.a.postPad"));
                lastEuclidOverrides[r].a.insertStart  = juce::jlimit(0, 63, modI("euclid.a.insSt"));
                lastEuclidOverrides[r].a.insertLength = juce::jlimit(0,  8, modI("euclid.a.insLen"));
                lastEuclidOverrides[r].b.hits         = juce::jlimit(0, 64, modI("euclid.b.hits"));
                lastEuclidOverrides[r].b.rotate       = juce::jlimit(0, 63, modI("euclid.b.rotate"));
                lastEuclidOverrides[r].b.prePad       = juce::jlimit(0, 12, modI("euclid.b.prePad"));
                lastEuclidOverrides[r].b.postPad      = juce::jlimit(0, 12, modI("euclid.b.postPad"));
                lastEuclidOverrides[r].b.insertStart  = juce::jlimit(0, 63, modI("euclid.b.insSt"));
                lastEuclidOverrides[r].b.insertLength = juce::jlimit(0,  8, modI("euclid.b.insLen"));
                lastEuclidOverrides[r].c.hits         = juce::jlimit(0, 64, modI("euclid.c.hits"));
                lastEuclidOverrides[r].c.rotate       = juce::jlimit(0, 63, modI("euclid.c.rotate"));
                lastEuclidOverrides[r].c.prePad       = juce::jlimit(0, 12, modI("euclid.c.prePad"));
                lastEuclidOverrides[r].c.postPad      = juce::jlimit(0, 12, modI("euclid.c.postPad"));
                lastEuclidOverrides[r].c.insertStart  = juce::jlimit(0, 63, modI("euclid.c.insSt"));
                lastEuclidOverrides[r].c.insertLength = juce::jlimit(0,  8, modI("euclid.c.insLen"));
            }
        }

        // #336 Stage B: trigger pattern recompute when integer-rounded overrides changed
        // since last block. Skips recompute on every block where modulation is sub-step
        // (typical case for a slow LFO). tryUpdatePatternFromModulation is non-blocking —
        // a missed try-lock just defers to the next block; prevEuclidOverrides only
        // advances when the recompute actually applied so the change-detection retries.
        if (lastEuclidOverrides[r] != prevEuclidOverrides[r])
        {
            if (sequencer.tryUpdatePatternFromModulation(r, lastEuclidOverrides[r]))
                prevEuclidOverrides[r] = lastEuclidOverrides[r];
        }

        // #357: only override activeParams when the modulation pass actually ran. For an
        // unmodulated rhythm modParams ≡ rhythm.voiceParams ≡ activeParams (already kept in
        // sync by VoiceEngine::applyPendingParams' dirty-flag path), so the call would be
        // pure waste — re-syncing ADSR + filter every block for no change.
        if (runModulationPass && voiceEngines[r])
            voiceEngines[r]->setActiveParams(modParams);
    }

    // Effective BPM for tempo-synced FX (Delay, Echo): host playhead takes priority
    // in DAW mode, MIDI clock estimate when locked in standalone, otherwise the
    // internal transport. Previously this always used internalBpm, so DAW-hosted
    // sessions saw the delay always tempo-synced to 120 regardless of host tempo.
    double effectiveBpm = internalBpm;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
            if (auto hostBpm = pos->getBpm())
                effectiveBpm = *hostBpm;
    if (midiSyncEnabled.load(std::memory_order_relaxed)
        && wrapperType == wrapperType_Standalone
        && midiClockIsPlaying.get())
        effectiveBpm = midiClockBpmEst.get();
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

    processCoreBlock(masterBus, voiceEngines.data(), numRhythms,
                     buffer.getNumSamples(), effectiveBpm, &directPtrs, fxRetPtr);

    // Mix sample preview (for file browser audition) directly into the master output.
    if (previewTransport.isPlaying())
    {
        const int ns = buffer.getNumSamples();
        previewScratchBuffer.clear(0, 0, ns);
        previewScratchBuffer.clear(1, 0, ns);
        previewTransport.getNextAudioBlock({ &previewScratchBuffer, 0, ns });
        for (int ch = 0; ch < masterBus.getNumChannels(); ++ch)
            masterBus.addFrom(ch, 0, previewScratchBuffer,
                              ch % previewScratchBuffer.getNumChannels(), 0, ns, 0.7f);
    }

    for (int r = 0; r < numRhythms; ++r)
        midiEngines[r].processBlock(midiMessages, buffer.getNumSamples());
#endif
}

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
    apvtsLoading = true;
    pushRhythmToAPVTS(ri);
    apvtsLoading = false;
}

void PluginProcessor::resetPlayState(int idx)
{
    if (idx < 0 || idx >= SequencerEngine::MaxRhythms) return;
    auto& s = rhythmPlayState[idx];
    s.currentStep  .set(0);
    s.patternLength.set(1);
    s.stepsA       .set(1);
    s.stepsB       .set(1);
    s.stepsC       .set(1);
    s.hitFired     .set(false);
    s.hitCount     .set(0);
}
bool PluginProcessor::swapRhythms(int i, int j)
{
    const int n = numActiveRhythms.load(std::memory_order_acquire);
    if (i < 0 || j < 0 || i >= n || j >= n || i == j) return false;

    // #235: suspendProcessing only sets a flag — it does NOT block the in-flight
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
            if      (src == i) src = j;
            else if (src == j) src = i;
        }

        std::swap(mixerEngine.channels[i], mixerEngine.channels[j]);

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
            mixerEngine.channels[i] = mixerEngine.channels[i + 1];
        }
        voiceEngines[newN].reset();
        midiEngines[newN] = MidiOutputEngine{};
        mixerEngine.channels[newN] = MixerEngine::ChannelState{};
    }
    suspendProcessing(false);
}

void PluginProcessor::resetRhythm(int index)
{
    if (index < 0 || index >= sequencer.getNumRhythms()) return;

    // #355: was a UI-thread spin on rhythm.modLock + concurrent vector destruction
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

    // #356: rhythmsLock serialises with the audio thread's ScopedTryLock in
    // processBlock. No audio-thread reader of name today, but the lock matches the
    // project's "message thread mutates Rhythm" convention and stays correct if a
    // future audio-path consumer (e.g. MIDI program-change preset matcher) reads it.
    const juce::ScopedLock sl(rhythmsLock);
    sequencer.getRhythm(index).name = newName.toStdString();
}

//==============================================================================
void PluginProcessor::setMidiSyncEnabled(bool on)
{
    midiSyncEnabled.store(on, std::memory_order_relaxed);
    if (!on) midiClockIsPlaying.set(false);
    appSettings->setValue("midiSyncEnabled", on);
    appSettings->saveIfNeeded();
}

void PluginProcessor::setMidiSyncMessages(int mode)
{
    midiSyncMessages.store(juce::jlimit(0, 2, mode), std::memory_order_relaxed);
    appSettings->setValue("midiSyncMessages", mode);
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
    const auto& outs = layouts.outputBuses;
    if (outs.size() < 1 || outs.size() > kTotalBuses)
        return false;

    // Multi-bus disabled: only allow a single stereo output.
    if (! multiBusEnabled.load(std::memory_order_relaxed) && outs.size() > 1)
        return false;

    // Each declared bus must be either stereo or disabled.
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

void PluginProcessor::startSamplePreview(const juce::File& file)
{
    if (!file.existsAsFile()) return;
    auto* reader = previewFormatManager.createReaderFor(file);
    if (!reader) return;
    previewTransport.stop();
    previewTransport.setSource(nullptr);
    previewSource = std::make_unique<juce::AudioFormatReaderSource>(reader, true);
    previewTransport.setSource(previewSource.get(), 0, nullptr,
                               reader->sampleRate, reader->numChannels);
    previewTransport.setPosition(0.0);
    previewTransport.start();
}

void PluginProcessor::stopSamplePreview()
{
    previewTransport.stop();
    previewTransport.setSource(nullptr);
    previewSource.reset();
}

//==============================================================================
// Hot-swap: stage a rhythm preset for atomic commit at the next loop boundary.
// or a MIDI program change was queued.
void PluginProcessor::handleAsyncUpdate()
{
    const int n = numActiveRhythms.load(std::memory_order_acquire);
    for (int r = 0; r < n; ++r)
    {
        auto& sw = pendingSwaps[r];
        if (!sw.boundaryReached.load(std::memory_order_acquire)) continue;
        // isReady may have been cleared by cancelStagedSwap between the audio thread
        // setting boundaryReached and this handler running — skip if so.
        if (!sw.isReady.load(std::memory_order_relaxed))
        {
            sw.boundaryReached.store(false, std::memory_order_relaxed);
            continue;
        }

        suspendProcessing(true);
        voiceEngines[r] = std::move(sw.pendingVoice);
        sequencer.getRhythm(r) = sw.pendingRhythm;
        loadedSamplePaths.set(r, sw.pendingSamplePath);
        sequencer.updatePattern(r);
        sw.isReady.store(false, std::memory_order_relaxed);
        sw.boundaryReached.store(false, std::memory_order_relaxed);
        suspendProcessing(false);

        apvtsLoading = true;
        pushRhythmToAPVTS(r);
        apvtsLoading = false;
    }

    // Drain MIDI program-change queue. Each event stages a rhythm preset; the existing
    // stageRhythmPreset path handles loop-boundary timing or applies immediately if stopped.
    {
        const int ready = pcFifo.getNumReady();
        if (ready > 0)
        {
            int start1, size1, start2, size2;
            pcFifo.prepareToRead(ready, start1, size1, start2, size2);
            const int activeRhythms = numActiveRhythms.load(std::memory_order_acquire);
            auto handle = [this, activeRhythms](const ProgramChangeEvent& ev)
            {
                if (ev.slot < 0 || ev.slot >= activeRhythms) return;
                if (! midiPresetMap.hasPreset(ev.presetIndex))   return;
                const juce::File f { midiPresetMap.getPresetPath(ev.presetIndex) };
                if (f.existsAsFile())
                    stageRhythmPreset(ev.slot, f);
            };
            for (int i = 0; i < size1; ++i) handle(pcQueue[(size_t)(start1 + i)]);
            for (int i = 0; i < size2; ++i) handle(pcQueue[(size_t)(start2 + i)]);
            pcFifo.finishedRead(ready);
        }
    }
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
