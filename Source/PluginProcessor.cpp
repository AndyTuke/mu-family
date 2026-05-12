#include "PluginProcessor.h"
#if MUCLID_LITE_BUILD
#include "LiteEditor.h"
#else
#include "PluginEditor.h"
#endif
#include "Sequencer/Rhythm.h"
#include "FX/FXAlgorithmDef.h"

#include <thread>   // #237: std::this_thread::yield in modulator deserialise lock-spin

// #237: forward declarations so save/load paths defined earlier in this file
// can reference the modulator serialise helpers defined later.
static juce::ValueTree serialiseModulators(const Rhythm& r);
static void            deserialiseModulators(const juce::ValueTree& mods, Rhythm& r);
static void            clearModulators(Rhythm& r);

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

    // #217: ADSR time params in 0..10 seconds with 0.3 skew. Skew 0.3 puts
    // ~100–200 ms at slider centre so the drum-snap region (1–30 ms) gets
    // generous resolution in the lower third, while pad/ambient values
    // (1–10 s) sit comfortably at the top. Stored value is in seconds
    // directly so host automation lanes show "0.150 s" / "2.5 s" instead
    // of the legacy meaningless 0–100. Format string mirrors the slider's
    // "X ms" below 1 s / "X.XX s" above. Rolled out per envelope to keep
    // each step independently revertable (a → amp, then filter, then pitch).
    auto addAdsrT = [&](const juce::String& id, const juce::String& name, float def)
    {
        layout.add(std::make_unique<juce::AudioParameterFloat>(
            id, name, juce::NormalisableRange<float>(0.0f, 10.0f, 0.0f, 0.3f), def,
            juce::AudioParameterFloatAttributes().withStringFromValueFunction(
                [](float v, int) -> juce::String {
                    if (v >= 1.0f) return juce::String(v, 2) + " s";
                    return juce::String((int)std::round(v * 1000.0f)) + " ms";
                })));
    };

    // ── Per-rhythm parameters (47 × 8 = 376) ─────────────────────────────────
    // Rhythms 0..kAutomatedRhythms-1 use full "Rhythm N " names so DAW automation
    // lanes show them clearly. Remaining rhythms use short "RN " names.
    for (int i = 0; i < SequencerEngine::MaxRhythms; ++i)
    {
        const juce::String p = "r" + juce::String(i) + "_";
        const juce::String n = (i < kAutomatedRhythms)
                                   ? "Rhythm " + juce::String(i + 1) + " "
                                   : "R"       + juce::String(i + 1) + " ";

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
        addB(p+"pEnvLeg",   n+"P Env Legato", false);   // #221
        // Filter
        addI(p+"fltType", n+"Filter Type", 0, 15, 0);  // 0-15: LP12/HP12/BP12/Notch/LP24/HP24/BP24/LP6/Comb+/AP12/Notch24/HP6/Peak/LoShf/HiShf/Comb-
        // #216: log-skewed range. Skew 0.25 puts ~1.3 kHz at slider centre and
        // gives the sub-bass / midrange the resolution they need. Without this,
        // 20–200 Hz lived in ~1% of knob travel.
        layout.add(std::make_unique<juce::AudioParameterFloat>(
            p+"fltCut", n+"Filter Cut",
            juce::NormalisableRange<float>(20.0f, 20000.0f, 0.0f, 0.25f), 8000.0f,
            juce::AudioParameterFloatAttributes().withStringFromValueFunction(
                [](float v, int) -> juce::String {
                    if (v < 1000.0f) return juce::String(v, 1) + " Hz";
                    return juce::String(v / 1000.0f, 1) + " kHz";
                })));
        addF(p+"fltRes",  n+"Filter Res",   0.0f,    0.99f,    0.2f);
        addF(p+"fEnvAtk", n+"F Env Atk",  0.0f, 100.0f,  1.0f);
        addF(p+"fEnvDec", n+"F Env Dec",  0.0f, 100.0f,  3.0f);
        addF(p+"fEnvSus", n+"F Env Sus",  0.0f, 100.0f,  0.0f);
        addF(p+"fEnvRel", n+"F Env Rel",  0.0f, 100.0f,  3.0f);
        addF(p+"fEnvDep", n+"F Env Dep",  0.0f,  48.0f,  0.0f);
        addB(p+"fEnvLeg", n+"F Env Legato", false);   // #221
        // Amp
        addF(p+"ampLvl",  n+"Amp Level",  0.0f,   2.0f,  1.0f);  // Issue #121: 0 dB default
        addAdsrT(p+"aEnvAtk", n+"A Env Atk", 0.005f);   // #217a — seconds
        addAdsrT(p+"aEnvDec", n+"A Env Dec", 0.3f);
        addF(p+"aEnvSus", n+"A Env Sus",  0.0f, 100.0f, 80.0f);   // sustain stays 0..100 %
        addAdsrT(p+"aEnvRel", n+"A Env Rel", 0.5f);
        addB(p+"aEnvLeg",   n+"A Env Legato", false);   // #221
        addF(p+"accentDb",  n+"Accent",     0.0f,  12.0f,  0.0f);
        // Drive
        addI(p+"drvChar",    n+"Drive Char",   0,     10,      0);  // 0=None … 10=TapeSat
        addF(p+"drvDrv",     n+"Drive",        0.0f, 100.0f,    0.0f);  // Soft/Hard/Fold drive amount
        addF(p+"drvOut",     n+"Drive Out",  -24.0f,    0.0f,   0.0f);  // Soft/Hard/Fold output level
        addF(p+"drvBits",    n+"Bits",         1.0f,  16.0f,   16.0f);  // Bitcrusher bit depth
        addF(p+"drvRate",    n+"Drive Rate",  100.0f, 48000.0f, 48000.0f);  // Bitcrusher sample rate
        addF(p+"drvDit",     n+"Dither",       0.0f, 100.0f,    0.0f);  // Bitcrusher dither amount
        addF(p+"drvTon",     n+"Drive Tone",  20.0f, 20000.0f, 20000.0f);  // Shared LPF
        addF(p+"eqMidGain",  n+"EQ Mid Gain",-18.0f,  18.0f,   0.0f);  // EQ mid-band gain (#129)
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
    addF("eff2dly", juce::String::fromUTF8(u8"Effect→Delay"),  0.0f, 1.0f, 0.0f);
    addF("eff2rev", juce::String::fromUTF8(u8"Effect→Reverb"), 0.0f, 1.0f, 0.0f);
    addF("dly2rev", juce::String::fromUTF8(u8"Delay→Reverb"),  0.0f, 1.0f, 0.0f);

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

    // ── Rhythm channel strips (11 × 8 = 88) ──────────────────────────────────
    for (int i = 0; i < SequencerEngine::MaxRhythms; ++i)
    {
        const juce::String c = "ch" + juce::String(i) + "_";
        const juce::String n = (i < kAutomatedRhythms)
                                   ? "Rhythm " + juce::String(i + 1) + " Ch "
                                   : "Ch"      + juce::String(i + 1) + " ";
        addF(c+"lvl",     n+"Level",      0.0f, 1.0f,  1.0f);  // Issue #121: 0 dB default
        addF(c+"pan",     n+"Pan",       -1.0f, 1.0f,  0.0f);
        addB(c+"mute",    n+"Mute",      false);
        addB(c+"solo",    n+"Solo",      false);
        addF(c+"sendEff", n+"Send Eff",  0.0f, 1.0f,  0.0f);
        addF(c+"sendDly", n+"Send Dly",  0.0f, 1.0f,  0.0f);
        addF(c+"sendRev", n+"Send Rev",  0.0f, 1.0f,  0.0f);
        // Sidechain
        addI(c+"scSrc",   n+"SC Src",    0, 8,     0);  // 0=off, 1-8=ch1-ch8
        addF(c+"scAmt",   n+"SC Amount", 0.0f, 1.0f, 0.0f);
        addF(c+"scAtk",   n+"SC Attack", 1.0f, 500.0f, 5.0f);
        addF(c+"scRel",   n+"SC Release",10.0f, 2000.0f, 100.0f);
        // Multi-bus output routing: 0 = Master mix, 1..8 = direct out to Bus 1..8.
        addI(c+"outBus",  n+"Output Bus",0, 8,     0);
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

    // ── Master (2 params + 8 insert params) ──────────────────────────────────
    addF("mstr_lvl",    "Master Level",     0.0f,    1.0f,     1.0f);   // Issue #121: 0 dB default
    addF("mstr_pan",    "Master Pan",      -1.0f,    1.0f,     0.0f);
    addI("mstrLoop",    "Master Loop",      0,       16,       0);      // 0=free, 1-16 → 16-256 steps
    // Master insert effect (#124): same algorithm set as per-rhythm voice INSERT.
    addI("mst_insChar", "Mst Insert Char",  0,       10,       0);      // 0=None … 10=TapeSat
    addF("mst_insDrv",  "Mst Insert Drive", 0.0f,  100.0f,     0.0f);
    addF("mst_insOut",  "Mst Insert Out", -24.0f,    0.0f,     0.0f);
    addF("mst_insBits", "Mst Insert Bits",  1.0f,   16.0f,    16.0f);
    addF("mst_insRate", "Mst Insert Rate", 100.0f, 48000.0f, 48000.0f);
    addF("mst_insDit",  "Mst Insert Dit",   0.0f,  100.0f,     0.0f);
    addF("mst_insTon",  "Mst Insert Tone", 20.0f, 20000.0f, 20000.0f);
    addF("mst_insMid",  "Mst Insert Mid", -18.0f,   18.0f,     0.0f);  // EQ mid gain

#if MUCLID_LITE_BUILD
    addI("lite_midiNote",   "MIDI Note", 0, 127, 36);
    addF("lite_accentAmt",  "Accent",    0.0f, 100.0f, 0.0f);
#endif

    return layout;
}

//==============================================================================
// Declare 10 stereo output buses: Master (always enabled), Out 1..8 + FX Returns
// (disabled by default so a fresh project loads with just one stereo output, matching
// pre-multi-bus behaviour). Hosts that support it can enable the extra buses.
PluginProcessor::PluginProcessor()
#if MUCLID_LITE_BUILD
    : AudioProcessor(BusesProperties()),
#else
    : AudioProcessor(BusesProperties()
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
    // #218: pitch.octave / pitch.fine removed from the populated set so legacy preset
    // assignments targeting them resolve to "destination not found" at apply time
    // (paramValues.find returns end() → assignment silently no-ops).
    modParamValues.reserve(24);
    for (const char* key : { "amp.attack", "amp.decay", "amp.sustain", "amp.release",
                              "filter.cutoff", "filter.resonance",
                              "fenv.attack", "fenv.decay", "fenv.depth",
                              "pitch.semitones",
                              "insert.drive", "insert.output",
                              "insert.bits", "insert.rate", "insert.dither", "insert.lpf",
                              // #223 additions
                              "pitch.envDepth", "amp.level", "accentDb" })
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
                // #218: collapsed pitch destinations to single "pitch.semitones" (±24 st full swing).
                // pitch.octave / pitch.fine no longer in the value map — legacy preset assignments
                // targeting them silently no-op at apply time.
                modParamValues["pitch.semitones"]  = 0.0f;
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
                    // #218: single Pitch destination, ±24 st full swing.
                    snap[kSnapPitchSemi]   .set(sn(modParamValues["pitch.semitones"], -24.0f,    24.0f));
                    snap[kSnapPitchOct]    .set(0.0f);   // deprecated by #218 — no longer modulatable
                    snap[kSnapPitchFine]   .set(0.0f);   // deprecated by #218 — no longer modulatable
                    snap[kSnapInsDrive]    .set(sn(modParamValues["insert.drive"],     0.0f,    100.0f));
                    snap[kSnapInsOutput]   .set(sn(modParamValues["insert.output"],   -24.0f,    0.0f));
                    snap[kSnapInsBits]     .set(sn(modParamValues["insert.bits"],      1.0f,     16.0f));
                    snap[kSnapInsDither]   .set(sn(modParamValues["insert.dither"],    0.0f,    100.0f));
                    snap[kSnapInsLpf]      .set(snLogHz(modParamValues["insert.lpf"]));   // #216 log
                    // #223 new destinations
                    snap[kSnapPitchEnvDep] .set(sn(modParamValues["pitch.envDepth"],   0.0f,    24.0f));
                    snap[kSnapAmpLvl]      .set(sn(modParamValues["amp.level"],        0.0f,     2.0f));
                    snap[kSnapAccent]      .set(sn(modParamValues["accentDb"],         0.0f,    12.0f));
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
                modParams.pitchMod       = juce::jlimit(-24.0f, 24.0f,
                                                         modParamValues["pitch.semitones"]);
                modParams.driveDrive     = juce::jlimit(0.0f,  100.0f,    modParamValues["insert.drive"]);
                modParams.driveOutput    = juce::jlimit(-24.0f,  0.0f,    modParamValues["insert.output"]);
                modParams.drvBits        = juce::jlimit(1.0f,   16.0f,    modParamValues["insert.bits"]);
                modParams.driveRate      = std::exp(std::log(100.0f) + juce::jlimit(0.0f, 100.0f, modParamValues["insert.rate"]) / 100.0f * (std::log(48000.0f) - std::log(100.0f)));
                modParams.drvDither      = juce::jlimit(0.0f,  100.0f,    modParamValues["insert.dither"]);
                modParams.driveTone      = juce::jlimit(20.0f, 20000.0f,  modParamValues["insert.lpf"]);
                // #223 new destinations write-back
                modParams.pitchEnvDepth  = juce::jlimit(0.0f,   24.0f,    modParamValues["pitch.envDepth"]);
                modParams.ampLevel       = juce::jlimit(0.0f,    2.0f,    modParamValues["amp.level"]);
                modParams.accentDb       = juce::jlimit(0.0f,   12.0f,    modParamValues["accentDb"]);
            }
        }

        if (voiceEngines[r])
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
    fxChain.setHostBpm(effectiveBpm);

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

    mixerEngine.processBlock(masterBus, numRhythms,
                             voiceEngines.data(), fxChain, buffer.getNumSamples(),
                             &directPtrs, fxRetPtr);

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
    if (id.startsWith("ch") || id.startsWith("ret_") || id.startsWith("mstr_") || id.startsWith("mst_ins"))
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
    "pitchOct","pitchSemi","pitchFine","pEnvAtk","pEnvDec","pEnvSus","pEnvRel","pEnvDep","pEnvLeg",
    "fltType","fltCut","fltRes","fEnvAtk","fEnvDec","fEnvSus","fEnvRel","fEnvDep","fEnvLeg",
    "ampLvl","aEnvAtk","aEnvDec","aEnvSus","aEnvRel","aEnvLeg","accentDb",
    "drvChar","drvDrv","drvOut","drvBits","drvRate","drvDit","drvTon","eqMidGain",
    nullptr
};

