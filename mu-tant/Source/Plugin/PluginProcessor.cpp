#include "Plugin/PluginProcessor.h"
#include "Plugin/PluginEditor.h"
#include "Audio/Scales.h"
#include "Modulation/MuTantModDest.h"
#include "Modulation/ModulatorSerialise.h"   // mu-core: shared modulator (de)serialise
#include "Sequencer/GatePatternSerialise.h"  // mu-tant: gate (de)serialise (also used by tests)
#include "Plugin/HostTransport.h"            // mu-core: shared host-playhead reader

#include <thread>

namespace mu_tant
{

namespace
{
    // Filter-cutoff proportion-space helpers — same skew as mu-clid (cutoff range
    // 20..20000 Hz, skewForCentre 640 Hz → skewFactor ≈ 0.2). Modulation runs in
    // proportion space so a given depth always sweeps the same visual arc of the
    // knob regardless of the base cutoff setting (matches depthScaleFor "filter.cutoff"
    // scale = 1.0 in ModulationMatrix).
    inline float tantPropFromCutoff(float hz)
    {
        return std::pow(juce::jlimit(0.0f, 1.0f, (hz - 20.0f) / 19980.0f), 0.2f);
    }
    inline float tantCutoffFromProp(float p)
    {
        return 20.0f + 19980.0f * std::pow(juce::jlimit(0.0f, 1.0f, p), 5.0f);
    }

    // (isValidModDest moved to PluginProcessor_Preset.cpp — used only by preset I/O.)

    // Modulation destinations in kModDestTable order, each paired with the per-voice APVTS
    // base id whose NormalisableRange + atom back it. mu_mod::resolveLane seeds each dest in
    // proportion space (range.convertTo0to1 of the atom) and writes the modulated value back
    // in the param's own units. Order MUST match kModDestTable so saved-assignment ids line up
    // with the map keys the matrix reads — the D_* enum below indexes the resolved `out` array.
    struct TantModParam { const char* destId; const char* apvtsBase; };
    constexpr TantModParam kTantModParams[] = {
        { "osc1.octave", "o1_oct" }, { "osc1.semi", "o1_semi" }, { "osc1.fine", "o1_fine" }, { "osc1.pos", "o1_pos" }, { "osc1.penv.prop", "o1_penv_depth" },
        { "osc2.octave", "o2_oct" }, { "osc2.semi", "o2_semi" }, { "osc2.fine", "o2_fine" }, { "osc2.pos", "o2_pos" }, { "osc2.penv.prop", "o2_penv_depth" },
        { "xmod.index", "xmod_index" }, { "xmod.depth", "xmod_depth" }, { "xmod.ssb", "xmod_ssb" },
        { "osc1.level", "o1_lvl" },  { "osc2.level", "o2_lvl" }, { "noise.level", "noise_lvl" },
        { "filter.cutoff", "flt_cut" }, { "filter.resonance", "flt_res" },
        { "filter.drive.prop", "flt_drv" }, { "filter.locut.prop", "flt_lo_cut" }, { "filter.env.prop", "flt_env_depth" },
        { "filter2.cutoff.prop", "flt2_cut" }, { "filter2.resonance.prop", "flt2_res" },
        { "filter2.drive.prop", "flt2_drv" }, { "filter2.locut.prop", "flt2_lo_cut" }, { "filter2.env.prop", "flt2_env_depth" },
        { "level", "level" },
        { "insert.p1", "insP1" }, { "insert.p2", "insP2" }, { "insert.p3", "insP3" }, { "insert.p4", "insP4" },
    };
    static_assert((int) std::size(kTantModParams) == kModDestCount,
                  "kTantModParams must pair every kModDestTable destination 1:1");

    // Index of each resolved value in the resolveLane `out` array (kTantModParams order).
    enum { D_o1Oct = 0, D_o1Semi, D_o1Fine, D_o1Pos, D_o1Penv,
           D_o2Oct, D_o2Semi, D_o2Fine, D_o2Pos, D_o2Penv,
           D_xmIndex, D_xmDepth, D_xmSsb, D_o1Lvl, D_o2Lvl, D_nzLvl,
           D_fCut, D_fRes, D_fDrv, D_fLoCut, D_fEnv,
           D_f2Cut, D_f2Res, D_f2Drv, D_f2LoCut, D_f2Env, D_level,
           D_insP1, D_insP2, D_insP3, D_insP4 };

