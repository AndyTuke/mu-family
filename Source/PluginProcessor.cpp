#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Sequencer/Rhythm.h"
#include "FX/FXAlgorithmDef.h"

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout PluginProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    auto addF = [&](const juce::String& id, const juce::String& name, float mn, float mx, float def)
    {
        layout.add(std::make_unique<juce::AudioParameterFloat>(
            id, name, juce::NormalisableRange<float>(mn, mx), def));
    };
    auto addB = [&](const juce::String& id, const juce::String& name, bool def)
    {
        layout.add(std::make_unique<juce::AudioParameterBool>(id, name, def));
    };
    auto addI = [&](const juce::String& id, const juce::String& name, int mn, int mx, int def)
    {
        layout.add(std::make_unique<juce::AudioParameterInt>(id, name, mn, mx, def));
    };

    // ── Per-rhythm parameters (47 × 8 = 376) ─────────────────────────────────
    for (int i = 0; i < SequencerEngine::MaxRhythms; ++i)
    {
        const juce::String p = "r" + juce::String(i) + "_";
        const juce::String n = "R" + juce::String(i + 1) + " ";

        // HitGen A
        addI(p+"stepsA",   n+"Steps A",   1, 64, 8);
        addI(p+"hitsA",    n+"Hits A",    0, 64, 0);
        addI(p+"rotA",     n+"Rot A",   -32, 32, 0);
        addI(p+"prePadA",  n+"PrePad A",  0, 12, 0);
        addI(p+"postPadA", n+"PostPad A", 0, 12, 0);
        addI(p+"insStA",   n+"InsSt A",   0, 63, 0);
        addI(p+"insLenA",  n+"InsLen A",  0,  8, 0);
        addB(p+"insModeA",    n+"InsMode A",     false);
        addB(p+"prePadModeA", n+"PrePadMode A",  false);
        addB(p+"postPadModeA",n+"PostPadMode A", false);
        // HitGen B
        addI(p+"stepsB",   n+"Steps B",   1, 64, 8);
        addI(p+"hitsB",    n+"Hits B",    0, 64, 0);
        addI(p+"rotB",     n+"Rot B",   -32, 32, 0);
        addI(p+"prePadB",  n+"PrePad B",  0, 12, 0);
        addI(p+"postPadB", n+"PostPad B", 0, 12, 0);
        addI(p+"insStB",   n+"InsSt B",   0, 63, 0);
        addI(p+"insLenB",  n+"InsLen B",  0,  8, 0);
        addB(p+"insModeB",    n+"InsMode B",     false);
        addB(p+"prePadModeB", n+"PrePadMode B",  false);
        addB(p+"postPadModeB",n+"PostPadMode B", false);
        // HitGen C
        addI(p+"stepsC",   n+"Steps C",   1, 64, 8);
        addI(p+"hitsC",    n+"Hits C",    0, 64, 0);
        addI(p+"rotC",     n+"Rot C",   -32, 32, 0);
        addI(p+"prePadC",  n+"PrePad C",  0, 12, 0);
        addI(p+"postPadC", n+"PostPad C", 0, 12, 0);
        addI(p+"insStC",   n+"InsSt C",   0, 63, 0);
        addI(p+"insLenC",  n+"InsLen C",  0,  8, 0);
        addB(p+"insModeC",    n+"InsMode C",     false);
        addB(p+"prePadModeC", n+"PrePadMode C",  false);
        addB(p+"postPadModeC",n+"PostPadMode C", false);
        // Logic
        addI(p+"logic", n+"Logic", 0, 4, 0);
        // Pitch
        addI(p+"pitchOct",  n+"Pitch Oct",  -4,   4,  0);
        addI(p+"pitchSemi", n+"Pitch Semi", -12,  12,  0);
        addF(p+"pitchFine", n+"Pitch Fine", -100.0f, 100.0f, 0.0f);
        addF(p+"pEnvAtk",   n+"P Env Atk",  0.0f, 100.0f,  0.0f);
        addF(p+"pEnvDec",   n+"P Env Dec",  0.0f, 100.0f,  1.0f);
        addF(p+"pEnvSus",   n+"P Env Sus",  0.0f, 100.0f,  0.0f);
        addF(p+"pEnvRel",   n+"P Env Rel",  0.0f, 100.0f,  1.0f);
        addF(p+"pEnvDep",   n+"P Env Dep",  0.0f,  24.0f,  0.0f);
        // Filter
        addI(p+"fltType", n+"Filter Type", 0, 9, 0);  // up to 10 types for future expansion
        addF(p+"fltCut",  n+"Filter Cut",  20.0f, 20000.0f, 8000.0f);
        addF(p+"fltRes",  n+"Filter Res",   0.0f,    0.99f,    0.2f);
        addF(p+"fEnvAtk", n+"F Env Atk",  0.0f, 100.0f,  1.0f);
        addF(p+"fEnvDec", n+"F Env Dec",  0.0f, 100.0f,  3.0f);
        addF(p+"fEnvSus", n+"F Env Sus",  0.0f, 100.0f,  0.0f);
        addF(p+"fEnvRel", n+"F Env Rel",  0.0f, 100.0f,  3.0f);
        addF(p+"fEnvDep", n+"F Env Dep",  0.0f,  48.0f,  0.0f);
        // Amp
        addF(p+"ampLvl",  n+"Amp Level",  0.0f,   2.0f,  0.5f);  // Stage 19: −6 dB default
        addF(p+"aEnvAtk", n+"A Env Atk",  0.0f, 100.0f,  0.0f);
        addF(p+"aEnvDec", n+"A Env Dec",  0.0f, 100.0f,  3.0f);
        addF(p+"aEnvSus", n+"A Env Sus",  0.0f, 100.0f, 80.0f);
        addF(p+"aEnvRel",   n+"A Env Rel",  0.0f, 100.0f,  5.0f);
        addF(p+"accentDb",  n+"Accent",     0.0f,  12.0f,  0.0f);
        // Drive
        addI(p+"drvChar", n+"Drive Char",   0,      4,      0);  // 0=None,1=Soft,2=Hard,3=Fold,4=Bitcrusher
        addF(p+"drvDrv",  n+"Drive",        0.0f, 100.0f,    0.0f);  // Soft/Hard/Fold drive amount
        addF(p+"drvOut",  n+"Drive Out",  -24.0f,    0.0f,   0.0f);  // Soft/Hard/Fold output level
        addF(p+"drvBits", n+"Bits",         1.0f,  16.0f,   16.0f);  // Bitcrusher bit depth
        addF(p+"drvRate", n+"Drive Rate",  100.0f, 48000.0f, 48000.0f);  // Bitcrusher sample rate
        addF(p+"drvDit",  n+"Dither",       0.0f, 100.0f,    0.0f);  // Bitcrusher dither amount
        addF(p+"drvTon",  n+"Drive Tone",  20.0f, 20000.0f, 20000.0f);  // Shared LPF
        // Misc
        addB(p+"midiMode", n+"MIDI Mode", false);
    }

    // ── Effect slot (8 params) ────────────────────────────────────────────────
    addI("eff_algo", "Effect Algorithm", 0, 7, 0);
    addB("eff_en",   "Effect Enable", true);
    addF("eff_send", "Effect Send",  0.0f, 1.0f, 1.0f);
    // Generic normalized (0–1) slots for algorithm-specific params.
    // Actual value = paramDef.minVal + stored * (paramDef.maxVal - paramDef.minVal).
    addF("eff_p0", "Effect P0", 0.0f, 1.0f, 0.5f);
    addF("eff_p1", "Effect P1", 0.0f, 1.0f, 0.5f);
    addF("eff_p2", "Effect P2", 0.0f, 1.0f, 0.5f);
    addF("eff_p3", "Effect P3", 0.0f, 1.0f, 0.5f);
    addF("eff_p4", "Effect P4", 0.0f, 1.0f, 0.5f);

    // ── Delay slot (11 params) ────────────────────────────────────────────────
    addB("dly_en",        "Delay Enable",    true);
    addB("dly_mode",      "Delay Sync",      false);
    addF("dly_ms",        "Delay Time (ms)", 1.0f, 4000.0f, 250.0f);
    addI("dly_syncDenom", "Delay Denom",     0, 3, 3);   // 0=1/32, 1=1/16, 2=1/8, 3=1/4
    addB("dly_syncDot",   "Delay Dotted",    false);
    addB("dly_syncTrip",  "Delay Triplet",   false);
    addI("dly_count",     "Delay Count",     1, 8, 1);
    addF("dly_fb",        "Delay Feedback",  0.0f,  0.98f, 0.45f);
    addF("dly_spread",    "Delay Spread",    0.0f,  1.0f,  0.0f);
    addF("dly_dirt",      "Delay Dirt",      0.0f,  1.0f,  0.0f);
    addF("dly_send",      "Delay Send",      0.0f,  1.0f,  1.0f);

    // ── Reverb slot (9 params) ────────────────────────────────────────────────
    addI("rev_algo", "Reverb Algorithm",  0, 3,  0);
    addB("rev_en",   "Reverb Enable",     true);
    addF("rev_lvl",  "Reverb Level",  0.0f,   1.0f,  1.0f);
    addF("rev_size", "Reverb Size",   0.0f,   1.0f,  0.5f);
    addF("rev_pre",  "Reverb Pre-Delay", 0.0f, 100.0f, 10.0f);
    addF("rev_diff", "Reverb Diffusion", 0.0f,   1.0f,  0.7f);
    addF("rev_damp", "Reverb Damp",   0.0f,   1.0f,  0.4f);
    addF("rev_mod",  "Reverb Mod",    0.0f,   1.0f,  0.2f);
    addF("rev_dirt", "Reverb Dirt",   0.0f,   1.0f,  0.0f);

    // ── Intra-FX routing (3 params) ───────────────────────────────────────────
    addF("eff2dly", "Effect→Delay",  0.0f, 1.0f, 0.0f);
    addF("eff2rev", "Effect→Reverb", 0.0f, 1.0f, 0.0f);
    addF("dly2rev", "Delay→Reverb",  0.0f, 1.0f, 0.0f);

    // ── Echo (embedded in EFX slot when algo=Echo) ────────────────────────────
    addB("echo_en",        "Echo Enable",      true);
    addF("echo_mode",      "Echo Mode",        0.0f, 1.0f, 0.0f);
    addF("echo_ms",        "Echo Time Ms",     1.0f, 4000.0f, 250.0f);
    addI("echo_syncDenom", "Echo Sync Denom",  0, 3, 2);
    addB("echo_syncDot",   "Echo Dotted",      false);
    addB("echo_syncTrip",  "Echo Triplet",     false);
    addI("echo_count",     "Echo Count",       1, 8, 1);
    addF("echo_fb",        "Echo Feedback",    0.0f, 1.0f, 0.45f);
    addF("echo_spread",    "Echo Spread",      0.0f, 1.0f, 0.0f);
    addF("echo_dirt",      "Echo Dirt",        0.0f, 1.0f, 0.0f);

    // ── Rhythm channel strips (7 × 8 = 56) ───────────────────────────────────
    for (int i = 0; i < SequencerEngine::MaxRhythms; ++i)
    {
        const juce::String c = "ch" + juce::String(i) + "_";
        const juce::String n = "Ch" + juce::String(i + 1) + " ";
        addF(c+"lvl",     n+"Level",      0.0f, 1.0f,  0.5f);
        addF(c+"pan",     n+"Pan",       -1.0f, 1.0f,  0.0f);
        addB(c+"mute",    n+"Mute",      false);
        addB(c+"solo",    n+"Solo",      false);
        addF(c+"sendEff", n+"Send Eff",  0.0f, 1.0f,  0.0f);
        addF(c+"sendDly", n+"Send Dly",  0.0f, 1.0f,  0.0f);
        addF(c+"sendRev", n+"Send Rev",  0.0f, 1.0f,  0.0f);
    }

    // ── Return channel strips (4 × 3 = 12) ───────────────────────────────────
    for (const char* ret : { "eff", "dly", "rev" })
    {
        const juce::String q = juce::String("ret_") + ret + "_";
        const juce::String nm = juce::String("Ret ") + ret + " ";
        addF(q+"lvl",  nm+"Level",  0.0f, 1.0f, 0.75f);
        addF(q+"pan",  nm+"Pan",   -1.0f, 1.0f, 0.0f);
        addB(q+"mute", nm+"Mute",  false);
        addB(q+"solo", nm+"Solo",  false);
    }

    // ── Master (2 params) ─────────────────────────────────────────────────────
    addF("mstr_lvl",  "Master Level", 0.0f, 1.0f,  0.75f);
    addF("mstr_pan",  "Master Pan",  -1.0f, 1.0f,  0.0f);
    addI("mstrLoop",  "Master Loop",  0, 16, 0);   // 0=free, 1-16 → 16-256 steps

    return layout;
}