// Per-channel mixer APVTS parameter suffixes (prefix: "ch{i}_").
// Saved in rhythm presets so sends/sidechain travel with the rhythm.
static const char* const kChannelSuffixes[] = {
    "lvl","pan","mute","solo","sendEff","sendDly","sendRev",
    "scSrc","scAmt","scAtk","scRel","outBus",
    nullptr
};

// Global APVTS parameter IDs written to the GlobalState child of .muclid presets.
static const char* const kGlobalParams[] = {
    "eff_algo","eff_en","eff_send","eff_p0","eff_p1","eff_p2","eff_p3","eff_p4",
    "dly_en","dly_mode","dly_ms","dly_syncDenom","dly_syncDot","dly_syncTrip",
    "dly_count","dly_fb","dly_spread","dly_dirt","dly_send",
    "rev_algo","rev_en","rev_lvl","rev_size","rev_pre","rev_diff","rev_damp","rev_mod","rev_dirt",
    "eff2dly","eff2rev","dly2rev",
    "echo_en","echo_mode","echo_ms","echo_syncDenom","echo_syncDot","echo_syncTrip",
    "echo_count","echo_fb","echo_spread","echo_dirt",
    "ret_eff_lvl","ret_eff_pan","ret_eff_mute","ret_eff_solo",
    "ret_dly_lvl","ret_dly_pan","ret_dly_mute","ret_dly_solo",
    "ret_rev_lvl","ret_rev_pan","ret_rev_mute","ret_rev_solo",
    "mstr_lvl","mstr_pan","mstrLoop",
    "mst_insChar","mst_insDrv","mst_insOut","mst_insBits","mst_insRate","mst_insDit","mst_insTon","mst_insMid",
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
    else if (suffix == "pEnvLeg")   { r.voiceParams.pitchEnvLegato = (v > 0.5f);   voiceDirty = true; }   // #221
    else if (suffix == "fltType")   { r.voiceParams.filterType     = juce::jlimit(0, 15, (int)v); voiceDirty = true; }
    else if (suffix == "fltCut")    { r.voiceParams.filterCutoff   = v;            voiceDirty = true; }
    else if (suffix == "fltRes")    { r.voiceParams.filterRes      = v;            voiceDirty = true; }
    else if (suffix == "fEnvAtk")   { r.voiceParams.filterEnvAtk   = adsrTime(v); voiceDirty = true; }
    else if (suffix == "fEnvDec")   { r.voiceParams.filterEnvDec   = adsrTime(v); voiceDirty = true; }
    else if (suffix == "fEnvSus")   { r.voiceParams.filterEnvSus   = adsrSus(v);  voiceDirty = true; }
    else if (suffix == "fEnvRel")   { r.voiceParams.filterEnvRel   = adsrTime(v); voiceDirty = true; }
    else if (suffix == "fEnvDep")   { r.voiceParams.filterEnvDepth = v;            voiceDirty = true; }
    else if (suffix == "fEnvLeg")   { r.voiceParams.filterEnvLegato = (v > 0.5f);  voiceDirty = true; }   // #221
    else if (suffix == "ampLvl")    { r.voiceParams.ampLevel       = v;            voiceDirty = true; }
    else if (suffix == "aEnvAtk")   { r.voiceParams.ampEnvAtk      = juce::jmax(0.001f, v); voiceDirty = true; }   // #217a seconds
    else if (suffix == "aEnvDec")   { r.voiceParams.ampEnvDec      = juce::jmax(0.001f, v); voiceDirty = true; }   // #217a seconds
    else if (suffix == "aEnvSus")   { r.voiceParams.ampEnvSus      = adsrSus(v);  voiceDirty = true; }
    else if (suffix == "aEnvRel")   { r.voiceParams.ampEnvRel = juce::jmax(0.001f, v); r.voiceParams.ampRelToEnd = (v >= 10.0f); voiceDirty = true; }   // #217a seconds + End at new max
    else if (suffix == "aEnvLeg")   { r.voiceParams.ampEnvLegato = (v > 0.5f);     voiceDirty = true; }   // #221
    else if (suffix == "accentDb")  { r.voiceParams.accentDb        = v;           voiceDirty = true; }
    else if (suffix == "drvChar")    { r.voiceParams.driveChar  = juce::jlimit(0, 10, (int)v); voiceDirty = true; }
    else if (suffix == "drvDrv")     { r.voiceParams.driveDrive = v;  voiceDirty = true; }
    else if (suffix == "drvOut")     { r.voiceParams.driveOutput= v;  voiceDirty = true; }
    else if (suffix == "drvBits")    { r.voiceParams.drvBits    = v;  voiceDirty = true; }
    else if (suffix == "drvRate")    { r.voiceParams.driveRate  = v;  voiceDirty = true; }
    else if (suffix == "drvDit")     { r.voiceParams.drvDither  = v;  voiceDirty = true; }
    else if (suffix == "drvTon")     { r.voiceParams.driveTone  = v;  voiceDirty = true; }
    else if (suffix == "eqMidGain")  { r.voiceParams.eqMidGain  = v;  voiceDirty = true; }
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
        if (voiceDirty && voiceEngines[ri]) voiceEngines[ri]->setParams(r.voiceParams);
    }
}