    // Compile-time integrity guard: pin kTantModParams' row order to kModDestTable (the
    // persisted assignment ids) AND to the D_* indices, so a reorder/insert that would seed
    // resolveLane with the wrong atom/range for a dest id fails to COMPILE rather than silently
    // mis-routing modulation. A constexpr static_assert beats a runtime test here — the tables
    // are file-local, and the check can never be forgotten or skipped.
    constexpr bool cstrEq(const char* a, const char* b)
    {
        while (*a != '\0' && *a == *b) { ++a; ++b; }
        return *a == *b;
    }
    constexpr bool tantModParamsAligned()
    {
        for (int i = 0; i < kModDestCount; ++i)
            if (! cstrEq(kTantModParams[i].destId, kModDestTable[i].id))
                return false;
        return true;
    }
    static_assert(tantModParamsAligned(),
                  "kTantModParams row order must match kModDestTable (saved-assignment ids)");
    // Representative D_* ↔ destination pins (catch an enum/table desync independent of the loop).
    static_assert(cstrEq(kTantModParams[D_o1Semi].destId, "osc1.semi"),     "D_o1Semi desync");
    static_assert(cstrEq(kTantModParams[D_fCut].destId,   "filter.cutoff"), "D_fCut desync");
    static_assert(cstrEq(kTantModParams[D_level].destId,  "level"),         "D_level desync");
    static_assert(cstrEq(kTantModParams[D_insP4].destId,  "insert.p4"),     "D_insP4 desync");
}


PluginProcessor::PluginProcessor()
    : ProcessorBase(BusesProperties()
                        .withInput ("Sidechain", juce::AudioChannelSet::stereo(), false)
                        .withOutput("Output",    juce::AudioChannelSet::stereo(), true),
                    createParameterLayout(),
                    juce::Identifier("MuTantState"))
{
    // Register mu-tant's modulation depth scales with mu-core before any audio runs
    // (once, message thread) — keeps mu-core from enumerating mu-tant param ids.
    registerDepthScales();

    // Persistent settings file (UI scale, future toggles).
    {
        juce::PropertiesFile::Options opts;
        opts.applicationName     = "muTant";
        opts.filenameSuffix      = "xml";
        opts.folderName          = "TDP";
        opts.osxLibrarySubFolder = "Application Support";
        auto settingsFile = opts.getDefaultFile();
        settingsFile.getParentDirectory().createDirectory();
        appSettings = std::make_unique<juce::PropertiesFile>(settingsFile, opts);
    }
    // Restore persisted UI scale so a fresh open uses the last-selected size.
    {
        const double stored = appSettings->getDoubleValue("uiScale", (double) kUiScaleMedium);
        uiScale = juce::jlimit(kUiScaleMedium, kUiScaleLarge, (float) stored);
    }
    // Restore persisted MIDI-clock-sync prefs (standalone external clock). Mirrors mu-clid.
    midiClockSync.setEnabled (appSettings->getBoolValue("midiSyncEnabled",  false));
    midiClockSync.setMessages(appSettings->getIntValue ("midiSyncMessages", 2));

    // Restore persisted MIDI Note mode (0 = Free, 1 = Note). Seed the audio-thread
    // gate so a fresh open in Note mode starts silent (closed) until a note arrives.
    midiNoteMode.store(appSettings->getIntValue("midiNoteMode", 0), std::memory_order_relaxed);
    lastNoteMode = midiNoteMode.load(std::memory_order_relaxed);
    noteGateGain = (lastNoteMode == 1) ? 0.0f : 1.0f;

    // Check license file — after appSettings so getContentDir() resolves.
    licenseInfo = mu_core::LicenseManager::check(getContentDir(),
                                                 kLicenseProductId,
                                                 kLicenseFilename,
                                                 kLicensePublicKey);

    // Online activation (Lemon Squeezy). Startup uses a LOCAL-only check so plugin load never
    // blocks on the network; the overlay's activateOnlineFn does the real network activate.
    if (mu_core::OnlineActivation::hasLocalActivation(getContentDir(), kActivationFilename))
        onlineActivated.store(true, std::memory_order_relaxed);
    activateOnlineFn = [this](const juce::String& key) {
        auto o = mu_core::OnlineActivation::activate(getContentDir(), kActivationFilename, key);
        if (o.ok) onlineActivated.store(true, std::memory_order_relaxed);
        return o;
    };

    // MIDI program-change preset maps (Ch 1-8 → per-voice .muPattern, Ch 9 →
    // full .muTant preset). The scan/drain machinery + the editor panels live in
    // mu-core; we only point the maps at the plugin's settings dir + load them.
    // Mirrors mu-clid.
    {
        const auto settingsDir = appSettings->getFile().getParentDirectory();
        midiPresetMap    .setStorageFile(settingsDir.getChildFile("muTant_midiPresets.json"));
        midiFullPresetMap.setStorageFile(settingsDir.getChildFile("muTant_midiFullPresets.json"));
        midiPresetMap    .load();
        midiFullPresetMap.load();
    }

    bank.loadFactoryBank();      // multi-table, mip-mapped factory wavetable bank
    for (int v = 0; v < kMaxVoices; ++v)
    {
        voices[(size_t) v] = std::make_unique<VoiceEngine>();
        voices[(size_t) v]->setBank(&bank);
        osc1UserIndex[(size_t) v].store(-1);   // no user wavetable → factory selection
        osc2UserIndex[(size_t) v].store(-1);
    }

    cacheParamPointers();        // resolve all APVTS atomics once (audio thread reads these)
    refreshAllPitchQuantFlags(); // seed the stepped-pitch flags (default state has no modulators)

    // Pre-allocate modParamValues entries once so the audio-thread map
    // never allocates — keys live in static storage (the kModDestTable string
    // literals), making string_view safe for the lifetime of the entries.
    modParamValues.reserve((size_t) kModDestCount);
    for (int i = 0; i < kModDestCount; ++i)
        modParamValues[kModDestTable[i].id] = 0.0f;
    // Insert slot keys use the same IDs as mu-clid (handled by depthScaleFor).
    // Pre-allocated above via kModDestTable; this comment documents the intent.

    // Render hook for the shared MixerEngine — captures only `this`, so no
    // per-block std::function construction (which would allocate). The per-block
    // transport snapshot it reads lives in the blk* members, set in processBlock.
    renderVoiceCb = [this](int v, juce::AudioBuffer<float>& buf, int n)
    {
        // Active voices render normally; a voice past the active count that is still
        // retiring (count-reducing swap) fades its old tail; anything else is silent.
        if (v < numVoices.load(std::memory_order_relaxed))
            renderVoice(v, buf, n);
        else if (retiring[(size_t) v].samplesLeft.load(std::memory_order_acquire) > 0)
            renderRetiringVoice(v, buf, n);
        else
            buf.clear();
    };

    // Mixer + FX state is listener-synced into mixerEngine/fxChain (channel strips,
    // sends, sidechain, returns, master, FX slots) — mirrors mu-clid. Seed it now
    // since JUCE doesn't fire parameterChanged on construction.
    registerFxListeners();
    syncAllFxParams();
}

PluginProcessor::~PluginProcessor()
{
    cancelPendingUpdate();   // no hot-swap / MIDI drain fires into a half-destroyed processor
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*>(p))
        {
            const juce::String id = rp->getParameterID();
            if (id.startsWith("ch") || mu_mixfx::isGlobalFxParamId(id))
                apvts.removeParameterListener(id, this);
        }
}

void PluginProcessor::registerFxListeners()
{
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*>(p))
        {
            const juce::String id = rp->getParameterID();
            if (id.startsWith("ch") || mu_mixfx::isGlobalFxParamId(id))
                apvts.addParameterListener(id, this);
        }
}

void PluginProcessor::syncAllFxParams()
{
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*>(p))
        {
            const juce::String id = rp->getParameterID();
            if (id.startsWith("ch") || mu_mixfx::isGlobalFxParamId(id))
                if (auto* a = apvts.getRawParameterValue(id))
                    syncGlobalFxParam(id, a->load());
        }
}

void PluginProcessor::parameterChanged(const juce::String& id, float v)
{
    if (id.startsWith("ch") || mu_mixfx::isGlobalFxParamId(id))
        syncGlobalFxParam(id, v);
}

bool PluginProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    // Sidechain input: at most one, must be stereo or disabled.
    const auto& ins = layouts.inputBuses;
    if (ins.size() > 1) return false;
    if (ins.size() == 1 && ins.getReference(0) != juce::AudioChannelSet::stereo()
                        && ins.getReference(0) != juce::AudioChannelSet::disabled())
        return false;
    // Main output: must be stereo.
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void PluginProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    for (auto& v : voices)
        if (v) v->prepare(sampleRate, samplesPerBlock);

    for (auto& ins : inserts)
        ins.prepare(sampleRate, samplesPerBlock);

    mixerEngine.prepare(sampleRate, samplesPerBlock);
    fxChain.prepare(sampleRate, samplesPerBlock);   // shared Effect/Delay/Reverb rack

    // Cancel any in-flight retire fade — its ramp length was derived from the old SR.
    for (auto& r : retiring)
        r.samplesLeft.store(0, std::memory_order_relaxed);
}

void PluginProcessor::cacheParamPointers()
{
    auto P = [this](const juce::String& id) { return apvts.getRawParameterValue(id); };
    globalPtrs.root    = P("root");
    globalPtrs.scale   = P("scale");
    mstrLoopPtr        = P("mstrLoop");   // master-loop length, read RT-safely in processBlock

    for (int v = 0; v < kMaxVoices; ++v)
    {
        auto vid = [v](const char* base) { return voiceParamId(v, base); };
        auto& p = voicePtrs[(size_t) v];
        p.o1Oct  = P(vid("o1_oct"));  p.o1Semi = P(vid("o1_semi")); p.o1Fine = P(vid("o1_fine")); p.o1Pos = P(vid("o1_pos")); p.o1Wt = P(vid("o1_wt"));
        p.o2Oct  = P(vid("o2_oct"));  p.o2Semi = P(vid("o2_semi")); p.o2Fine = P(vid("o2_fine")); p.o2Pos = P(vid("o2_pos")); p.o2Wt = P(vid("o2_wt"));
        p.xmodPhaseMode = P(vid("xmod_phaseMode")); p.xmodIndex = P(vid("xmod_index")); p.sync = P(vid("sync"));
        p.xmodFdbk = P(vid("xmod_fdbk")); p.xmodAmpMode = P(vid("xmod_ampMode")); p.xmodDepth = P(vid("xmod_depth")); p.xmodSsb = P(vid("xmod_ssb"));
        p.o1Lvl  = P(vid("o1_lvl"));  p.o2Lvl  = P(vid("o2_lvl"));  p.noiseLvl = P(vid("noise_lvl")); p.noiseType = P(vid("noise_type"));
        p.fltType= P(vid("flt_type")); p.fltCut = P(vid("flt_cut")); p.fltRes = P(vid("flt_res")); p.fltEnvDepth = P(vid("flt_env_depth")); p.fltDrv = P(vid("flt_drv")); p.fltLoCut = P(vid("flt_lo_cut"));
        p.flt2Type=P(vid("flt2_type"));p.flt2Cut=P(vid("flt2_cut"));p.flt2Res=P(vid("flt2_res"));p.flt2EnvDepth=P(vid("flt2_env_depth"));p.flt2Drv=P(vid("flt2_drv"));p.flt2LoCut=P(vid("flt2_lo_cut"));
        p.fltSeries = P(vid("flt_series"));
        p.o1PenvDepth = P(vid("o1_penv_depth")); p.o2PenvDepth = P(vid("o2_penv_depth"));
        p.level  = P(vid("level"));
        p.gateGap= P(vid("gate_gap"));p.gateBypass = P(vid("gate_bypass"));
        p.drvChar= P(vid("drvChar")); p.insP1  = P(vid("insP1")); p.insP2 = P(vid("insP2")); p.insP3 = P(vid("insP3")); p.insP4 = P(vid("insP4"));
    }

    // Build the cached modulation routing for resolveLane: dest ids + each param's range are
    // voice-independent (cached once); the backing atomics differ per voice. Using the param's
    // own NormalisableRange (via getParameterRange) means the filter-cutoff proportion seed
    // exactly matches its skewed knob — no separate tantPropFromCutoff to drift.
    modParamValues.reserve((size_t) kNumModDests + 4);   // pre-size → no audio-thread rehash
    for (int i = 0; i < kNumModDests; ++i)
    {
        modDestIds[(size_t) i]    = kTantModParams[i].destId;
        modDestRanges[(size_t) i] = apvts.getParameterRange(voiceParamId(0, kTantModParams[i].apvtsBase));
        modParamValues.emplace(std::string_view(kTantModParams[i].destId), 0.0f);   // pre-insert key
        for (int v = 0; v < kMaxVoices; ++v)
        {
            modDestAtoms[(size_t) v][(size_t) i] =
                apvts.getRawParameterValue(voiceParamId(v, kTantModParams[i].apvtsBase));
            jassert(modDestAtoms[(size_t) v][(size_t) i] != nullptr);   // catch an apvtsBase drift
        }
    }
}