//==============================================================================
PluginProcessor::PluginProcessor()
    : AudioProcessor(BusesProperties()
          .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "MuClidState", createParameterLayout())
{
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
    modParamValues.reserve(20);
    for (const char* key : { "amp.attack", "amp.decay", "amp.sustain", "amp.release",
                              "filter.cutoff", "filter.resonance",
                              "fenv.attack", "fenv.decay", "fenv.depth",
                              "pitch.semitones",
                              "insert.drive", "insert.output",
                              "insert.bits", "insert.rate", "insert.dither", "insert.lpf" })
        modParamValues[key] = 0.0f;

    // Add default rhythm (16 steps, 4 hits) and sync its state to APVTS.
    Rhythm defaultRhythm;
    defaultRhythm.name       = "<unnamed>";
    defaultRhythm.genA.steps = 16;
    defaultRhythm.genA.hits  = 4;
    sequencer.addRhythm(defaultRhythm);
    voiceEngines[0] = std::make_unique<VoiceEngine>();
    numActiveRhythms.store(1, std::memory_order_release);

    apvtsLoading = true;
    pushRhythmToAPVTS(0);
    apvtsLoading = false;

    // Ensure user content folders exist and load the default preset if present.
    ensureContentFoldersExist();
    loadDefaultPreset();
}

PluginProcessor::~PluginProcessor()
{
    cancelPendingUpdate();
    for (auto* param : getParameters())
        if (auto* p = dynamic_cast<juce::AudioProcessorParameterWithID*>(param))
            apvts.removeParameterListener(p->getParameterID(), this);
}

//==============================================================================
const juce::String PluginProcessor::getName() const { return "mu-Clid"; }
bool PluginProcessor::acceptsMidi() const { return false; }
double PluginProcessor::getTailLengthSeconds() const { return 0.0; }

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
        voiceEngines[i]->prepareToPlay(sampleRate, samplesPerBlock);
        midiEngines[i].prepare(sampleRate, samplesPerBlock);
    }
    fxChain.prepare(sampleRate, samplesPerBlock);
    mixerEngine.prepare(sampleRate, samplesPerBlock);
}

void PluginProcessor::releaseResources() {}