// Force-sync a rhythm's full Rhythm struct (HitGenerator state + voiceParams) from
// current APVTS values, regardless of whether parameterChanged would fire. Needed
// after preset reload paths where APVTS values land back on the same numbers they
// held before (e.g. preset A → B → A): JUCE skips listener callbacks on unchanged
// values, so a freshly-constructed Rhythm object at a regrown slot would never get
// its data populated and would play with default `hits=0` patterns (silent) and
// default voice params.
void PluginProcessor::forceSyncRhythmFromAPVTS(int ri)
{
    if (ri < 0 || ri >= SequencerEngine::MaxRhythms) return;
    if (ri >= sequencer.getNumRhythms()) return;

    Rhythm& r = sequencer.getRhythm(ri);
    const juce::String prefix = "r" + juce::String(ri) + "_";

    bool patternDirty = false;
    bool voiceDirty   = false;
    for (int j = 0; kRhythmSuffixes[j] != nullptr; ++j)
    {
        if (auto* raw = apvts.getRawParameterValue(prefix + kRhythmSuffixes[j]))
            applyRhythmSuffix(kRhythmSuffixes[j], raw->load(), r, patternDirty, voiceDirty);
    }

    if (patternDirty) sequencer.updatePattern(ri);
    if (voiceEngines[ri]) voiceEngines[ri]->setParams(r.voiceParams);
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
            else if (param == "scSrc")   ch.sidechainSource   = juce::roundToInt(v) - 1;
            else if (param == "scAmt")   ch.sidechainAmount   = v;
            else if (param == "scAtk")   ch.sidechainAttackMs  = v;
            else if (param == "scRel")   ch.sidechainReleaseMs = v;
            else if (param == "outBus")  ch.outputBus         = juce::jlimit(0, 8, juce::roundToInt(v));
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
    else if (id == "mst_insChar") mixerEngine.masterInsertParams.driveChar  = juce::jlimit(0, 10, (int)v);
    else if (id == "mst_insDrv")  mixerEngine.masterInsertParams.driveDrive = v;
    else if (id == "mst_insOut")  mixerEngine.masterInsertParams.driveOutput= v;
    else if (id == "mst_insBits") mixerEngine.masterInsertParams.drvBits    = v;
    else if (id == "mst_insRate") mixerEngine.masterInsertParams.driveRate  = v;
    else if (id == "mst_insDit")  mixerEngine.masterInsertParams.drvDither  = v;
    else if (id == "mst_insTon")  mixerEngine.masterInsertParams.driveTone  = v;
    else if (id == "mst_insMid")  mixerEngine.masterInsertParams.eqMidGain  = v;
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
    set(px+"pEnvLeg",   vp.pitchEnvLegato ? 1.0f : 0.0f);   // #221
    set(px+"fltType",   (float)vp.filterType);
    set(px+"fltCut",    vp.filterCutoff);
    set(px+"fltRes",    vp.filterRes);
    set(px+"fEnvAtk",   vp.filterEnvAtk  * (100.0f/3.0f));
    set(px+"fEnvDec",   vp.filterEnvDec  * (100.0f/3.0f));
    set(px+"fEnvSus",   vp.filterEnvSus  * 100.0f);
    set(px+"fEnvRel",   vp.filterEnvRel  * (100.0f/3.0f));
    set(px+"fEnvDep",   vp.filterEnvDepth);
    set(px+"fEnvLeg",   vp.filterEnvLegato ? 1.0f : 0.0f);   // #221
    set(px+"ampLvl",    vp.ampLevel);
    set(px+"aEnvAtk",   vp.ampEnvAtk);   // #217a seconds
    set(px+"aEnvDec",   vp.ampEnvDec);   // #217a seconds
    set(px+"aEnvSus",   vp.ampEnvSus  * 100.0f);
    // #217a: ampRel writes 10 (new max) when ampRelToEnd is on so the >=10 check in
    // applyRhythmSuffix triggers; otherwise writes the seconds value directly.
    set(px+"aEnvRel",   vp.ampRelToEnd ? 10.0f : vp.ampEnvRel);
    set(px+"aEnvLeg",   vp.ampEnvLegato ? 1.0f : 0.0f);   // #221
    set(px+"accentDb",  vp.accentDb);
    set(px+"drvChar",   (float)vp.driveChar);
    set(px+"drvDrv",    vp.driveDrive);
    set(px+"drvOut",    vp.driveOutput);
    set(px+"drvBits",   vp.drvBits);
    set(px+"drvRate",   vp.driveRate);
    set(px+"drvDit",    vp.drvDither);
    set(px+"drvTon",    vp.driveTone);
    set(px+"eqMidGain", vp.eqMidGain);
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
    // Sidechain + bus routing: previously missing here, so after a sidebar reorder
    // the in-memory swap was correct but APVTS still held the old slot's values,
    // and the APVTS->engine listener would overwrite the engine back to stale state.
    set(px+"scSrc",   (float)(ch.sidechainSource + 1));  // engine -1..7 → APVTS 0..8
    set(px+"scAmt",   ch.sidechainAmount);
    set(px+"scAtk",   ch.sidechainAttackMs);
    set(px+"scRel",   ch.sidechainReleaseMs);
    set(px+"outBus",  (float)ch.outputBus);
}