VoiceConfig PluginProcessor::readConfig(int voiceIndex) const
{
    const auto& p = voicePtrs[(size_t) voiceIndex];
    VoiceConfig c;
    // Tonal centre is global.
    c.root         = (int) globalPtrs.root->load();
    c.scaleIndex     = (int) globalPtrs.scale->load();
    // Per-voice osc pitch (integer) + position.
    c.osc1Octave   = (int) p.o1Oct->load();
    c.osc1Semi     = (int) p.o1Semi->load();
    c.osc1Fine     = (int) p.o1Fine->load();
    c.osc1Pos      = p.o1Pos->load();
    c.osc2Octave   = (int) p.o2Oct->load();
    c.osc2Semi     = (int) p.o2Semi->load();
    c.osc2Fine     = (int) p.o2Fine->load();
    c.osc2Pos      = p.o2Pos->load();
    // Wavetable: a loaded user table (resolved bank index) overrides the factory
    // o{1,2}_wt selection; -1 → use the factory index.
    {
        const int u1 = osc1UserIndex[(size_t) voiceIndex].load();
        const int u2 = osc2UserIndex[(size_t) voiceIndex].load();
        c.osc1Wavetable = (u1 >= 0) ? u1 : (int) p.o1Wt->load();
        c.osc2Wavetable = (u2 >= 0) ? u2 : (int) p.o2Wt->load();
    }
    // X-Mod — Lane A (phase/index) + Lane B (amplitude/multiply).
    c.xmodPhaseMode = (int) p.xmodPhaseMode->load();   // 0 FM, 1 PM, 2 TZFM
    c.xmodIndex     = p.xmodIndex->load() * 0.01f;     // 0..100 → 0..1
    c.sync          = p.sync->load() > 0.5f;
    c.xmodFeedback  = p.xmodFdbk->load() > 0.5f;
    c.xmodAmpMode   = (int) p.xmodAmpMode->load();     // 0 AM, 1 RM, 2 SSB
    c.xmodDepth     = p.xmodDepth->load() * 0.01f;     // -100..100 → -1..1
    c.xmodSsbHz     = p.xmodSsb->load();               // Hz
    // Per-source levels.
    c.osc1LevelDb  = p.o1Lvl->load();
    c.osc2LevelDb  = p.o2Lvl->load();
    c.noiseLevelDb = p.noiseLvl->load();
    c.noiseType    = (int) p.noiseType->load();
    c.filterType      = (int) p.fltType->load();
    c.filterCutoff    = p.fltCut->load();
    c.filterRes       = p.fltRes->load();
    c.filterEnvDepth  = p.fltEnvDepth->load();
    c.filterDrive     = p.fltDrv->load();
    c.filterLowCutHz  = p.fltLoCut->load();
    c.filter2Type     = (int) p.flt2Type->load();
    c.filter2Cutoff   = p.flt2Cut->load();
    c.filter2Res      = p.flt2Res->load();
    c.filter2EnvDepth = p.flt2EnvDepth->load();
    c.filter2Drive    = p.flt2Drv->load();
    c.filter2LowCutHz = p.flt2LoCut->load();
    c.filterSeries    = p.fltSeries->load() > 0.5f;
    c.osc1PitchEnvDepth = p.o1PenvDepth->load();
    c.osc2PitchEnvDepth = p.o2PenvDepth->load();
    c.levelDb         = p.level->load();
    return c;
}

void PluginProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();

    // Preserve the DAW sidechain, then clear (the SC input bus shares buffer channels
    // with the output, so a bare clear would wipe it → no ducking / dead GR). Shared.
    captureSidechainAndClear(buffer);

    // MIDI program-change → preset load. Enqueue matching PCs (Ch 1-8 per-voice,
    // Ch 9 full) into the lock-free FIFO; handleAsyncUpdate drains them. Done
    // before the voicesTryLock so PCs aren't dropped during a voice add/remove.
    if (scanMidiProgramChanges(midiMessages))
        triggerAsyncUpdate();

    // Note mode (gate + pitch-track): scan note on/off into the held-note stack so
    // renderVoice can pitch-track the held note. The amplitude gate is applied to the
    // final mix after the render (applyNoteModeGate). No-op in Free mode.
    scanNoteMode(midiMessages);

    // External MIDI clock (standalone): scan the buffer + advance the clock estimate. Returns
    // the start-of-block beat (0 when disabled); the transport derivation below slaves to it.
    const double midiClockBeat = midiClockSync.process(midiMessages, numSamples, currentSampleRate);

    // Per-block transport snapshot for the render hook (audio-thread-only members, read by
    // renderVoice). Family standard (mu-core HostTransport): consult the playhead first.
    //   • Plugin: host drives play + BPM (mu-Tant keeps its own free-running beat, by design).
    //   • Standalone + mu-link: the injected MuLinkPlayHead supplies a beat POSITION → slave
    //     the beat to it (phase-locked to the mu-link master clock).
    //   • Standalone, free-running: no playhead → the internal play button drives.
    // No voice data here, so this — and the beat advance + hot-swap boundary check at the end —
    // run OUTSIDE the render lock: the transport keeps advancing even while a preset hot-swap
    // commits on the message thread (no transport freeze).
    double bpm = internalBpm.load(std::memory_order_relaxed);
    const auto ht = mu_core::readHostTransport(getPlayHead());

    bool   slaved     = false;
    double slavedBeat = 0.0;
    if (wrapperType != wrapperType_Standalone)
    {
        if (ht.bpm > 0.0) bpm = ht.bpm;
        playing.store(ht.playing, std::memory_order_relaxed);   // UI timer reads this
        blkPlaying = ht.playing;
    }
    else if (ht.hasPosition)
    {
        // Slaved to mu-link. Wrap the master ppq into mu-Tant's bounded beat space (the same
        // ceiling the internal transport uses) so the gate + hot-swap boundary see positions
        // exactly as they do free-running — only the SOURCE of the beat changes.
        if (ht.bpm > 0.0) bpm = ht.bpm;
        playing.store(ht.playing, std::memory_order_relaxed);
        blkPlaying = ht.playing;
        slaved     = true;
        slavedBeat = std::fmod(ht.ppqPosition, (double) GatePattern::kMaxPatternBars * 4.0);
    }
    else if (midiClockSync.isEnabled() && midiClockSync.isPlaying())
    {
        // Slaved to external MIDI clock (standalone, MIDI-in). Same bounded beat space as the
        // others — only the SOURCE of the beat changes (mu-link takes priority above when attached).
        bpm = midiClockSync.getBpm();
        playing.store(true, std::memory_order_relaxed);
        blkPlaying = true;
        slaved     = true;
        slavedBeat = std::fmod(midiClockBeat, (double) GatePattern::kMaxPatternBars * 4.0);
    }
    else
    {
        blkPlaying = playing.load(std::memory_order_relaxed);
    }

    blkBeatStart      = slaved ? slavedBeat : internalBeatPos.load(std::memory_order_relaxed);
    blkBeatsPerSample = (bpm / 60.0) / currentSampleRate;

    // Render → gate → insert → mixer through the shared path (engine→insert→mixer).
    // Guarded by a try-lock ONLY against a voice add/remove data shift (the message
    // thread holds voicesLock for those). On contention, leave the buffer silent for
    // this block — a sub-ms gap during a structural edit. A preset hot-swap does NOT
    // take this lock (its data lands through the per-structure editLock / modLock +
    // atomic params), so a swap commit never silences the render here.
    {
        const juce::ScopedTryLock voicesTryLock(voicesLock);
        if (voicesTryLock.isLocked())
        {
            const int numActiveVoices = numVoices.load(std::memory_order_relaxed);

            // Extend the rendered channel count to cover any voices still fading out
            // from a count-reducing swap, so the mixer keeps mixing their tail.
            int renderCount = numActiveVoices;
            for (int v = numActiveVoices; v < kMaxVoices; ++v)
                if (retiring[(size_t) v].samplesLeft.load(std::memory_order_acquire) > 0)
                    renderCount = v + 1;

            // (Sidechain already captured at the top of processBlock, before the clear.)
            processCoreBlock(buffer, nullptr, renderCount, numSamples, bpm,
                             nullptr, nullptr, nullptr, &renderVoiceCb);
        }
    }

    // Note mode: gate the whole mix on the held→silent edge with a short anti-click
    // fade (runs even if the render was skipped on lock contention — buffer is silent,
    // the ramp still tracks). No-op in Free mode.
    applyNoteModeGate(buffer, numSamples);

    // Advance the transport beat. Wrap at the maximum pattern length (64 beats =
    // 16 bars in 4/4) to keep floating-point precision bounded. Each GatePattern
    // wraps internally at its own patternLengthBars; the global counter just needs a
    // ceiling. Runs unconditionally (atomics only) so the transport never freezes.
    const double oldPos    = blkBeatStart;
    double       newPosRaw = oldPos;   // pre-ceiling advanced position for boundary detection
    if (blkPlaying)
    {
        newPosRaw = oldPos + blkBeatsPerSample * (double) numSamples;
        double pos = newPosRaw;
        constexpr double kMaxPatBeats = (double) GatePattern::kMaxPatternBars * 4.0; // 64
        if (pos >= kMaxPatBeats) pos -= kMaxPatBeats;
        internalBeatPos.store(pos, std::memory_order_relaxed);
    }

    // Hot-swap boundary check: a staged preset commits when its reference loop wraps
    // (or on the playing→stopped edge). Uses the RAW pre-ceiling position so the
    // loop-index test holds for lengths that don't divide 64. The reference loop
    // depends on the Hot-swap timing (SwapMode) + the master loop:
    //   • master-loop length in beats = mstrLoop steps / 4 (4 steps/beat); 0 = off.
    //   • per-voice swap: OnMasterLoop (+ a loop set) → master loop; else the voice's
    //     own gate-pattern boundary (so a swap can't hang when no master loop exists).
    //   • full preset: master loop when one is defined, else voice 0's gate boundary.
    const int    mlSteps         = mstrLoopPtr ? (int) mstrLoopPtr->load() * 16 : 0;
    const double masterLoopBeats = mlSteps > 0 ? (double) mlSteps / 4.0 : 0.0;
    const bool   onMasterLoop    = (swapModeAtomic.load(std::memory_order_relaxed) == 0)
                                   && masterLoopBeats > 0.0;

    std::array<double, VoiceHotSwapStager::kMaxVoices> voicePatBeats {};
    for (int v = 0; v < VoiceHotSwapStager::kMaxVoices; ++v)
    {
        const double gateBeats = (double) gatePatterns[(size_t) v].patternLengthBars * 4.0;
        voicePatBeats[(size_t) v] = onMasterLoop ? masterLoopBeats : gateBeats;
    }
    const double fullPatBeats = masterLoopBeats > 0.0
                                ? masterLoopBeats
                                : (double) gatePatterns[0].patternLengthBars * 4.0;
    if (hotSwapStager.checkBoundaries(numVoices.load(std::memory_order_relaxed),
                                      blkPlaying, wasPlaying,
                                      oldPos, newPosRaw, voicePatBeats, fullPatBeats))
        triggerAsyncUpdate();
    wasPlaying = blkPlaying;
}