void PluginProcessor::processBlock(juce::AudioBuffer<float>& buffer,
                                   juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();

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

    if (!playing && internalPlaying)
    {
        playing  = true;
        beatPos  = internalBeatPos;
        internalBeatPos += (buffer.getNumSamples() / currentSampleRate) * (internalBpm / 60.0);
    }

    const int numRhythms = numActiveRhythms.load(std::memory_order_acquire);

    sequencerPlaying.set(playing);
    lastBeatPos.set(beatPos);

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

        if (!rhythm.modulationMatrix.getAssignments().empty())
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
                modParamValues["pitch.semitones"]  = 0.0f;
                modParamValues["insert.drive"]     = modParams.driveDrive;
                modParamValues["insert.output"]    = modParams.driveOutput;
                modParamValues["insert.bits"]      = modParams.drvBits;
                modParamValues["insert.rate"]      = (modParams.driveRate - 100.0f) / (48000.0f - 100.0f) * 100.0f;
                modParamValues["insert.dither"]    = modParams.drvDither;
                modParamValues["insert.lpf"]       = modParams.driveTone;

                rhythm.modulationMatrix.process(rhythm.controlSequences, beatPos, modParamValues);

                rhythm.modLock.store(false, std::memory_order_release);

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
                modParams.pitchMod       = juce::jlimit(-24.0f, 24.0f, modParamValues["pitch.semitones"]);
                modParams.driveDrive     = juce::jlimit(0.0f,  100.0f,    modParamValues["insert.drive"]);
                modParams.driveOutput    = juce::jlimit(-24.0f,  0.0f,    modParamValues["insert.output"]);
                modParams.drvBits        = juce::jlimit(1.0f,   16.0f,    modParamValues["insert.bits"]);
                modParams.driveRate      = 100.0f + juce::jlimit(0.0f, 100.0f, modParamValues["insert.rate"]) / 100.0f * (48000.0f - 100.0f);
                modParams.drvDither      = juce::jlimit(0.0f,  100.0f,    modParamValues["insert.dither"]);
                modParams.driveTone      = juce::jlimit(20.0f, 20000.0f,  modParamValues["insert.lpf"]);
            }
        }

        if (voiceEngines[r])
            voiceEngines[r]->setActiveParams(modParams);
    }

    fxChain.setHostBpm(internalBpm);
    mixerEngine.processBlock(buffer, numRhythms,
                             voiceEngines.data(), fxChain, buffer.getNumSamples());

    for (int r = 0; r < numRhythms; ++r)
        midiEngines[r].processBlock(midiMessages, buffer.getNumSamples());
}

//==============================================================================
bool PluginProcessor::hasEditor() const { return true; }

juce::AudioProcessorEditor* PluginProcessor::createEditor()
{
    return new PluginEditor(*this);
}

//==============================================================================
// APVTS listener — routes every parameter change to the correct engine.
void PluginProcessor::parameterChanged(const juce::String& id, float v)
{
    // Rhythm params: r{0-7}_{suffix}
    if (id.length() >= 4 && id[0] == 'r' && id[1] >= '0' && id[1] <= '7' && id[2] == '_')
    {
        syncRhythmParam(id[1] - '0', id.substring(3), v);
        return;
    }
    // FX params
    if (id.startsWith("eff_") || id.startsWith("rev_") || id.startsWith("dly_") ||
        id.startsWith("eff2") || id.startsWith("dly2") || id.startsWith("echo_"))
    {
        syncFXParam(id, v);
        return;
    }
    // Mixer params
    if (id.startsWith("ch") || id.startsWith("ret_") || id.startsWith("mstr_"))
    {
        syncMixerParam(id, v);
        return;
    }
    // Master loop length
    if (id == "mstrLoop")
    {
        sequencer.setMasterLoopSteps((int)v * 16);
        return;
    }
}

// Convert 0–100 UI scale → seconds for ADSR A/D/R knobs.
// 0 → 0.001 s (1 ms), 100 → 3 s, linear.
static inline float adsrTime(float v) { return juce::jmax(0.001f, v * 0.03f); }
// Convert 0–100 UI scale → 0–1 amplitude for ADSR Sustain.
static inline float adsrSus(float v)  { return juce::jlimit(0.0f, 1.0f, v / 100.0f); }

//==============================================================================
// All per-rhythm APVTS parameter suffixes (used for rhythm preset save/load).
static const char* const kRhythmSuffixes[] = {
    "stepsA","hitsA","rotA","prePadA","postPadA","insStA","insLenA","insModeA","prePadModeA","postPadModeA",
    "stepsB","hitsB","rotB","prePadB","postPadB","insStB","insLenB","insModeB","prePadModeB","postPadModeB",
    "stepsC","hitsC","rotC","prePadC","postPadC","insStC","insLenC","insModeC","prePadModeC","postPadModeC",
    "logic",
    "pitchOct","pitchSemi","pitchFine","pEnvAtk","pEnvDec","pEnvSus","pEnvRel","pEnvDep",
    "fltType","fltCut","fltRes","fEnvAtk","fEnvDec","fEnvSus","fEnvRel","fEnvDep",
    "ampLvl","aEnvAtk","aEnvDec","aEnvSus","aEnvRel","accentDb",
    "drvChar","drvDrv","drvOut","drvBits","drvRate","drvDit","drvTon",
    "midiMode",
    nullptr
};