void PluginProcessor::swapAPVTSForRhythms(int i, int j)
{
    apvtsLoading = true;
    pushRhythmToAPVTS(i);
    pushRhythmToAPVTS(j);
    // Push every active channel: not just i and j, because sidechain source
    // indices on OTHER channels may have been re-translated by the swap
    // (any channel pointing at i now points at j, and vice versa).
    const int n = numActiveRhythms.load(std::memory_order_acquire);
    for (int c = 0; c < n; ++c)
        pushMixerChannelToAPVTS(c);
    apvtsLoading = false;
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

// Called on the message thread when the audio thread signals a boundary was reached
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
        if (auto* param = apvts.getParameter(srcPrefix + kRhythmSuffixes[i]))
            state.setProperty("r0_" + juce::String(kRhythmSuffixes[i]), param->getValue(), nullptr);

    const juce::String chPrefix = "ch" + juce::String(rhythmIdx) + "_";
    for (int i = 0; kChannelSuffixes[i] != nullptr; ++i)
        if (auto* param = apvts.getParameter(chPrefix + kChannelSuffixes[i]))
            state.setProperty("ch_" + juce::String(kChannelSuffixes[i]), param->getValue(), nullptr);

    auto dir = getRhythmsDir();
    dir.createDirectory();
    juce::String safe = name.replaceCharacters("\\/:|*?<>\"", "_________");
    if (safe.isEmpty()) safe = "Rhythm";
    dir.getChildFile(safe + ".muRhyth").replaceWithText(state.toXmlString());
}