void PluginProcessor::applyModulation(int v, VoiceConfig& cfg)
{
    // Resolve this voice's modulation through the shared mu-core range-based routing: seed each
    // destination's proportion from its APVTS atom, run the voice's matrix under a try-lock
    // (skipped on contention → the voice plays un-modulated this block), then write the
    // modulated values back in param units. On lock-miss or with no assignments the values
    // round-trip to the base config, so cfg + the live-arc snapshot stay correct either way.
    // The dests are now proportion-clamped (scale 1.0) — a full-depth mod sweeps the whole
    // range and clamps at the rails, instead of overflowing it as the old display-unit path did.
    float out[kNumModDests];
    mu_mod::resolveLane(&voiceSlots[(size_t) v], blkBeatStart, kNumModDests,
                        modDestIds.data(), modDestAtoms[(size_t) v].data(),
                        modDestRanges.data(), modParamValues, out);

    // Write the resolved values back into the voice config. Octave/fine pitch dests round to the
    // nearest step (the proportion round-trip isn't bit-exact, so a plain truncation could drop
    // a step); the rest map straight across, already range-clamped by resolveLane.
    cfg.osc1Octave   = juce::roundToInt(out[D_o1Oct]);
    cfg.osc1Fine     = juce::roundToInt(out[D_o1Fine]);
    cfg.osc1Pos      = out[D_o1Pos];
    cfg.osc2Octave   = juce::roundToInt(out[D_o2Oct]);
    cfg.osc2Fine     = juce::roundToInt(out[D_o2Fine]);
    cfg.osc2Pos      = out[D_o2Pos];

    // Pitch (semitones): a Stepped CS snaps the whole result to a semitone (melodies); a
    // smooth source glides via a fractional offset on top of the exact integer base.
    {
        const float o1Base = (float) voicePtrs[(size_t) v].o1Semi->load();
        const float o2Base = (float) voicePtrs[(size_t) v].o2Semi->load();
        const bool  o1Stepped = osc1SemiStepped[(size_t) v].load(std::memory_order_relaxed);
        const bool  o2Stepped = osc2SemiStepped[(size_t) v].load(std::memory_order_relaxed);
        if (o1Stepped) { cfg.osc1Semi = juce::roundToInt(out[D_o1Semi]); cfg.osc1SemiMod = 0.0f; }
        else           { cfg.osc1Semi = juce::roundToInt(o1Base);        cfg.osc1SemiMod = out[D_o1Semi] - o1Base; }
        if (o2Stepped) { cfg.osc2Semi = juce::roundToInt(out[D_o2Semi]); cfg.osc2SemiMod = 0.0f; }
        else           { cfg.osc2Semi = juce::roundToInt(o2Base);        cfg.osc2SemiMod = out[D_o2Semi] - o2Base; }
    }
    cfg.xmodIndex    = out[D_xmIndex] * 0.01f;   // param 0..100 → engine 0..1
    cfg.xmodDepth    = out[D_xmDepth] * 0.01f;   // param -100..100 → engine -1..1
    cfg.xmodSsbHz    = out[D_xmSsb];             // Hz (direct)
    cfg.osc1LevelDb  = out[D_o1Lvl];
    cfg.osc2LevelDb  = out[D_o2Lvl];
    cfg.noiseLevelDb = out[D_nzLvl];
    cfg.osc1PitchEnvDepth = out[D_o1Penv];
    cfg.osc2PitchEnvDepth = out[D_o2Penv];
    cfg.filterCutoff   = out[D_fCut];
    cfg.filterRes      = out[D_fRes];
    cfg.filterDrive    = out[D_fDrv];
    cfg.filterLowCutHz = out[D_fLoCut];
    cfg.filterEnvDepth = out[D_fEnv];
    cfg.filter2Cutoff  = out[D_f2Cut];
    cfg.filter2Res     = out[D_f2Res];
    cfg.filter2Drive   = out[D_f2Drv];
    cfg.filter2LowCutHz= out[D_f2LoCut];
    cfg.filter2EnvDepth= out[D_f2Env];
    cfg.levelDb        = out[D_level];

    // Publish post-matrix values for the UI live-arc indicators (same units as the bound knobs:
    // filter cutoff in Hz, xmod 0..100, levels in dB). Filter 2 + insert P1-4 are read by the
    // audio path from `out` / modParamValues; the two filter-2 dests have no live-arc.
    auto& snap = voiceSnap[(size_t) v];
    snap[mu_tant::kTantSnapOsc1Octave] .store(out[D_o1Oct]);
    snap[mu_tant::kTantSnapOsc1Semi]   .store(out[D_o1Semi]);
    snap[mu_tant::kTantSnapOsc1Fine]   .store(out[D_o1Fine]);
    snap[mu_tant::kTantSnapOsc1Pos]    .store(out[D_o1Pos]);
    snap[mu_tant::kTantSnapOsc2Octave] .store(out[D_o2Oct]);
    snap[mu_tant::kTantSnapOsc2Semi]   .store(out[D_o2Semi]);
    snap[mu_tant::kTantSnapOsc2Fine]   .store(out[D_o2Fine]);
    snap[mu_tant::kTantSnapOsc2Pos]    .store(out[D_o2Pos]);
    snap[mu_tant::kTantSnapXModIndex]  .store(out[D_xmIndex]);
    snap[mu_tant::kTantSnapXModDepth]  .store(out[D_xmDepth]);
    snap[mu_tant::kTantSnapXModSsb]    .store(out[D_xmSsb]);
    snap[mu_tant::kTantSnapOsc1Level]  .store(out[D_o1Lvl]);
    snap[mu_tant::kTantSnapOsc2Level]  .store(out[D_o2Lvl]);
    snap[mu_tant::kTantSnapNoiseLevel] .store(out[D_nzLvl]);
    snap[mu_tant::kTantSnapFilterCutoff].store(out[D_fCut]);   // actual Hz
    snap[mu_tant::kTantSnapFilterRes]  .store(out[D_fRes]);
    snap[mu_tant::kTantSnapLevel]      .store(out[D_level]);
    snap[mu_tant::kTantSnapInsP1]      .store(out[D_insP1]);
    snap[mu_tant::kTantSnapInsP2]      .store(out[D_insP2]);
    snap[mu_tant::kTantSnapInsP3]      .store(out[D_insP3]);
    snap[mu_tant::kTantSnapInsP4]      .store(out[D_insP4]);
}

void PluginProcessor::applyFilterEnvelope(int v, VoiceConfig& cfg, int numSamples)
{
    // Filter pattern envelopes add to cfg.filterCutoff in proportion space.
    // envelope value 0 → no change from base; 1 at depth=1 → fully open.
    // Apply the same kMinAttackMs rise-limiting as the gate to prevent clicks
    // when gate and filter envelopes are out of sync.
    auto& fPat = filterPatterns[(size_t) v];
    if (blkPlaying && fPat.hasEnvelopes.load(std::memory_order_relaxed))
    {
        // The filter cutoff is a per-BLOCK value, so the de-click slew (a per-sample
        // rate from kMinAttackMs) must be scaled by the block length — otherwise the
        // rising edge is limited ~blockSize× too slowly and a per-bar decay envelope
        // never re-opens the filter (it would take seconds to reach its peak).
        const float maxRise = (currentSampleRate > 0.0 && GatePattern::kMinAttackMs > 0.0f)
                            ? (float) ((double) juce::jmax(1, numSamples)
                                       / ((double) GatePattern::kMinAttackMs * 0.001 * currentSampleRate))
                            : 1.0f;
        bool fExpected = false;
        if (fPat.editLock.compare_exchange_strong(fExpected, true, std::memory_order_acquire))
        {
            fPat.resetGateCache();
            const float target   = fPat.gateAt(blkBeatStart, 0.0f);
            // Slew-limit the filter envelope rising edge (same as gate)
            fPat.filterLevel = (target > fPat.filterLevel)
                             ? std::min(target, fPat.filterLevel + maxRise)
                             : target;
            const float depth    = juce::jlimit(-1.0f, 1.0f, cfg.filterEnvDepth);
            const float baseProp = tantPropFromCutoff(cfg.filterCutoff);
            // baseProp + depth*filterLevel: envelope adds on top of base cutoff.
            //   depth=1, filterLevel=0 → baseProp (no change)
            //   depth=1, filterLevel=1 → clamped to 1.0 (fully open)
            //   depth=-1,filterLevel=1 → baseProp-1 (clamped toward 20 Hz)
            const float modProp  = juce::jlimit(0.0f, 1.0f, baseProp + depth * fPat.filterLevel);
            cfg.filterCutoff = juce::jlimit(20.0f, 20000.0f, tantCutoffFromProp(modProp));

            // Apply the same envelope to Filter 2 with its own depth.
            const float depth2   = juce::jlimit(-1.0f, 1.0f, cfg.filter2EnvDepth);
            const float baseProp2 = tantPropFromCutoff(cfg.filter2Cutoff);
            const float modProp2  = juce::jlimit(0.0f, 1.0f, baseProp2 + depth2 * fPat.filterLevel);
            cfg.filter2Cutoff = juce::jlimit(20.0f, 20000.0f, tantCutoffFromProp(modProp2));

            fPat.editLock.store(false, std::memory_order_release);
        }
        // On lock contention: use un-modulated cutoffs (edit-time blip)
    }
    else if (!blkPlaying)
    {
        fPat.filterLevel = 1.0f;  // reset when stopped
    }
}

void PluginProcessor::applyPitchEnvelope(int v, VoiceConfig& cfg)
{
    // Pitch pattern envelope value (0..1) × depth (±24 semitones) shifts
    // osc1/osc2 pitch before VoiceEngine processes it this block.
    auto& pPat = pitchPatterns[(size_t) v];
    if (blkPlaying && pPat.hasEnvelopes.load(std::memory_order_relaxed))
    {
        bool pExpected = false;
        if (pPat.editLock.compare_exchange_strong(pExpected, true, std::memory_order_acquire))
        {
            pPat.resetGateCache();
            const float envLevel = pPat.gateAt(blkBeatStart, 0.0f);
            // Depth comes from cfg (so it tracks any pitch-env-depth modulation applied
            // in applyModulation), not the raw param.
            const float d1 = cfg.osc1PitchEnvDepth;
            const float d2 = cfg.osc2PitchEnvDepth;
            // Fractional semitone offset — the envelope sweeps smoothly (no semitone stepping).
            cfg.osc1SemiMod += d1 * envLevel;
            cfg.osc2SemiMod += d2 * envLevel;
            pPat.editLock.store(false, std::memory_order_release);
        }
    }
}