// Applies a rhythm parameter suffix + display-scale value to a Rhythm struct.
// Returns patternDirty (true) or voiceDirty (false) via the out-params.
// Called from syncRhythmParam and stageRhythmPreset.
static void applyRhythmSuffix(const juce::String& suffix, float v, Rhythm& r,
                               bool& patternDirty, bool& voiceDirty)
{
    auto applyHitGen = [&](HitGenerator& gen, const juce::String& s, float val)
    {
        if      (s == "steps")       { gen.steps        = juce::jlimit(1, 64, (int)val); patternDirty = true; }
        else if (s == "hits")        { gen.hits          = juce::jlimit(0, 64, (int)val); patternDirty = true; }
        else if (s == "rot")         { gen.rotate        = (int)val;                      patternDirty = true; }
        else if (s == "prePad")      { gen.prePad        = juce::jlimit(0, 12, (int)val); patternDirty = true; }
        else if (s == "postPad")     { gen.postPad       = juce::jlimit(0, 12, (int)val); patternDirty = true; }
        else if (s == "insSt")       { gen.insertStart   = juce::jlimit(0, 63, (int)val); patternDirty = true; }
        else if (s == "insLen")      { gen.insertLength  = juce::jlimit(0,  8, (int)val); patternDirty = true; }
        else if (s == "insMode")     { gen.insertMode   = val > 0.5f ? InsertMode::Mute : InsertMode::Pad; patternDirty = true; }
        else if (s == "prePadMode")  { gen.prePadMode   = val > 0.5f ? InsertMode::Mute : InsertMode::Pad; patternDirty = true; }
        else if (s == "postPadMode") { gen.postPadMode  = val > 0.5f ? InsertMode::Mute : InsertMode::Pad; patternDirty = true; }
    };

    if      (suffix.endsWith("A")) applyHitGen(r.genA, suffix.dropLastCharacters(1), v);
    else if (suffix.endsWith("B")) applyHitGen(r.genB, suffix.dropLastCharacters(1), v);
    else if (suffix.endsWith("C")) applyHitGen(r.genC, suffix.dropLastCharacters(1), v);
    else if (suffix == "logic")     { r.logic = static_cast<Logic>(juce::jlimit(0, 4, (int)v)); patternDirty = true; }
    else if (suffix == "pitchOct")  { r.voiceParams.pitchOctave    = juce::jlimit(-4, 4, (int)v);   voiceDirty = true; }
    else if (suffix == "pitchSemi") { r.voiceParams.pitchSemitones = juce::jlimit(-12, 12, (int)v); voiceDirty = true; }
    else if (suffix == "pitchFine") { r.voiceParams.pitchFine      = v;  voiceDirty = true; }
    else if (suffix == "pEnvAtk")   { r.voiceParams.pitchEnvAtk    = adsrTime(v); voiceDirty = true; }
    else if (suffix == "pEnvDec")   { r.voiceParams.pitchEnvDec    = adsrTime(v); voiceDirty = true; }
    else if (suffix == "pEnvSus")   { r.voiceParams.pitchEnvSus    = adsrSus(v);  voiceDirty = true; }
    else if (suffix == "pEnvRel")   { r.voiceParams.pitchEnvRel    = adsrTime(v); voiceDirty = true; }
    else if (suffix == "pEnvDep")   { r.voiceParams.pitchEnvDepth  = v;            voiceDirty = true; }
    else if (suffix == "fltType")   { r.voiceParams.filterType     = juce::jlimit(0, 9, (int)v); voiceDirty = true; }
    else if (suffix == "fltCut")    { r.voiceParams.filterCutoff   = v;            voiceDirty = true; }
    else if (suffix == "fltRes")    { r.voiceParams.filterRes      = v;            voiceDirty = true; }
    else if (suffix == "fEnvAtk")   { r.voiceParams.filterEnvAtk   = adsrTime(v); voiceDirty = true; }
    else if (suffix == "fEnvDec")   { r.voiceParams.filterEnvDec   = adsrTime(v); voiceDirty = true; }
    else if (suffix == "fEnvSus")   { r.voiceParams.filterEnvSus   = adsrSus(v);  voiceDirty = true; }
    else if (suffix == "fEnvRel")   { r.voiceParams.filterEnvRel   = adsrTime(v); voiceDirty = true; }
    else if (suffix == "fEnvDep")   { r.voiceParams.filterEnvDepth = v;            voiceDirty = true; }
    else if (suffix == "ampLvl")    { r.voiceParams.ampLevel       = v;            voiceDirty = true; }
    else if (suffix == "aEnvAtk")   { r.voiceParams.ampEnvAtk      = adsrTime(v); voiceDirty = true; }
    else if (suffix == "aEnvDec")   { r.voiceParams.ampEnvDec      = adsrTime(v); voiceDirty = true; }
    else if (suffix == "aEnvSus")   { r.voiceParams.ampEnvSus      = adsrSus(v);  voiceDirty = true; }
    else if (suffix == "aEnvRel")   { r.voiceParams.ampEnvRel = adsrTime(v); r.voiceParams.ampRelToEnd = (v >= 100.0f); voiceDirty = true; }
    else if (suffix == "accentDb")  { r.voiceParams.accentDb        = v;           voiceDirty = true; }
    else if (suffix == "drvChar")   { r.voiceParams.driveChar  = juce::jlimit(0, 4, (int)v); voiceDirty = true; }
    else if (suffix == "drvDrv")    { r.voiceParams.driveDrive = v;  voiceDirty = true; }
    else if (suffix == "drvOut")    { r.voiceParams.driveOutput= v;  voiceDirty = true; }
    else if (suffix == "drvBits")   { r.voiceParams.drvBits    = v;  voiceDirty = true; }
    else if (suffix == "drvRate")   { r.voiceParams.driveRate  = v;  voiceDirty = true; }
    else if (suffix == "drvDit")    { r.voiceParams.drvDither  = v;  voiceDirty = true; }
    else if (suffix == "drvTon")    { r.voiceParams.driveTone  = v;  voiceDirty = true; }
    else if (suffix == "midiMode")  { r.midiMode = v > 0.5f; }
}

void PluginProcessor::syncRhythmParam(int ri, const juce::String& suffix, float v)
{
    if (ri < 0 || ri >= SequencerEngine::MaxRhythms) return;
    if (ri >= sequencer.getNumRhythms()) return;

    Rhythm& r = sequencer.getRhythm(ri);
    bool patternDirty = false;
    bool voiceDirty   = false;
    applyRhythmSuffix(suffix, v, r, patternDirty, voiceDirty);

    if (!apvtsLoading)
    {
        if (patternDirty) sequencer.updatePattern(ri);
        if (voiceDirty)   voiceEngines[ri]->setParams(r.voiceParams);
    }
}

void PluginProcessor::syncFXParam(const juce::String& id, float v)
{
    auto& eff = fxChain.effectSlot();
    auto& dly = fxChain.delaySlot();
    auto& rev = fxChain.reverbSlot();

    if      (id == "eff_algo") { if (!apvtsLoading) eff.setAlgorithm((int)v); }
    else if (id == "eff_en")   { eff.setEnabled(v > 0.5f); }
    else if (id.startsWith("eff_p"))
    {
        int idx = id.substring(5).getIntValue();
        const auto& algos = FXAlgorithmRegistry::effectAlgorithms();
        int ai = eff.getAlgorithmIndex();
        if (ai < (int)algos.size() && idx < (int)algos[ai].params.size())
        {
            const auto& pd = algos[ai].params[idx];
            eff.setParam(pd.id, pd.minVal + v * (pd.maxVal - pd.minVal));
        }
    }
    else if (id == "dly_en")   { dly.setEnabled(v > 0.5f); }
    else if (id == "dly_mode") { dly.setTimeMode(v > 0.5f ? DelaySlot::TimeMode::Sync : DelaySlot::TimeMode::Free); }
    else if (id == "dly_ms")   { dly.setDelayMs(v); }
    else if (id == "dly_syncDenom" || id == "dly_syncDot" || id == "dly_syncTrip")
    {
        static const int denoms[] = { 32, 16, 8, 4 };
        int idx  = juce::jlimit(0, 3, (int)*apvts.getRawParameterValue("dly_syncDenom"));
        bool dot = *apvts.getRawParameterValue("dly_syncDot")  > 0.5f;
        bool tri = *apvts.getRawParameterValue("dly_syncTrip") > 0.5f;
        dly.setTimeDivision(denoms[idx], dot, tri);
    }
    else if (id == "dly_count")  { dly.setTimeCount(juce::jmax(1, (int)v)); }
    else if (id == "dly_fb")     { dly.setFeedback(v); }
    else if (id == "dly_spread") { dly.setSpread(v); }
    else if (id == "dly_dirt")   { dly.setDirt(v); }
    else if (id == "dly_send")   { dly.setSend(v); }
    else if (id == "rev_algo")   { if (!apvtsLoading) rev.setAlgorithm((int)v); }
    else if (id == "rev_en")     { rev.setEnabled(v > 0.5f); }
    else if (id == "rev_lvl")    { rev.setLevel(v); }
    else if (id == "rev_size")   { rev.setParam("size",      v); }
    else if (id == "rev_pre")    { rev.setParam("predelay",  v); }
    else if (id == "rev_diff")   { rev.setParam("diffusion", v); }
    else if (id == "rev_damp")   { rev.setParam("damp",      v); }
    else if (id == "rev_mod")    { rev.setParam("mod",       v); }
    else if (id == "rev_dirt")   { rev.setParam("dirt",      v); }
    else if (id == "eff2dly")    { fxChain.setEffectToDelaySend(v); }
    else if (id == "eff2rev")    { fxChain.setEffectToReverbSend(v); }
    else if (id == "dly2rev")    { fxChain.setDelayToReverbSend(v); }
    else if (id == "echo_en")    { eff.getEchoDelay().setEnabled(v > 0.5f); }
    else if (id == "echo_mode")  { eff.getEchoDelay().setTimeMode(v > 0.5f ? DelaySlot::TimeMode::Sync : DelaySlot::TimeMode::Free); }
    else if (id == "echo_ms")    { eff.getEchoDelay().setDelayMs(v); }
    else if (id == "echo_syncDenom" || id == "echo_syncDot" || id == "echo_syncTrip")
    {
        static const int denoms[] = { 32, 16, 8, 4 };
        int  idx = juce::jlimit(0, 3, (int)*apvts.getRawParameterValue("echo_syncDenom"));
        bool dot = *apvts.getRawParameterValue("echo_syncDot")  > 0.5f;
        bool tri = *apvts.getRawParameterValue("echo_syncTrip") > 0.5f;
        eff.getEchoDelay().setTimeDivision(denoms[idx], dot, tri);
    }
    else if (id == "echo_count")  { eff.getEchoDelay().setTimeCount(juce::jmax(1, (int)v)); }
    else if (id == "echo_fb")     { eff.getEchoDelay().setFeedback(v); }
    else if (id == "echo_spread") { eff.getEchoDelay().setSpread(v); }
    else if (id == "echo_dirt")   { eff.getEchoDelay().setDirt(v); }
}