void PluginProcessor::saveRhythmPresetToFile(int rhythmIdx, const juce::File& destFile,
                                             bool embedSample)
{
    if (rhythmIdx < 0 || rhythmIdx >= sequencer.getNumRhythms()) return;

    juce::ValueTree state("MuClidRhythm");
    state.setProperty("presetName",     destFile.getFileNameWithoutExtension(), nullptr);
    state.setProperty("presetCategory", "",                                     nullptr);

    const Rhythm& r = sequencer.getRhythm(rhythmIdx);
    state.setProperty("r0_name",   juce::String(r.name),        nullptr);
    state.setProperty("r0_colour", r.colourIndex,                nullptr);
    state.setProperty("r0_sample", loadedSamplePaths[rhythmIdx], nullptr);

    // Rhythm presets store ONLY sequencer-page state (Euclidean params, voice chain,
    // envelopes, insert effect). Mixer-page state (channel level/pan/sends/sidechain/
    // output bus) intentionally stays with the slot, not with the rhythm.
    const juce::String srcPrefix = "r" + juce::String(rhythmIdx) + "_";
    for (int i = 0; kRhythmSuffixes[i] != nullptr; ++i)
        if (auto* param = apvts.getParameter(srcPrefix + kRhythmSuffixes[i]))
            state.setProperty("r0_" + juce::String(kRhythmSuffixes[i]), param->getValue(), nullptr);

    // #237: serialise modulators (ControlSequences + ModulationMatrix assignments).
    state.addChild(serialiseModulators(sequencer.getRhythm(rhythmIdx)), -1, nullptr);

    if (embedSample)
    {
        const juce::String path = loadedSamplePaths[rhythmIdx];
        if (path.isNotEmpty())
        {
            juce::File f(path);
            if (f.existsAsFile())
            {
                juce::MemoryBlock mb;
                if (f.loadFileAsData(mb) && mb.getSize() > 0)
                {
                    state.setProperty("sampleData",
                                      juce::Base64::toBase64(mb.getData(), mb.getSize()), nullptr);
                    state.setProperty("sampleName", f.getFileName(), nullptr);
                }
            }
        }
    }

    destFile.replaceWithText(state.toXmlString());
}