bool PluginProcessor::pitchDestHasSteppedSource(const VoiceSlot& slot, const char* destId) const
{
    // A pitch destination is "stepped" if any assigned source is a Stepped ControlSequence.
    // Allocation-free: compares sourceId ("csN_output") against each CS id without building
    // strings, so it's safe to call on the audio thread (under the mod lock).
    for (const auto& a : slot.modulationMatrix.getAssignments())
    {
        if (a.destinationId != destId) continue;
        for (const auto& cs : slot.controlSequences)
        {
            if (cs.mode != ControlSequence::Mode::Stepped) continue;
            const auto& sid = cs.id;
            if (a.sourceId.size() == sid.size() + 7
                && a.sourceId.compare(0, sid.size(), sid) == 0
                && a.sourceId.compare(sid.size(), 7, "_output") == 0)
                return true;
        }
    }
    return false;
}

void PluginProcessor::refreshPitchQuantFlags(int v)
{
    if (v < 0 || v >= kMaxVoices) return;
    // Message-thread only (after any modulator mutation); the audio thread reads the
    // resulting atomics lock-free. Reading the assignments here is safe: the message
    // thread is the sole writer and the mutation has already completed.
    const auto& slot = voiceSlots[(size_t) v];
    osc1SemiStepped[(size_t) v].store(pitchDestHasSteppedSource(slot, "osc1.semi"), std::memory_order_relaxed);
    osc2SemiStepped[(size_t) v].store(pitchDestHasSteppedSource(slot, "osc2.semi"), std::memory_order_relaxed);
}

void PluginProcessor::refreshAllPitchQuantFlags()
{
    for (int v = 0; v < kMaxVoices; ++v) refreshPitchQuantFlags(v);
}

// One voice's full chain into the channel buffer: modulation → engine → gate →
// insert. Invoked by MixerEngine::processBlock (Phase 1) per active channel; the
// mixer then applies the channel strip + master. Renders even muted/un-soloed
// voices (the mixer mutes at the mix), matching mu-clid's render-then-mute model.
void PluginProcessor::renderVoice(int v, juce::AudioBuffer<float>& buf, int numSamples)
{
    VoiceConfig cfg = readConfig(v);
    applyModulation(v, cfg);
    applyFilterEnvelope(v, cfg, numSamples);
    applyPitchEnvelope(v, cfg);

    // Note mode pitch-track: transpose the voice so its tonal centre lands on the held
    // MIDI note. Free-mode centre = root + 12*kBaseOctave (octave 0, root → C3), so the
    // offset is held - that centre; scale intervals + per-osc oct/semi/fine still apply.
    if (midiNoteMode.load(std::memory_order_relaxed) == 1 && noteCurrentMidi >= 0)
        cfg.pitchOffsetSemis = (float) (noteCurrentMidi - (cfg.root + 12 * kBaseOctave));

    buf.clear();
    voices[(size_t) v]->setConfig(cfg);
    voices[(size_t) v]->process(buf, numSamples);

    // The per-voice gater shapes the (post-filter) voice output:
    //   bypassed             → raw drone passes (audition / configure)
    //   stopped              → silence (gate closed — nothing audible on load)
    //   playing, no envelopes→ silence (nothing drawn → nothing passes)
    //   playing, envelopes   → per-sample envelope gate
    // See applyGateBlock — the audio path + the audio test harness share it.
    const auto& vp = voicePtrs[(size_t) v];
    const float gateGap    = vp.gateGap->load() * 0.01f;   // 0..100% → 0..1
    const bool  gateBypass = vp.gateBypass->load() > 0.5f;
    float* gl = buf.getWritePointer(0);
    float* gr = buf.getNumChannels() > 1 ? buf.getWritePointer(1) : nullptr;
    applyGateBlock(gatePatterns[(size_t) v], gl, gr, numSamples, gateGap, gateBypass,
                   blkPlaying, blkBeatStart, blkBeatsPerSample, currentSampleRate);

    // Use modulated insert param values (modulation may have adjusted them).
    VoiceParams ip;
    ip.insertAlgo     = (int) vp.drvChar->load();
    ip.insertParam[0] = juce::jlimit(0.0f, 1.0f, modParamValues["insert.p1"]);
    ip.insertParam[1] = juce::jlimit(0.0f, 1.0f, modParamValues["insert.p2"]);
    ip.insertParam[2] = juce::jlimit(0.0f, 1.0f, modParamValues["insert.p3"]);
    ip.insertParam[3] = juce::jlimit(0.0f, 1.0f, modParamValues["insert.p4"]);
    inserts[(size_t) v].process(buf, numSamples, buf.getNumChannels(), ip);

    // Feed the sidebar spectrum glyph — capture post-insert audio on the audio thread.
    voiceRingBuffers[(size_t) v].write(buf, numSamples);
}

void PluginProcessor::renderRetiringVoice(int v, juce::AudioBuffer<float>& buf, int numSamples)
{
    auto& r = retiring[(size_t) v];
    const int left = r.samplesLeft.load(std::memory_order_acquire);
    if (left <= 0) { buf.clear(); return; }

    // Render the OLD voice (snapshot config) through its still-intact engine — same
    // oscillators + filter state as the instant before the swap, so the tail is faithful.
    buf.clear();
    voices[(size_t) v]->setConfig(r.config);
    voices[(size_t) v]->process(buf, numSamples);

    // Falling linear gain across the retire window: gain at sample i = (left - i) * gainStep,
    // clamped ≥ 0 so the slot is silent once the countdown is spent.
    for (int ch = 0; ch < buf.getNumChannels(); ++ch)
    {
        float* d = buf.getWritePointer(ch);
        for (int i = 0; i < numSamples; ++i)
            d[i] *= juce::jmax(0.0f, (float) (left - i) * r.gainStep);
    }

    // Run the OLD insert (snapshot algo + params) so an effect tail the new preset
    // drops still fades out instead of vanishing at the swap instant.
    VoiceParams ip;
    ip.insertAlgo     = r.insAlgo;
    ip.insertParam[0] = r.insP[0];
    ip.insertParam[1] = r.insP[1];
    ip.insertParam[2] = r.insP[2];
    ip.insertParam[3] = r.insP[3];
    inserts[(size_t) v].process(buf, numSamples, buf.getNumChannels(), ip);

    r.samplesLeft.store(juce::jmax(0, left - numSamples), std::memory_order_release);
}

juce::AudioProcessorEditor* PluginProcessor::createEditor()
{
    return new PluginEditor(*this);
}

void PluginProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    apvts.state.setProperty("numVoices", numVoices.load(), nullptr);
    apvts.state.setProperty("voiceColours", serialiseVoiceColours(), nullptr);
    writeVoiceDataToState();
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary(*xml, destData);
}

void PluginProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    // Validate the root tag before applying (mirrors mu-on / mu-toni): a corrupt or
    // foreign chunk that still parses as XML must NOT replace our APVTS root, or the
    // params detach. Only apply a tree tagged as our own state type.
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
        if (xml->hasTagName(apvts.state.getType()))
            applyFullPresetTree(juce::ValueTree::fromXml(*xml));   // host state restore — always immediate
}

// ── Dynamic voice management ─────────────────────────────────────────────────
int PluginProcessor::firstUnusedColourIndex() const
{
    const int n = numVoices.load();
    std::array<bool, kMaxVoices> used { };
    for (int i = 0; i < n; ++i)
    {
        const int c = voiceColourIndex[(size_t) i];
        if (c >= 0 && c < kMaxVoices) used[(size_t) c] = true;
    }
    for (int c = 0; c < kMaxVoices; ++c)
        if (! used[(size_t) c]) return c;
    return 0;
}