void PluginProcessor::syncMixerParam(const juce::String& id, float v)
{
    // ch{0-7}_{param}
    if (id.length() >= 6 && id[0] == 'c' && id[1] == 'h' && id[3] == '_')
    {
        int i = id[2] - '0';
        if (i >= 0 && i < SequencerEngine::MaxRhythms)
        {
            auto& ch = mixerEngine.channels[i];
            const juce::String param = id.substring(4);
            if      (param == "lvl")     ch.level      = v;
            else if (param == "pan")     ch.pan        = v;
            else if (param == "mute")    ch.mute       = v > 0.5f;
            else if (param == "solo")    ch.solo       = v > 0.5f;
            else if (param == "sendEff") ch.sendEffect = v;
            else if (param == "sendDly") ch.sendDelay  = v;
            else if (param == "sendRev") ch.sendReverb = v;
        }
        return;
    }

    // ret_{eff|dly|rev}_{param}
    if (id.startsWith("ret_"))
    {
        int retIdx = -1;
        juce::String rest;
        if      (id.startsWith("ret_eff_")) { retIdx = 0; rest = id.substring(8); }
        else if (id.startsWith("ret_dly_")) { retIdx = 1; rest = id.substring(8); }
        else if (id.startsWith("ret_rev_")) { retIdx = 2; rest = id.substring(8); }

        if (retIdx >= 0)
        {
            auto& ret = mixerEngine.returns[retIdx];
            if      (rest == "lvl")  ret.level = v;
            else if (rest == "pan")  ret.pan   = v;
            else if (rest == "mute") ret.mute  = v > 0.5f;
            else if (rest == "solo") ret.solo  = v > 0.5f;
        }
        return;
    }

    if      (id == "mstr_lvl") mixerEngine.masterLevel = v;
    else if (id == "mstr_pan") mixerEngine.masterPan   = v;
}

//==============================================================================
void PluginProcessor::pushRhythmToAPVTS(int ri)
{
    if (ri < 0 || ri >= sequencer.getNumRhythms()) return;
    const Rhythm& r = sequencer.getRhythm(ri);
    const juce::String px = "r" + juce::String(ri) + "_";

    auto set = [this](const juce::String& id, float v)
    {
        if (auto* p = apvts.getParameter(id))
            p->setValueNotifyingHost(p->convertTo0to1(v));
    };

    auto& A = r.genA;
    set(px+"stepsA",   (float)A.steps);
    set(px+"hitsA",    (float)A.hits);
    set(px+"rotA",     (float)A.rotate);
    set(px+"prePadA",  (float)A.prePad);
    set(px+"postPadA", (float)A.postPad);
    set(px+"insStA",   (float)A.insertStart);
    set(px+"insLenA",  (float)A.insertLength);
    set(px+"insModeA",    A.insertMode  == InsertMode::Mute ? 1.0f : 0.0f);
    set(px+"prePadModeA", A.prePadMode  == InsertMode::Mute ? 1.0f : 0.0f);
    set(px+"postPadModeA",A.postPadMode == InsertMode::Mute ? 1.0f : 0.0f);

    auto& B = r.genB;
    set(px+"stepsB",   (float)B.steps);
    set(px+"hitsB",    (float)B.hits);
    set(px+"rotB",     (float)B.rotate);
    set(px+"prePadB",  (float)B.prePad);
    set(px+"postPadB", (float)B.postPad);
    set(px+"insStB",   (float)B.insertStart);
    set(px+"insLenB",  (float)B.insertLength);
    set(px+"insModeB",    B.insertMode  == InsertMode::Mute ? 1.0f : 0.0f);
    set(px+"prePadModeB", B.prePadMode  == InsertMode::Mute ? 1.0f : 0.0f);
    set(px+"postPadModeB",B.postPadMode == InsertMode::Mute ? 1.0f : 0.0f);

    auto& C = r.genC;
    set(px+"stepsC",   (float)C.steps);
    set(px+"hitsC",    (float)C.hits);
    set(px+"rotC",     (float)C.rotate);
    set(px+"prePadC",  (float)C.prePad);
    set(px+"postPadC", (float)C.postPad);
    set(px+"insStC",   (float)C.insertStart);
    set(px+"insLenC",  (float)C.insertLength);
    set(px+"insModeC",    C.insertMode  == InsertMode::Mute ? 1.0f : 0.0f);
    set(px+"prePadModeC", C.prePadMode  == InsertMode::Mute ? 1.0f : 0.0f);
    set(px+"postPadModeC",C.postPadMode == InsertMode::Mute ? 1.0f : 0.0f);

    set(px+"logic", (float)r.logic);

    const auto& vp = r.voiceParams;
    set(px+"pitchOct",  (float)vp.pitchOctave);
    set(px+"pitchSemi", (float)vp.pitchSemitones);
    set(px+"pitchFine", vp.pitchFine);
    // ADSR stored in APVTS as 0–100: A/D/R * (100/3) (seconds→display), S * 100.
    set(px+"pEnvAtk",   vp.pitchEnvAtk  * (100.0f/3.0f));
    set(px+"pEnvDec",   vp.pitchEnvDec  * (100.0f/3.0f));
    set(px+"pEnvSus",   vp.pitchEnvSus  * 100.0f);
    set(px+"pEnvRel",   vp.pitchEnvRel  * (100.0f/3.0f));
    set(px+"pEnvDep",   vp.pitchEnvDepth);
    set(px+"fltType",   (float)vp.filterType);
    set(px+"fltCut",    vp.filterCutoff);
    set(px+"fltRes",    vp.filterRes);
    set(px+"fEnvAtk",   vp.filterEnvAtk  * (100.0f/3.0f));
    set(px+"fEnvDec",   vp.filterEnvDec  * (100.0f/3.0f));
    set(px+"fEnvSus",   vp.filterEnvSus  * 100.0f);
    set(px+"fEnvRel",   vp.filterEnvRel  * (100.0f/3.0f));
    set(px+"fEnvDep",   vp.filterEnvDepth);
    set(px+"ampLvl",    vp.ampLevel);
    set(px+"aEnvAtk",   vp.ampEnvAtk  * (100.0f/3.0f));
    set(px+"aEnvDec",   vp.ampEnvDec  * (100.0f/3.0f));
    set(px+"aEnvSus",   vp.ampEnvSus  * 100.0f);
    set(px+"aEnvRel",   vp.ampEnvRel  * (100.0f/3.0f));
    set(px+"accentDb",  vp.accentDb);
    set(px+"drvChar",   (float)vp.driveChar);
    set(px+"drvDrv",    vp.driveDrive);
    set(px+"drvOut",    vp.driveOutput);
    set(px+"drvBits",   vp.drvBits);
    set(px+"drvRate",   vp.driveRate);
    set(px+"drvDit",    vp.drvDither);
    set(px+"drvTon",    vp.driveTone);
    set(px+"midiMode",  r.midiMode ? 1.0f : 0.0f);
}