bool PluginProcessor::applyRhythmPreset(const juce::File& file, int targetIdx)
{
    if (!file.existsAsFile()) return false;
    auto xml = juce::parseXML(file);
    if (!xml) return false;
    auto state = juce::ValueTree::fromXml(*xml);
    if (!state.isValid()) return false;

    // Load only sequencer-page state — mixer settings stay attached to the slot.
    // Older rhythm preset files may include "ch_*" properties; those are simply
    // ignored on load (no read here) so legacy files load cleanly with the new policy.
    const juce::String dstPrefix = "r" + juce::String(targetIdx) + "_";
    for (int i = 0; kRhythmSuffixes[i] != nullptr; ++i)
    {
        juce::Identifier propId { "r0_" + juce::String(kRhythmSuffixes[i]) };
        if (state.hasProperty(propId))
            if (auto* param = apvts.getParameter(dstPrefix + kRhythmSuffixes[i]))
                param->setValueNotifyingHost((float)state.getProperty(propId));
    }

    Rhythm& r = sequencer.getRhythm(targetIdx);
    auto nameVal = state.getProperty("r0_name");
    if (nameVal.isString() && nameVal.toString().isNotEmpty())
        r.name = nameVal.toString().toStdString();
    r.colourIndex = (int)state.getProperty("r0_colour", r.colourIndex);

    // #237: restore modulators if the preset carries a Modulators child.
    // Legacy .muRhyth (no Modulators) is left alone — the rhythm keeps its
    // existing in-memory CS state instead of being wiped. This is the
    // intentional opposite of clear-then-load for the host-state path: when
    // a user loads a rhythm preset that pre-dates #237, we don't want their
    // freshly-authored modulators getting destroyed.
    if (auto mods = state.getChildWithName("Modulators"); mods.isValid())
    {
        clearModulators(r);
        deserialiseModulators(mods, r);
    }

    // Embedded sample takes priority over path-based load (mirrors full-preset logic).
    juce::String encodedData = state.getProperty("sampleData").toString();
    if (encodedData.isNotEmpty())
    {
        juce::MemoryOutputStream mos;
        if (juce::Base64::convertFromBase64(mos, encodedData) && mos.getDataSize() > 0)
        {
            juce::String sampleName = state.getProperty("sampleName", "embedded").toString();
            juce::File tmp = juce::File::getSpecialLocation(juce::File::tempDirectory)
                                 .getChildFile("muclid_" + sampleName);
            if (tmp.replaceWithData(mos.getData(), mos.getDataSize()))
            {
                voiceEngines[targetIdx]->loadFile(tmp);
                loadedSamplePaths.set(targetIdx, tmp.getFullPathName());
            }
        }
    }
    else
    {
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
                else
                {
                    // Linked sample missing — drop any previously loaded sample and
                    // keep the recorded path so the RhythmPanel can show a "missing"
                    // indicator and offer a relocate-to-find browse action.
                    voiceEngines[targetIdx]->clearSample();
                    loadedSamplePaths.set(targetIdx, samplePath);
                }
            }
        }
        else
        {
            voiceEngines[targetIdx]->clearSample();
            loadedSamplePaths.set(targetIdx, juce::String());
        }
    }

    // #240: force-sync APVTS → Rhythm so freshly-defaulted slots (after a
    // shrink/grow cycle) get their fields repopulated even when JUCE skips
    // the parameterChanged listener because incoming values match the
    // already-stored APVTS values.
    forceSyncRhythmFromAPVTS(targetIdx);
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
// #237 — Modulator state serialisation.
// Captures everything not APVTS-backed: per-rhythm ControlSequences
// (mode/polarity/loop+step timing/stepValues/curvePoints with Bézier handles)
// and ModulationMatrix assignments (id/source/dest/depth/curve).
//
// Schema (one Modulators child per rhythm, identified by rhythmIdx property):
//   <Modulators rhythmIdx="0">
//     <Seq id="cs0" mode="0" polarity="0"
//          loopNV="2" loopMod="0" loopMult="4"
//          stepNV="2" stepMod="0" stepMult="1">
//       <Step v="42.5"/>
//       <Point x="0.0" y="0.5" bez="0" hx="0.0" hy="0.0"/>
//     </Seq>
//     <Asgn id="..." src="cs0_output" dest="filter.cutoff" depth="50" curve="0"/>
//   </Modulators>
//
// Idempotent: a tree without a Modulators child leaves the rhythm's existing
// in-memory defaults untouched (legacy preset compat).
static juce::ValueTree serialiseModulators(const Rhythm& r)
{
    juce::ValueTree mods("Modulators");

    for (const auto& cs : r.controlSequences)
    {
        juce::ValueTree seq("Seq");
        seq.setProperty("id",       juce::String(cs.id),               nullptr);
        seq.setProperty("mode",     (int)cs.mode,                       nullptr);
        seq.setProperty("polarity", (int)cs.polarity,                   nullptr);
        seq.setProperty("loopNV",   (int)cs.loopNoteValue,              nullptr);
        seq.setProperty("loopMod",  (int)cs.loopNoteMod,                nullptr);
        seq.setProperty("loopMult", cs.loopMultiplier,                  nullptr);
        seq.setProperty("stepNV",   (int)cs.stepNoteValue,              nullptr);
        seq.setProperty("stepMod",  (int)cs.stepNoteMod,                nullptr);
        seq.setProperty("stepMult", cs.stepMultiplier,                  nullptr);

        for (const auto v : cs.stepValues)
        {
            juce::ValueTree st("Step");
            st.setProperty("v", v, nullptr);
            seq.addChild(st, -1, nullptr);
        }
        for (const auto& pt : cs.curvePoints)
        {
            juce::ValueTree p("Point");
            p.setProperty("x",   pt.x,                          nullptr);
            p.setProperty("y",   pt.y,                          nullptr);
            p.setProperty("bez", pt.hasBezierHandle ? 1 : 0,    nullptr);
            p.setProperty("hx",  pt.handleX,                    nullptr);
            p.setProperty("hy",  pt.handleY,                    nullptr);
            seq.addChild(p, -1, nullptr);
        }

        mods.addChild(seq, -1, nullptr);
    }

    for (const auto& a : r.modulationMatrix.getAssignments())
    {
        juce::ValueTree asgn("Asgn");
        asgn.setProperty("id",    juce::String(a.id),            nullptr);
        asgn.setProperty("src",   juce::String(a.sourceId),      nullptr);
        asgn.setProperty("dest",  juce::String(a.destinationId), nullptr);
        asgn.setProperty("depth", a.depth,                       nullptr);
        asgn.setProperty("curve", a.curve,                       nullptr);
        mods.addChild(asgn, -1, nullptr);
    }

    return mods;
}