int PluginProcessor::addVoice()
{
    const juce::ScopedLock sl(voicesLock);
    const int n = numVoices.load();
    if (n >= kMaxVoices) return -1;
    hotSwapStager.cancelVoice(n);                   // the new slot must carry no stale staged swap
    resetVoiceSlot(n);                              // fresh defaults for the new slot
    voiceColourIndex[(size_t) n] = firstUnusedColourIndex();   // allocate its palette colour
    numVoices.store(n + 1);
    apvts.state.setProperty("numVoices", n + 1, nullptr);
    refreshPitchQuantFlags(n);   // fresh slot has no modulators → flags off
    return n;
}

void PluginProcessor::removeVoice(int idx)
{
    const juce::ScopedLock sl(voicesLock);
    const int n = numVoices.load();
    if (n <= 1 || idx < 0 || idx >= n) return;     // never remove the last voice

    // The down-shift renumbers every voice from idx upward, so any pending per-voice
    // hot-swap (keyed by index) would land on the wrong slot — drop them all. (A
    // staged FULL preset is index-independent: it replaces the whole state at commit,
    // so it stays and simply wins.)
    for (int v = 0; v < kMaxVoices; ++v) hotSwapStager.cancelVoice(v);

    // Shift every higher voice down one slot — APVTS values + gate + modulators.
    for (int d = idx; d < n - 1; ++d)
    {
        copyVoiceParams(d + 1, d);
        voiceColourIndex[(size_t) d] = voiceColourIndex[(size_t) (d + 1)];   // colour follows the voice
        voiceSlots[(size_t) d] = voiceSlots[(size_t) (d + 1)];          // CopyableSpinLock-safe
        gatePatterns[(size_t) d].copyDataFrom(gatePatterns[(size_t) (d + 1)]);
        filterPatterns[(size_t) d].copyDataFrom(filterPatterns[(size_t) (d + 1)]);
        pitchPatterns[(size_t) d].copyDataFrom(pitchPatterns[(size_t) (d + 1)]);
    }
    resetVoiceSlot(n - 1);                          // clear the vacated top slot
    numVoices.store(n - 1);
    apvts.state.setProperty("numVoices", n - 1, nullptr);
    refreshAllPitchQuantFlags();                    // the down-shift renumbered every voice
}

void PluginProcessor::swapVoices(int a, int b)
{
    const juce::ScopedLock sl(voicesLock);
    const int n = numVoices.load();
    if (a < 0 || b < 0 || a >= n || b >= n || a == b) return;

    // Pending per-voice swaps are keyed by index; swapping the two slots would
    // misdirect them, so drop both.
    hotSwapStager.cancelVoice(a);
    hotSwapStager.cancelVoice(b);

    auto swapPrefix = [this](const juce::String& pa, const juce::String& pb)
    {
        for (auto* p : getParameters())
            if (auto* rp = dynamic_cast<juce::RangedAudioParameter*>(p))
            {
                const juce::String id = rp->getParameterID();
                if (id.startsWith(pa))
                    if (auto* bp = apvts.getParameter(pb + id.substring(pa.length())))
                    {
                        const float va = rp->getValue();
                        const float vb = bp->getValue();
                        rp->setValueNotifyingHost(vb);
                        bp->setValueNotifyingHost(va);
                    }
            }
    };
    swapPrefix("v"  + juce::String(a) + "_", "v"  + juce::String(b) + "_");
    swapPrefix("ch" + juce::String(a) + "_", "ch" + juce::String(b) + "_");

    std::swap(voiceColourIndex[(size_t) a], voiceColourIndex[(size_t) b]);   // colour follows the voice
    // User-wavetable selection follows the voice too.
    std::swap(osc1UserPath[(size_t) a], osc1UserPath[(size_t) b]);
    std::swap(osc2UserPath[(size_t) a], osc2UserPath[(size_t) b]);
    { const int t1 = osc1UserIndex[(size_t) a].load(); osc1UserIndex[(size_t) a].store(osc1UserIndex[(size_t) b].load()); osc1UserIndex[(size_t) b].store(t1); }
    { const int t2 = osc2UserIndex[(size_t) a].load(); osc2UserIndex[(size_t) a].store(osc2UserIndex[(size_t) b].load()); osc2UserIndex[(size_t) b].store(t2); }

    const VoiceSlot tmpSlot = voiceSlots[(size_t) a];
    voiceSlots[(size_t) a] = voiceSlots[(size_t) b];
    voiceSlots[(size_t) b] = tmpSlot;

    GatePattern tmpGate;
    tmpGate.copyDataFrom(gatePatterns[(size_t) a]);
    gatePatterns[(size_t) a].copyDataFrom(gatePatterns[(size_t) b]);
    gatePatterns[(size_t) b].copyDataFrom(tmpGate);

    GatePattern tmpFilter;
    tmpFilter.copyDataFrom(filterPatterns[(size_t) a]);
    filterPatterns[(size_t) a].copyDataFrom(filterPatterns[(size_t) b]);
    filterPatterns[(size_t) b].copyDataFrom(tmpFilter);

    GatePattern tmpPitch;
    tmpPitch.copyDataFrom(pitchPatterns[(size_t) a]);
    pitchPatterns[(size_t) a].copyDataFrom(pitchPatterns[(size_t) b]);
    pitchPatterns[(size_t) b].copyDataFrom(tmpPitch);
    refreshPitchQuantFlags(a);   // the two slots' modulators swapped
    refreshPitchQuantFlags(b);
}

void PluginProcessor::resetVoice(int idx)
{
    const juce::ScopedLock sl(voicesLock);
    if (idx < 0 || idx >= numVoices.load()) return;
    hotSwapStager.cancelVoice(idx);            // a staged swap would re-fill what we're clearing
    const int keepColour = voiceColourIndex[(size_t) idx];
    resetVoiceSlot(idx);                       // params + gate + modulators → defaults
    voiceColourIndex[(size_t) idx] = keepColour;   // identity colour stays
    refreshPitchQuantFlags(idx);               // modulators cleared → flags off
}

void PluginProcessor::handleAsyncUpdate()
{
    // Commit any hot-swap that reached its loop boundary. The full preset commits
    // first (it supersedes per-voice swaps), then each flagged per-voice swap.
    juce::ValueTree tree;
    if (hotSwapStager.takeFull(tree))
    {
        applyFullPresetTree(tree);
        if (onPresetSwapCommitted) onPresetSwapCommitted();
    }
    for (int v = 0; v < kMaxVoices; ++v)
        if (hotSwapStager.takeVoice(v, tree))
        {
            applyVoicePresetTree(v, tree);
            if (onVoiceHotSwapCommitted) onVoiceHotSwapCommitted(v);
        }

    // Drain the MIDI program-change queue → applyMidiPresetSlot / applyFullMidiPreset
    // (which themselves hot-swap: stage while playing, apply while stopped).
    drainPendingMidiProgramChanges();
}

void PluginProcessor::loadUserWavetable(int voice, int oscIndex, const juce::File& file)
{
    if (voice < 0 || voice >= kMaxVoices || ! file.existsAsFile()) return;
    int idx;
    {
        const juce::ScopedLock sl(voicesLock);   // bank append must exclude the audio thread
        idx = bank.addOrLoadFile(file);
    }
    if (idx < 0) { if (onLoadError) onLoadError("Could not load wavetable \"" + file.getFileName() + "\""); return; }
    const juce::String path = file.getFullPathName();
    if (oscIndex == 0) { osc1UserPath[(size_t) voice] = path; osc1UserIndex[(size_t) voice].store(idx); }
    else             { osc2UserPath[(size_t) voice] = path; osc2UserIndex[(size_t) voice].store(idx); }
}