//==============================================================================
void PluginProcessor::addRhythm(const Rhythm& r)
{
    int ri = sequencer.getNumRhythms();
    if (ri >= SequencerEngine::MaxRhythms) return;
    sequencer.addRhythm(r);
    voiceEngines[ri] = std::make_unique<VoiceEngine>();
    voiceEngines[ri]->prepareToPlay(currentSampleRate, currentBlockSize);
    midiEngines[ri].prepare(currentSampleRate, currentBlockSize);
    numActiveRhythms.store(sequencer.getNumRhythms(), std::memory_order_release);
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

void PluginProcessor::pushMixerChannelToAPVTS(int idx)
{
    if (idx < 0 || idx >= SequencerEngine::MaxRhythms) return;
    const auto& ch = mixerEngine.channels[idx];
    const juce::String px = "ch" + juce::String(idx) + "_";

    auto set = [this](const juce::String& id, float v)
    {
        if (auto* p = apvts.getParameter(id))
            p->setValueNotifyingHost(p->convertTo0to1(v));
    };

    set(px+"lvl",     ch.level);
    set(px+"pan",     ch.pan);
    set(px+"mute",    ch.mute ? 1.0f : 0.0f);
    set(px+"solo",    ch.solo ? 1.0f : 0.0f);
    set(px+"sendEff", ch.sendEffect);
    set(px+"sendDly", ch.sendDelay);
    set(px+"sendRev", ch.sendReverb);
}

void PluginProcessor::swapAPVTSForRhythms(int i, int j)
{
    apvtsLoading = true;
    pushRhythmToAPVTS(i);
    pushRhythmToAPVTS(j);
    pushMixerChannelToAPVTS(i);
    pushMixerChannelToAPVTS(j);
    apvtsLoading = false;
}

bool PluginProcessor::swapRhythms(int i, int j)
{
    const int n = numActiveRhythms.load(std::memory_order_acquire);
    if (i < 0 || j < 0 || i >= n || j >= n || i == j) return false;

    suspendProcessing(true);

    sequencer.swapRhythmSlots(i, j);
    std::swap(voiceEngines[i], voiceEngines[j]);
    std::swap(midiEngines[i],  midiEngines[j]);
    resetPlayState(i);
    resetPlayState(j);

    juce::String tmp = loadedSamplePaths[i];
    loadedSamplePaths.set(i, loadedSamplePaths[j]);
    loadedSamplePaths.set(j, tmp);

    std::swap(mixerEngine.channels[i], mixerEngine.channels[j]);

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
    numActiveRhythms.store(newN, std::memory_order_release);
    sequencer.removeRhythm(index);
    for (int i = index; i < newN; ++i)
    {
        voiceEngines[i] = std::move(voiceEngines[i + 1]);
        midiEngines[i]  = std::move(midiEngines[i + 1]);
    }
    voiceEngines[newN].reset();
    midiEngines[newN] = MidiOutputEngine{};
    suspendProcessing(false);
}

//==============================================================================
void PluginProcessor::loadSampleForRhythm(int rhythmIndex, const juce::File& file)
{
    if (rhythmIndex < 0 || rhythmIndex >= numActiveRhythms.load(std::memory_order_acquire)) return;
    voiceEngines[rhythmIndex]->loadFile(file);
    loadedSamplePaths.set(rhythmIndex, file.getFullPathName());
}

//==============================================================================
// Hot-swap: stage a rhythm preset for atomic commit at the next loop boundary.
// If not playing, applies the preset immediately via applyRhythmPreset.
void PluginProcessor::stageRhythmPreset(int rhythmIndex, const juce::File& file)
{
    if (rhythmIndex < 0 || rhythmIndex >= sequencer.getNumRhythms()) return;

    if (!sequencerPlaying.get())
    {
        applyRhythmPreset(file, rhythmIndex);
        return;
    }

    if (!file.existsAsFile()) return;
    auto xml = juce::parseXML(file);
    if (!xml) return;
    auto state = juce::ValueTree::fromXml(*xml);
    if (!state.isValid()) return;

    auto& sw = pendingSwaps[rhythmIndex];

    // Cancel any existing staged swap before overwriting.
    sw.isReady.store(false, std::memory_order_release);
    sw.boundaryReached.store(false, std::memory_order_relaxed);
    sw.pendingVoice.reset();

    // Start from the current rhythm and apply the preset on top (matching applyRhythmPreset).
    Rhythm newRhythm = sequencer.getRhythm(rhythmIndex);
    const juce::String paramPrefix = "r" + juce::String(rhythmIndex) + "_";

    for (int i = 0; kRhythmSuffixes[i] != nullptr; ++i)
    {
        const juce::String suffix = kRhythmSuffixes[i];
        juce::Identifier propId { "r0_" + suffix };
        if (state.hasProperty(propId))
        {
            const float normVal = (float)state.getProperty(propId);
            if (auto* param = apvts.getParameter(paramPrefix + suffix))
            {
                const float actualVal = param->convertFrom0to1(normVal);
                bool pd = false, vd = false;
                applyRhythmSuffix(suffix, actualVal, newRhythm, pd, vd);
            }
        }
    }

    // Name and colour.
    auto nameVal = state.getProperty("r0_name");
    if (nameVal.isString() && nameVal.toString().isNotEmpty())
        newRhythm.name = nameVal.toString().toStdString();
    newRhythm.colourIndex = (int)state.getProperty("r0_colour", newRhythm.colourIndex);

    // Prepare the pending voice engine.
    auto newVoice = std::make_unique<VoiceEngine>();
    newVoice->prepareToPlay(currentSampleRate, currentBlockSize);

    juce::String samplePath;
    juce::String storedPath = state.getProperty("r0_sample").toString();
    if (storedPath.isNotEmpty())
    {
        juce::File sf(storedPath);
        if (sf.existsAsFile())
        {
            newVoice->loadFile(sf);
            samplePath = sf.getFullPathName();
        }
        else
        {
            juce::File fallback = getSamplesDir().getChildFile(juce::File(storedPath).getFileName());
            if (fallback.existsAsFile())
            {
                newVoice->loadFile(fallback);
                samplePath = fallback.getFullPathName();
            }
        }
    }

    // Commit the staged data; set isReady last (release barrier).
    sw.pendingRhythm     = std::move(newRhythm);
    sw.pendingSamplePath = samplePath;
    sw.pendingVoice      = std::move(newVoice);
    sw.boundaryReached.store(false, std::memory_order_relaxed);
    sw.isReady.store(true, std::memory_order_release);
}

void PluginProcessor::cancelStagedSwap(int rhythmIndex)
{
    if (rhythmIndex < 0 || rhythmIndex >= SequencerEngine::MaxRhythms) return;
    auto& sw = pendingSwaps[rhythmIndex];
    sw.isReady.store(false, std::memory_order_release);
    sw.boundaryReached.store(false, std::memory_order_relaxed);
    sw.pendingVoice.reset();
}

bool PluginProcessor::hasPendingSwap(int rhythmIndex) const
{
    if (rhythmIndex < 0 || rhythmIndex >= SequencerEngine::MaxRhythms) return false;
    return pendingSwaps[rhythmIndex].isReady.load(std::memory_order_relaxed);
}

// Called on the message thread when the audio thread signals a boundary was reached.
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

void PluginProcessor::saveRhythmPreset(int rhythmIdx, const juce::String& name,
                                        const juce::String& category)
{
    if (rhythmIdx < 0 || rhythmIdx >= sequencer.getNumRhythms()) return;

    juce::ValueTree state("MuClidRhythm");
    state.setProperty("presetName",     name,     nullptr);
    state.setProperty("presetCategory", category, nullptr);

    const Rhythm& r = sequencer.getRhythm(rhythmIdx);
    state.setProperty("r0_name",   juce::String(r.name),          nullptr);
    state.setProperty("r0_colour", r.colourIndex,                  nullptr);
    state.setProperty("r0_sample", loadedSamplePaths[rhythmIdx],   nullptr);

    const juce::String srcPrefix = "r" + juce::String(rhythmIdx) + "_";
    for (int i = 0; kRhythmSuffixes[i] != nullptr; ++i)
    {
        if (auto* param = apvts.getParameter(srcPrefix + kRhythmSuffixes[i]))
            state.setProperty("r0_" + juce::String(kRhythmSuffixes[i]),
                               param->getValue(), nullptr);
    }

    auto dir = getRhythmsDir();
    dir.createDirectory();
    juce::String safe = name.replaceCharacters("\\/:|*?<>\"", "_________");
    if (safe.isEmpty()) safe = "Rhythm";
    dir.getChildFile(safe + ".muRhyth").replaceWithText(state.toXmlString());
}

bool PluginProcessor::applyRhythmPreset(const juce::File& file, int targetIdx)
{
    if (!file.existsAsFile()) return false;
    auto xml = juce::parseXML(file);
    if (!xml) return false;
    auto state = juce::ValueTree::fromXml(*xml);
    if (!state.isValid()) return false;

    const juce::String dstPrefix = "r" + juce::String(targetIdx) + "_";
    for (int i = 0; kRhythmSuffixes[i] != nullptr; ++i)
    {
        juce::Identifier propId { "r0_" + juce::String(kRhythmSuffixes[i]) };
        if (state.hasProperty(propId))
        {
            if (auto* param = apvts.getParameter(dstPrefix + kRhythmSuffixes[i]))
                param->setValueNotifyingHost((float)state.getProperty(propId));
        }
    }

    Rhythm& r = sequencer.getRhythm(targetIdx);
    auto nameVal = state.getProperty("r0_name");
    if (nameVal.isString() && nameVal.toString().isNotEmpty())
        r.name = nameVal.toString().toStdString();
    r.colourIndex = (int)state.getProperty("r0_colour", r.colourIndex);

    juce::String samplePath = state.getProperty("r0_sample").toString();
    if (samplePath.isNotEmpty())
    {
        juce::File f(samplePath);
        if (f.existsAsFile())
        {
            voiceEngines[targetIdx]->loadFile(f);
            loadedSamplePaths.set(targetIdx, f.getFullPathName());
        }
        else
        {
            juce::File fallback = getSamplesDir().getChildFile(juce::File(samplePath).getFileName());
            if (fallback.existsAsFile())
            {
                voiceEngines[targetIdx]->loadFile(fallback);
                loadedSamplePaths.set(targetIdx, fallback.getFullPathName());
            }
        }
    }
    return true;
}

bool PluginProcessor::applyDefaultRhythm(int rhythmIndex)
{
    return applyRhythmPreset(getRhythmsDir().getChildFile("_default.muRhyth"), rhythmIndex);
}

void PluginProcessor::loadDefaultPreset()
{
    juce::File f = getPresetsDir().getChildFile("_default.muclid");
    if (f.existsAsFile())
        loadPreset(f);
}

//==============================================================================
static void populateStateTree(juce::ValueTree& state, int numRhythms,
                              SequencerEngine& seq, const juce::StringArray& samplePaths)
{
    state.setProperty("numRhythms", numRhythms, nullptr);
    for (int i = 0; i < numRhythms; ++i)
    {
        const Rhythm& r = seq.getRhythm(i);
        state.setProperty("r" + juce::String(i) + "_name",   juce::String(r.name),   nullptr);
        state.setProperty("r" + juce::String(i) + "_colour", r.colourIndex,           nullptr);
        state.setProperty("r" + juce::String(i) + "_sample", samplePaths[i],          nullptr);
    }
}

void PluginProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    populateStateTree(state, sequencer.getNumRhythms(), sequencer, loadedSamplePaths);
    juce::MemoryOutputStream(destData, true).writeString(state.toXmlString());
}