static void deserialiseModulators(const juce::ValueTree& mods, Rhythm& r)
{
    if (!mods.isValid() || mods.getType() != juce::Identifier("Modulators"))
        return;

    // Spin on the rhythm's modLock so the audio thread can't be inside
    // ModulationMatrix::process() / ControlSequence::evaluate() while we
    // overwrite the vectors. Mirrors ModulatorEditor's lock pattern.
    while (r.modLock.exchange(true, std::memory_order_acquire))
        std::this_thread::yield();

    for (int ci = 0; ci < mods.getNumChildren(); ++ci)
    {
        auto node = mods.getChild(ci);

        if (node.getType() == juce::Identifier("Seq"))
        {
            const juce::String id = node.getProperty("id").toString();
            // Find matching CS by id (csN). The vector is pre-sized to
            // MaxControlSequences in Rhythm's ctor so we patch in place.
            for (auto& cs : r.controlSequences)
            {
                if (juce::String(cs.id) != id) continue;
                cs.mode           = (ControlSequence::Mode)    (int)node.getProperty("mode",     (int)cs.mode);
                cs.polarity       = (ControlSequence::Polarity)(int)node.getProperty("polarity", (int)cs.polarity);
                cs.loopNoteValue  = (NoteValue)                (int)node.getProperty("loopNV",   (int)cs.loopNoteValue);
                cs.loopNoteMod    = (NoteMod)                  (int)node.getProperty("loopMod",  (int)cs.loopNoteMod);
                cs.loopMultiplier =                            (int)node.getProperty("loopMult", cs.loopMultiplier);
                cs.stepNoteValue  = (NoteValue)                (int)node.getProperty("stepNV",   (int)cs.stepNoteValue);
                cs.stepNoteMod    = (NoteMod)                  (int)node.getProperty("stepMod",  (int)cs.stepNoteMod);
                cs.stepMultiplier =                            (int)node.getProperty("stepMult", cs.stepMultiplier);

                cs.stepValues.clear();
                cs.curvePoints.clear();
                for (int j = 0; j < node.getNumChildren(); ++j)
                {
                    auto child = node.getChild(j);
                    if (child.getType() == juce::Identifier("Step"))
                    {
                        cs.stepValues.push_back((float)(double)child.getProperty("v", 0.0));
                    }
                    else if (child.getType() == juce::Identifier("Point"))
                    {
                        ControlSequence::CurvePoint pt;
                        pt.x                = (float)(double)child.getProperty("x", 0.0);
                        pt.y                = (float)(double)child.getProperty("y", 0.0);
                        pt.hasBezierHandle  = (int)child.getProperty("bez", 0) != 0;
                        pt.handleX          = (float)(double)child.getProperty("hx", 0.0);
                        pt.handleY          = (float)(double)child.getProperty("hy", 0.0);
                        cs.curvePoints.push_back(pt);
                    }
                }
                break;
            }
        }
        else if (node.getType() == juce::Identifier("Asgn"))
        {
            ModulationAssignment a;
            a.id            = node.getProperty("id").toString().toStdString();
            a.sourceId      = node.getProperty("src").toString().toStdString();
            a.destinationId = node.getProperty("dest").toString().toStdString();
            a.depth         = (float)(double)node.getProperty("depth", 0.0);
            a.curve         = (float)(double)node.getProperty("curve", 0.0);
            r.modulationMatrix.addAssignment(a);
        }
    }

    r.modLock.store(false, std::memory_order_release);
}