void PluginProcessor::clearUserWavetable(int voice, int oscIndex)
{
    if (voice < 0 || voice >= kMaxVoices) return;
    if (oscIndex == 0) { osc1UserPath[(size_t) voice].clear(); osc1UserIndex[(size_t) voice].store(-1); }
    else             { osc2UserPath[(size_t) voice].clear(); osc2UserIndex[(size_t) voice].store(-1); }
}

juce::String PluginProcessor::userWavetablePath(int voice, int oscIndex) const
{
    if (voice < 0 || voice >= kMaxVoices) return {};
    return (oscIndex == 0) ? osc1UserPath[(size_t) voice] : osc2UserPath[(size_t) voice];
}

bool PluginProcessor::userWavetableMissing(int voice, int oscIndex) const
{
    const auto p = userWavetablePath(voice, oscIndex);
    return p.isNotEmpty() && ! juce::File(p).existsAsFile();
}

void PluginProcessor::copyVoiceParams(int src, int dst)
{
    auto shift = [this](const juce::String& srcPrefix, const juce::String& dstPrefix)
    {
        for (auto* p : getParameters())
            if (auto* rp = dynamic_cast<juce::RangedAudioParameter*>(p))
            {
                const juce::String id = rp->getParameterID();
                if (id.startsWith(srcPrefix))
                    if (auto* dp = apvts.getParameter(dstPrefix + id.substring(srcPrefix.length())))
                        dp->setValueNotifyingHost(rp->getValue());
            }
    };
    shift("v"  + juce::String(src) + "_", "v"  + juce::String(dst) + "_");
    shift("ch" + juce::String(src) + "_", "ch" + juce::String(dst) + "_");
    // User-wavetable selection copies with the voice (same bank index, already loaded).
    osc1UserPath[(size_t) dst] = osc1UserPath[(size_t) src]; osc1UserIndex[(size_t) dst].store(osc1UserIndex[(size_t) src].load());
    osc2UserPath[(size_t) dst] = osc2UserPath[(size_t) src]; osc2UserIndex[(size_t) dst].store(osc2UserIndex[(size_t) src].load());
}

void PluginProcessor::resetVoiceSlot(int idx)
{
    auto resetPrefix = [this](const juce::String& prefix)
    {
        for (auto* p : getParameters())
            if (auto* rp = dynamic_cast<juce::RangedAudioParameter*>(p))
            {
                const juce::String id = rp->getParameterID();
                if (id.startsWith(prefix))
                    rp->setValueNotifyingHost(rp->getDefaultValue());
            }
    };
    resetPrefix("v"  + juce::String(idx) + "_");
    resetPrefix("ch" + juce::String(idx) + "_");
    gatePatterns[(size_t) idx].copyDataFrom(GatePattern{});
    filterPatterns[(size_t) idx].copyDataFrom(GatePattern{});
    pitchPatterns[(size_t) idx].copyDataFrom(GatePattern{});
    voiceSlots[(size_t) idx] = VoiceSlot{};
    osc1UserPath[(size_t) idx].clear(); osc1UserIndex[(size_t) idx].store(-1);
    osc2UserPath[(size_t) idx].clear(); osc2UserIndex[(size_t) idx].store(-1);
}

// ── Settings ─────────────────────────────────────────────────────────────────
void PluginProcessor::setUiScale(float scale)
{
    const float clamped = juce::jlimit(kUiScaleMedium, kUiScaleLarge, scale);
    if (uiScale == clamped) return;
    if (appSettings != nullptr)
    {
        appSettings->setValue("uiScale", (double) clamped);
        appSettings->saveIfNeeded();
    }
    ProcessorBase::setUiScale(clamped);
}

void PluginProcessor::setMidiSyncEnabled(bool on)
{
    midiClockSync.setEnabled(on);
    if (appSettings != nullptr) { appSettings->setValue("midiSyncEnabled", on); appSettings->saveIfNeeded(); }
}

void PluginProcessor::setMidiSyncMessages(int mode)
{
    midiClockSync.setMessages(mode);
    if (appSettings != nullptr) { appSettings->setValue("midiSyncMessages", mode); appSettings->saveIfNeeded(); }
}

void PluginProcessor::setMidiNoteMode(int mode)
{
    midiNoteMode.store(mode, std::memory_order_relaxed);
    if (appSettings != nullptr) { appSettings->setValue("midiNoteMode", mode); appSettings->saveIfNeeded(); }
}

// ── Note mode (gate + pitch-track) ───────────────────────────────────────────
// Audio-thread-only: scanNoteMode + applyNoteModeGate both run within processBlock
// on one thread, so the held-note stack + ramp state need no synchronisation.

void PluginProcessor::scanNoteMode(const juce::MidiBuffer& midi)
{
    const int mode = midiNoteMode.load(std::memory_order_relaxed);

    // A Free↔Note switch resets the held stack + gate so neither mode inherits stale
    // state (e.g. Free wouldn't get stuck silent, Note wouldn't open from a phantom note).
    if (mode != lastNoteMode)
    {
        numHeldNotes    = 0;
        noteCurrentMidi = -1;
        noteGateGain    = (mode == 1) ? 0.0f : 1.0f;
        lastNoteMode    = mode;
    }

    if (mode != 1)
        return;

    // Maintain a last-note-priority held-note stack: a Note On pushes (de-duped), a
    // Note Off removes; the stack top is the sounding pitch. Stack capped at 16 held
    // notes — beyond that the oldest is dropped (silently) to keep the array bounded.
    for (const auto meta : midi)
    {
        const auto msg = meta.getMessage();
        if (msg.isNoteOn())
        {
            const int note = msg.getNoteNumber();
            for (int i = 0; i < numHeldNotes; ++i)        // de-dupe a re-pressed note
                if (heldNotes[(size_t) i] == note) { --numHeldNotes;
                    for (int j = i; j < numHeldNotes; ++j) heldNotes[(size_t) j] = heldNotes[(size_t) j + 1];
                    break; }
            if (numHeldNotes == (int) heldNotes.size())   // full → drop the oldest
            {
                for (int j = 0; j + 1 < numHeldNotes; ++j) heldNotes[(size_t) j] = heldNotes[(size_t) j + 1];
                --numHeldNotes;
            }
            heldNotes[(size_t) numHeldNotes++] = note;
        }
        else if (msg.isNoteOff())
        {
            const int note = msg.getNoteNumber();
            for (int i = 0; i < numHeldNotes; ++i)
                if (heldNotes[(size_t) i] == note) { --numHeldNotes;
                    for (int j = i; j < numHeldNotes; ++j) heldNotes[(size_t) j] = heldNotes[(size_t) j + 1];
                    break; }
        }
    }

    noteCurrentMidi = numHeldNotes > 0 ? heldNotes[(size_t) (numHeldNotes - 1)] : -1;
}

void PluginProcessor::applyNoteModeGate(juce::AudioBuffer<float>& buffer, int numSamples)
{
    if (midiNoteMode.load(std::memory_order_relaxed) != 1)
        return;

    // Linear amplitude ramp toward 1 (a note held) or 0 (all released), reaching the
    // target over ~8 ms so the held→silent edge fades instead of clicking.
    const float target   = numHeldNotes > 0 ? 1.0f : 0.0f;
    const float fadeSecs  = 0.008f;
    const float gainStep = 1.0f / (float) juce::jmax(1.0, fadeSecs * currentSampleRate);

    float* l = buffer.getWritePointer(0);
    float* r = buffer.getNumChannels() > 1 ? buffer.getWritePointer(1) : nullptr;
    float  g = noteGateGain;
    for (int i = 0; i < numSamples; ++i)
    {
        g += juce::jlimit(-gainStep, gainStep, target - g);
        l[i] *= g;
        if (r != nullptr) r[i] *= g;
    }
    noteGateGain = g;
}

// ── Presets ──────────────────────────────────────────────────────────────────
} // namespace mu_tant

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new mu_tant::PluginProcessor();
}