void PluginProcessor::restoreStateFromTree(const juce::ValueTree& state)
{
    int n = juce::jlimit(1, SequencerEngine::MaxRhythms,
                         (int)state.getProperty("numRhythms", 1));

    // Expand to MaxRhythms so parameterChanged can write to all 8 rhythm slots.
    sequencer.setNumRhythms(SequencerEngine::MaxRhythms);

    apvtsLoading = true;
    apvts.replaceState(state);
    apvtsLoading = false;

    // Trim to actual active count.
    sequencer.setNumRhythms(n);

    // Populate fixed voice/midi arrays to match n.
    // Ordering matters: when shrinking, store the new (smaller) count BEFORE destroying
    // excess slots so the audio thread can't access a slot being reset.  When expanding,
    // create and prepare slots BEFORE incrementing the count so the audio thread never
    // sees an uninitialised slot.
    const int oldN = numActiveRhythms.load(std::memory_order_acquire);
    if (n < oldN)
    {
        numActiveRhythms.store(n, std::memory_order_release);  // decrement first
        for (int i = n; i < oldN; ++i)
        {
            voiceEngines[i].reset();
            midiEngines[i] = MidiOutputEngine{};
        }
    }
    else
    {
        for (int i = oldN; i < n; ++i)
        {
            voiceEngines[i] = std::make_unique<VoiceEngine>();
            if (currentSampleRate > 0 && currentBlockSize > 0)
            {
                voiceEngines[i]->prepareToPlay(currentSampleRate, currentBlockSize);
                midiEngines[i].prepare(currentSampleRate, currentBlockSize);
            }
        }
        numActiveRhythms.store(n, std::memory_order_release);  // increment after slots ready
    }

    // Restore non-APVTS properties and refresh engines.
    for (int i = 0; i < n; ++i)
    {
        Rhythm& r = sequencer.getRhythm(i);
        r.name        = state.getProperty("r" + juce::String(i) + "_name",
                                          "Rhythm " + juce::String(i + 1)).toString().toStdString();
        r.colourIndex = (int)state.getProperty("r" + juce::String(i) + "_colour", i % 30);

        sequencer.updatePattern(i);
        voiceEngines[i]->setParams(r.voiceParams);

        juce::String samplePath = state.getProperty("r" + juce::String(i) + "_sample").toString();
        juce::String sampleData = state.getProperty("r" + juce::String(i) + "_sampleData").toString();
        juce::String sampleName = state.getProperty("r" + juce::String(i) + "_sampleName").toString();

        loadedSamplePaths.set(i, samplePath);

        if (sampleData.isNotEmpty() && sampleName.isNotEmpty())
        {
            juce::MemoryBlock mb;
            {
                juce::MemoryOutputStream mos(mb, false);
                juce::Base64::convertFromBase64(mos, sampleData);
            }
            if (mb.getSize() > 0)
            {
                juce::File tempDir = juce::File::getSpecialLocation(juce::File::tempDirectory)
                                         .getChildFile("muClid_samples");
                tempDir.createDirectory();
                juce::File tempFile = tempDir.getChildFile(sampleName);
                if (tempFile.replaceWithData(mb.getData(), mb.getSize()))
                {
                    voiceEngines[i]->loadFile(tempFile);
                    loadedSamplePaths.set(i, tempFile.getFullPathName());
                }
            }
        }
        else if (samplePath.isNotEmpty())
        {
            juce::File f(samplePath);
            if (f.existsAsFile())
            {
                voiceEngines[i]->loadFile(f);
            }
            else
            {
                juce::File fallback = getSamplesDir().getChildFile(juce::File(samplePath).getFileName());
                if (fallback.existsAsFile())
                {
                    voiceEngines[i]->loadFile(fallback);
                    loadedSamplePaths.set(i, fallback.getFullPathName());
                }
            }
        }
    }
}

void PluginProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (auto xml = juce::parseXML(juce::String::fromUTF8((const char*)data, sizeInBytes)))
    {
        auto state = juce::ValueTree::fromXml(*xml);
        if (state.isValid())
            restoreStateFromTree(state);
    }
}

//==============================================================================
void PluginProcessor::savePreset(const juce::String& name,
                                 const juce::String& description,
                                 const juce::String& category,
                                 bool embedSamples)
{
    const int n = sequencer.getNumRhythms();

    juce::ValueTree root("MuClidPreset");
    root.setProperty("presetName",        name,        nullptr);
    root.setProperty("presetDescription", description, nullptr);
    root.setProperty("presetCategory",    category,    nullptr);

    for (int i = 0; i < n; ++i)
    {
        const Rhythm& r = sequencer.getRhythm(i);
        juce::ValueTree rTree("Rhythm");
        rTree.setProperty("name",   juce::String(r.name), nullptr);
        rTree.setProperty("colour", r.colourIndex,         nullptr);
        rTree.setProperty("sample", loadedSamplePaths[i],  nullptr);

        const juce::String srcPrefix = "r" + juce::String(i) + "_";
        for (int j = 0; kRhythmSuffixes[j] != nullptr; ++j)
        {
            if (auto* param = apvts.getParameter(srcPrefix + kRhythmSuffixes[j]))
                rTree.setProperty(kRhythmSuffixes[j], param->getValue(), nullptr);
        }

        if (embedSamples)
        {
            const juce::String path = loadedSamplePaths[i];
            if (path.isNotEmpty())
            {
                juce::File f(path);
                if (f.existsAsFile())
                {
                    juce::MemoryBlock mb;
                    if (f.loadFileAsData(mb) && mb.getSize() > 0)
                    {
                        rTree.setProperty("sampleData",
                                           juce::Base64::toBase64(mb.getData(), mb.getSize()), nullptr);
                        rTree.setProperty("sampleName", f.getFileName(), nullptr);
                    }
                }
            }
        }

        root.addChild(rTree, -1, nullptr);
    }

    auto dir = getPresetsDir();
    dir.createDirectory();

    juce::String safeName = name.replaceCharacters("\\/:|*?<>\"", "_________");
    if (safeName.isEmpty()) safeName = "Preset";
    dir.getChildFile(safeName + ".muclid").replaceWithText(root.toXmlString());
}

void PluginProcessor::loadPreset(const juce::File& file)
{
    auto xml = juce::parseXML(file);
    if (!xml) return;
    auto root = juce::ValueTree::fromXml(*xml);
    if (!root.isValid()) return;

    if (root.getType() == juce::Identifier("MuClidPreset"))
    {
        const int n = juce::jlimit(1, SequencerEngine::MaxRhythms, root.getNumChildren());
        sequencer.setNumRhythms(n);

        const int oldN2 = numActiveRhythms.load(std::memory_order_acquire);
        if (n < oldN2)
        {
            numActiveRhythms.store(n, std::memory_order_release);
            for (int i = n; i < oldN2; ++i)
            {
                voiceEngines[i].reset();
                midiEngines[i] = MidiOutputEngine{};
            }
        }
        else
        {
            for (int i = oldN2; i < n; ++i)
            {
                voiceEngines[i] = std::make_unique<VoiceEngine>();
                if (currentSampleRate > 0 && currentBlockSize > 0)
                {
                    voiceEngines[i]->prepareToPlay(currentSampleRate, currentBlockSize);
                    midiEngines[i].prepare(currentSampleRate, currentBlockSize);
                }
            }
            numActiveRhythms.store(n, std::memory_order_release);
        }

        apvtsLoading = true;
        for (int i = 0; i < n; ++i)
        {
            auto rTree = root.getChild(i);
            if (!rTree.isValid()) continue;

            const juce::String dstPrefix = "r" + juce::String(i) + "_";
            for (int j = 0; kRhythmSuffixes[j] != nullptr; ++j)
            {
                juce::Identifier propId { kRhythmSuffixes[j] };
                if (rTree.hasProperty(propId))
                    if (auto* param = apvts.getParameter(dstPrefix + kRhythmSuffixes[j]))
                        param->setValueNotifyingHost((float)rTree.getProperty(propId));
            }

            Rhythm& r = sequencer.getRhythm(i);
            auto nameVal = rTree.getProperty("name");
            if (nameVal.isString() && nameVal.toString().isNotEmpty())
                r.name = nameVal.toString().toStdString();
            r.colourIndex = (int)rTree.getProperty("colour", r.colourIndex);

            juce::String sampleData = rTree.getProperty("sampleData").toString();
            juce::String sampleName = rTree.getProperty("sampleName").toString();
            juce::String samplePath = rTree.getProperty("sample").toString();
            loadedSamplePaths.set(i, samplePath);

            if (sampleData.isNotEmpty() && sampleName.isNotEmpty())
            {
                juce::MemoryBlock mb;
                {
                    juce::MemoryOutputStream mos(mb, false);
                    juce::Base64::convertFromBase64(mos, sampleData);
                }
                if (mb.getSize() > 0)
                {
                    juce::File tempDir = juce::File::getSpecialLocation(juce::File::tempDirectory)
                                             .getChildFile("muClid_samples");
                    tempDir.createDirectory();
                    juce::File tempFile = tempDir.getChildFile(sampleName);
                    if (tempFile.replaceWithData(mb.getData(), mb.getSize()))
                    {
                        voiceEngines[i]->loadFile(tempFile);
                        loadedSamplePaths.set(i, tempFile.getFullPathName());
                    }
                }
            }
            else if (samplePath.isNotEmpty())
            {
                juce::File f(samplePath);
                if (f.existsAsFile())
                {
                    voiceEngines[i]->loadFile(f);
                }
                else
                {
                    juce::File fallback = getSamplesDir().getChildFile(juce::File(samplePath).getFileName());
                    if (fallback.existsAsFile())
                    {
                        voiceEngines[i]->loadFile(fallback);
                        loadedSamplePaths.set(i, fallback.getFullPathName());
                    }
                }
            }

            sequencer.updatePattern(i);
            voiceEngines[i]->setParams(r.voiceParams);
        }
        apvtsLoading = false;
    }
    else
    {
        restoreStateFromTree(root);
    }
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new PluginProcessor();
}