// Clears CS step/curve data + assignments before each deserialise so successive
// preset loads don't accumulate state. Only called from the preset-load paths.
static void clearModulators(Rhythm& r)
{
    while (r.modLock.exchange(true, std::memory_order_acquire))
        std::this_thread::yield();

    while (!r.modulationMatrix.getAssignments().empty())
        r.modulationMatrix.removeAssignment(r.modulationMatrix.getAssignments().front().id);

    for (auto& cs : r.controlSequences)
    {
        cs.stepValues.clear();
        cs.curvePoints.clear();
    }

    r.modLock.store(false, std::memory_order_release);
}

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

        // #237: per-rhythm modulator state as a child of the APVTS state tree.
        auto mods = serialiseModulators(r);
        mods.setProperty("rhythmIdx", i, nullptr);
        state.addChild(mods, -1, nullptr);
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

    // #237: restore per-rhythm modulator state from the Modulators children.
    // Each child carries a rhythmIdx property so we apply to the right slot
    // regardless of child ordering. Legacy state (no Modulators children)
    // leaves rhythm defaults in place — clean degradation.
    for (int ci = 0; ci < state.getNumChildren(); ++ci)
    {
        auto child = state.getChild(ci);
        if (child.getType() != juce::Identifier("Modulators")) continue;
        const int ri = (int)child.getProperty("rhythmIdx", -1);
        if (ri < 0 || ri >= n) continue;
        Rhythm& target = sequencer.getRhythm(ri);
        clearModulators(target);
        deserialiseModulators(child, target);
    }

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

        // #240: force-sync APVTS → Rhythm so shrink/grow cycles (preset A → B → A)
        // repopulate freshly-defaulted Rhythm fields even when JUCE skips listener
        // callbacks because the APVTS values didn't change. Internally calls
        // updatePattern + voiceEngines[i]->setParams.
        forceSyncRhythmFromAPVTS(i);

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
            if (auto* param = apvts.getParameter(srcPrefix + kRhythmSuffixes[j]))
                rTree.setProperty(kRhythmSuffixes[j], param->getValue(), nullptr);

        const juce::String chSrcPrefix = "ch" + juce::String(i) + "_";
        for (int j = 0; kChannelSuffixes[j] != nullptr; ++j)
            if (auto* param = apvts.getParameter(chSrcPrefix + kChannelSuffixes[j]))
                rTree.setProperty("ch_" + juce::String(kChannelSuffixes[j]), param->getValue(), nullptr);

        // #237: serialise modulators per rhythm.
        rTree.addChild(serialiseModulators(r), -1, nullptr);

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

    // Save global FX/mixer state so a preset fully restores the session.
    juce::ValueTree globalTree("GlobalState");
    for (int i = 0; kGlobalParams[i] != nullptr; ++i)
        if (auto* param = apvts.getParameter(kGlobalParams[i]))
            globalTree.setProperty(kGlobalParams[i], param->getValue(), nullptr);
    root.addChild(globalTree, -1, nullptr);

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
        // Count only Rhythm-type children; GlobalState child was added in #123.
        int n = 0;
        for (int ci = 0; ci < root.getNumChildren(); ++ci)
            if (root.getChild(ci).getType() == juce::Identifier("Rhythm"))
                ++n;
        n = juce::jlimit(1, SequencerEngine::MaxRhythms, n);
        sequencer.setNumRhythms(n);

        const int oldN2 = numActiveRhythms.load(std::memory_order_acquire);
        if (n < oldN2)
        {
            numActiveRhythms.store(n, std::memory_order_release);
            for (int i = n; i < oldN2; ++i)
            {
                voiceEngines[i].reset();
                midiEngines[i] = MidiOutputEngine{};
                // Wipe mixer channel state and sample-path memory so the now-inactive
                // slot does not retain stale fader/sidechain/sample data from the
                // pre-load session. Without this, opening the mixer after loading a
                // smaller preset would still show the previous tenant's settings on
                // the dimmed slots.
                mixerEngine.channels[i] = MixerEngine::ChannelState{};
                if (i < loadedSamplePaths.size())
                    loadedSamplePaths.set(i, juce::String());
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
        int rhythmIdx = 0;
        for (int ci = 0; ci < root.getNumChildren() && rhythmIdx < n; ++ci)
        {
            auto rTree = root.getChild(ci);
            if (rTree.getType() != juce::Identifier("Rhythm")) continue;
            const int i = rhythmIdx++;

            const juce::String dstPrefix = "r" + juce::String(i) + "_";
            for (int j = 0; kRhythmSuffixes[j] != nullptr; ++j)
            {
                juce::Identifier propId { kRhythmSuffixes[j] };
                if (rTree.hasProperty(propId))
                    if (auto* param = apvts.getParameter(dstPrefix + kRhythmSuffixes[j]))
                        param->setValueNotifyingHost((float)rTree.getProperty(propId));
            }

            const juce::String dstChPrefix = "ch" + juce::String(i) + "_";
            for (int j = 0; kChannelSuffixes[j] != nullptr; ++j)
            {
                juce::Identifier chPropId { "ch_" + juce::String(kChannelSuffixes[j]) };
                if (rTree.hasProperty(chPropId))
                    if (auto* param = apvts.getParameter(dstChPrefix + kChannelSuffixes[j]))
                        param->setValueNotifyingHost((float)rTree.getProperty(chPropId));
            }

            Rhythm& r = sequencer.getRhythm(i);

            // #237: restore modulators if the preset carries a Modulators child.
            // Same opt-in behaviour as applyRhythmPreset — legacy .muclid files
            // (no Modulators child per rhythm) leave the rhythm's existing
            // in-memory state in place, avoiding accidental destruction.
            if (auto rMods = rTree.getChildWithName("Modulators"); rMods.isValid())
            {
                clearModulators(r);
                deserialiseModulators(rMods, r);
            }

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
                    else
                    {
                        // Linked sample not found — clear any previously loaded sample
                        // on this slot so the RhythmPanel shows the missing indicator
                        // consistently and the user can click to relocate.
                        voiceEngines[i]->clearSample();
                        // loadedSamplePaths already set to samplePath above (line ~1999)
                    }
                }
            }
            else
            {
                // Preset rhythm has no sample at all — wipe any stale sample on this slot.
                voiceEngines[i]->clearSample();
            }

            // #240: force-sync APVTS → Rhythm so a preset that re-loads identical
            // values into a freshly-recreated slot (preset A → B → A pattern)
            // still populates r.voiceParams / r.genA.hits. JUCE skips listener
            // callbacks when setValueNotifyingHost is called with an unchanged
            // value, so parameterChanged → syncRhythmParam never fires.
            // Internally calls updatePattern + voiceEngines[i]->setParams.
            forceSyncRhythmFromAPVTS(i);
        }

        // Restore global FX/mixer state if present (added in #123; older files omit this).
        for (int ci = 0; ci < root.getNumChildren(); ++ci)
        {
            auto child = root.getChild(ci);
            if (child.getType() != juce::Identifier("GlobalState")) continue;
            for (int gi = 0; kGlobalParams[gi] != nullptr; ++gi)
            {
                juce::Identifier propId { kGlobalParams[gi] };
                if (child.hasProperty(propId))
                    if (auto* param = apvts.getParameter(kGlobalParams[gi]))
                        param->setValueNotifyingHost((float)child.getProperty(propId));
            }
            break;
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
