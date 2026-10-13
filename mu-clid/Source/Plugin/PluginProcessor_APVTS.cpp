// partial-class TU split from PluginProcessor.cpp. Contains:
// - createParameterLayout (the big APVTS layout factory)
// - parameterChanged dispatcher (allocation-free; queues rhythm changes for the message thread)
// - the rhythm-parameter queue drain / forceSyncRhythmFromApvts (FX/mixer route to mu-core's
//   ProcessorBase::syncGlobalFxParam — see the backlog)
// - pushRhythmToApvts / pushMixerChannelToApvts / swapApvtsForRhythms
//
// Shared helpers (kChannelSuffixes, applyRhythmSuffix, adsrTime, adsrSus)
// live in PluginProcessor_Internal.h since the Preset TU needs them too.
// Global param IDs live in kGlobalParamDefs (PresetHelpers.h).

#include "Audio/SpinLock.h"   // mu-core: spin lock helpers
#include "PluginProcessor.h"
#include "PluginProcessor_Internal.h"
#include "Modulation/ModulationSkew.h"   // knob ranges shared with modulation (depth = % of range)
#include "Audio/FX/Slots/FXAlgorithmDef.h"
#include "Plugin/MixerFxParams.h"
#include "ValueFormat.h"   // mu-core: shared value text
#include "Sequencer/EuclidParamLayout.h"   // mu-core: the Euclid parameter set

#include <algorithm>
#include <cstring>
#include <thread>

namespace mu_clid {

// The pad / insert-length modulation ranges must equal the parameters' ranges.
static_assert(mu_clid::mod_skew::kPad.hi == (float) HitGenerator::kMaxPrePad && HitGenerator::kMaxPrePad == HitGenerator::kMaxPostPad, "kPad must match the pad parameters");
static_assert(mu_clid::mod_skew::kInsertLength.hi == (float) HitGenerator::kMaxInsertLength, "kInsertLength must match insLen");

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

    // ADSR time params in 0..10 seconds with 0.3 skew. Skew 0.3 puts
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

    // ── Per-rhythm parameters (one set per rhythm slot) ─────────────────────────────────
    // Rhythms 0..kAutomatedRhythms-1 use full "Rhythm N " names so DAW automation
    // lanes show them clearly. Remaining rhythms use short "RN " names.
    for (int i = 0; i < RhythmSequencer::MaxRhythms; ++i)
    {
        const juce::String p = "r" + juce::String(i) + "_";
        const juce::String n = (i < kAutomatedRhythms)
                                   ? "Rhythm " + juce::String(i + 1) + " "
                                   : "R"       + juce::String(i + 1) + " ";

        // HitGen A / B / C, logic, legato, mono and reset steps: the Euclid set shared with mu-Clid MIDI.
        mu_core::addEuclidParams(layout, p, n);
        // Pitch — octave ±3, semi ±12 (±1 oct), fine ±100 cents (1 cent step).
        // Combined static max = ±4 octaves; clamped at the engine.
        addI(p+"pitchOct",  n+"Pitch Oct",  (int) mu_clid::mod_skew::kPitchOctave.lo, (int) mu_clid::mod_skew::kPitchOctave.hi, 0);
        addI(p+"pitchSemi", n+"Pitch Semi", (int) mu_clid::mod_skew::kPitchSemi.lo,   (int) mu_clid::mod_skew::kPitchSemi.hi,   0);
        addI(p+"pitchFine", n+"Pitch Fine", -100, 100, 0);
        addAdsrT(p+"pEnvAtk", n+"P Env Atk", 0.0f);    // seconds (≈ legacy 0.0 × 0.03)
        addAdsrT(p+"pEnvDec", n+"P Env Dec", 0.03f);   //         (≈ legacy 1.0 × 0.03)
        addF     (p+"pEnvSus", n+"P Env Sus", 0.0f, 100.0f, 0.0f);   // sustain stays 0..100 %
        addAdsrT(p+"pEnvRel", n+"P Env Rel", 0.03f);   //         (≈ legacy 1.0 × 0.03)
        addF(p+"pEnvDep",   n+"P Env Dep",  mu_clid::mod_skew::kPitchEnvDepth.lo, mu_clid::mod_skew::kPitchEnvDepth.hi, 0.0f);
        // Filter
        addI(p+"fltType", n+"Filter Type", 0, 15, 0);  // 0-15: LP12/HP12/BP12/Notch/LP24/HP24/BP24/LP6/Comb+/AP12/Notch24/HP6/Peak/LoShf/HiShf/Comb-
        // log-skewed range. Skew 0.25 puts ~1.3 kHz at slider centre and
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
        addF(p+"fltRes",  n+"Filter Res",   mu_clid::mod_skew::kResonance.lo, mu_clid::mod_skew::kResonance.hi, 0.2f);
        addAdsrT(p+"fEnvAtk", n+"F Env Atk", 0.03f);   // seconds (≈ legacy 1.0 × 0.03)
        addAdsrT(p+"fEnvDec", n+"F Env Dec", 0.09f);   //         (≈ legacy 3.0 × 0.03)
        addF     (p+"fEnvSus", n+"F Env Sus",  0.0f, 100.0f,  0.0f);   // sustain stays 0..100 %
        addAdsrT(p+"fEnvRel", n+"F Env Rel", 0.09f);   //         (≈ legacy 3.0 × 0.03)
        addF(p+"fEnvDep", n+"F Env Dep",  mu_clid::mod_skew::kFenvDepth.lo, mu_clid::mod_skew::kFenvDepth.hi, 0.0f);
        // 4-pole high-pass that sits inline with the main filter. Skewed so most
        // of the knob travel lives in the audible low-end region (0–200 Hz).
        layout.add(std::make_unique<juce::AudioParameterFloat>(
            p+"fltLoCut", n+"Filter Low Cut",
            juce::NormalisableRange<float>(0.0f, 1000.0f, 0.0f, 0.35f), 0.0f,
            juce::AudioParameterFloatAttributes().withStringFromValueFunction(
                [](float v, int) { return mu_fmt::lowCut(v); })));
        // Pre-filter valve saturation depth. 0 = bypass, 1 = full warmth.
        addF(p+"fltDrv", n+"Filter Drive", 0.0f, 1.0f, 0.0f);
        // Amp — level stored in dB (-60..+6), engine converts to gain at read.
        // Default 0 dB = unity gain.
        addF(p+"ampLvl",  n+"Amp Level", mu_clid::mod_skew::kAmpLevel.lo, mu_clid::mod_skew::kAmpLevel.hi, 0.0f);
        addAdsrT(p+"aEnvAtk", n+"A Env Atk", 0.005f);   // seconds
        addAdsrT(p+"aEnvDec", n+"A Env Dec", 0.3f);
        addF(p+"aEnvSus", n+"A Env Sus",  0.0f, 100.0f, 80.0f);   // sustain stays 0..100 %
        addAdsrT(p+"aEnvRel", n+"A Env Rel", 0.5f);
        addF(p+"accentDb",  n+"Accent",     mu_clid::mod_skew::kAccent.lo, mu_clid::mod_skew::kAccent.hi, 0.0f);
        // Insert effect: algo selector + 4 generic Param slots stored as 0..1
        // normalised. Each algorithm's process() reads p.insertParam[N] and
        // converts to the actual value through mu_ui::normToActual using the
        // per-slot range / skew table in Source/Audio/InsertSlotConfig.h. DAW
        // automation lanes show 0..1; the editor UI shows the actual value
        // with the correct unit ("Drive %" / "Bits" / "Mid Hz" / "Note" / ...)
        // based on the active algorithm.
        addI(p+"drvChar", n+"Insert Algo", 0, mu_audio::kInsertAlgorithmCount - 1, 0);
        addF(p+"insP1",   n+"Insert P1",   0.0f, 1.0f, 0.0f);
        addF(p+"insP2",   n+"Insert P2",   0.0f, 1.0f, 0.0f);
        addF(p+"insP3",   n+"Insert P3",   0.0f, 1.0f, 0.0f);
        addF(p+"insP4",   n+"Insert P4",   0.0f, 1.0f, 0.0f);
        // Output mode (0 Sample, 1 MIDI) and the MIDI-output settings used in MIDI mode. Velocity and
        // Accent are percentages; Accent has the meaning of mu-Clid MIDI's Accent knob.
        addI(p+"outMode",    n+"Output Mode",   0, 1,   0);
        addI(p+"midiNote",   n+"MIDI Note",     0, 127, 36);
        addI(p+"midiChan",   n+"MIDI Channel",  1, 16,  1);
        addI(p+"midiVel",    n+"MIDI Velocity", 1, 100, 100);
        addI(p+"midiAccent", n+"MIDI Accent",   0, 100, 0);
    }

    // ── Shared global FX + return + master layout (mu-core) ───────────────────
    // Declares eff_/dly_/rev_/eff2*/echo_/ret_*/mstr_lvl/mstr_pan/mst_ins* with
    // the family's reference defaults; synced to fxChain/mixerEngine via
    // ProcessorBase::syncGlobalFxParam. mu-clid keeps only its product-specific
    // globals below (the ch{i}_ rhythm strips + mstrLoop).
    mu_mixfx::addGlobalFxParams(layout);

    // ── Rhythm channel strips (11 × 8 = 88) ──────────────────────────────────
    for (int i = 0; i < RhythmSequencer::MaxRhythms; ++i)
    {
        const juce::String c = "ch" + juce::String(i) + "_";
        const juce::String n = (i < kAutomatedRhythms)
                                   ? "Rhythm " + juce::String(i + 1) + " Ch "
                                   : "Ch"      + juce::String(i + 1) + " ";
        // mu-Clid's strips keep their original ids (no version hint) and continuous ranges.
        mu_mixfx::ChannelStripOptions opts;
        opts.versionHint = 0;
        opts.continuous  = true;
        mu_mixfx::addChannelStripParams(layout, c, n, opts);
    }

    // ── Master loop length — product-specific (sequencer master loop) ─────────
    // mstr_lvl / mstr_pan / mst_ins* + the ret_ return strips are declared by
    // mu_mixfx::addGlobalFxParams above; mstrLoop is mu-clid-only.
    addI("mstrLoop",    "Master Loop",      0,       16,       0);      // 0=free, 1-16 → 16-256 steps

    return layout;
}

//==============================================================================
void PluginProcessor::parameterChanged(const juce::String& id, float v)
{
    // Raw characters throughout: this can run on the audio thread (VST3 automation), where building a
    // juce::String would allocate. Only atomic stores happen here; see markRhythmParamDirty.
    const char* raw = id.toRawUTF8();

    // Rhythm params: r{0-7}_{suffix}
    if (raw[0] == 'r' && raw[1] >= '0' && raw[1] < '0' + RhythmSequencer::MaxRhythms && raw[2] == '_' && raw[3] != '\0')
    {
        markRhythmParamDirty(raw[1] - '0', raw + 3);
        return;
    }
    // (Mixer / global-FX ids never reach here: ProcessorBase's own listener syncs them.)
    // Master loop length (a relaxed atomic in the sequencer, so safe from any thread)
    if (std::strcmp(raw, "mstrLoop") == 0)
    {
        sequencer.setMasterLoopSteps(juce::jmin((int)v * 16, 256));   // 0 = free
        return;
    }
}

//==============================================================================
// True on the message thread. With no MessageManager (a headless run) it is the thread that built the
// processor, so a test cannot silently take the message-thread path from its "audio" thread.
bool PluginProcessor::isMessageThread() const noexcept
{
    const auto* mm = juce::MessageManager::getInstanceWithoutCreating();
    return mm != nullptr ? mm->isThisTheMessageThread() : std::this_thread::get_id() == ownerThread;
}

// Binary search over the sorted suffix table, on raw characters; -1 if the suffix is unknown.
int PluginProcessor::findRhythmParamIndex(const char* suffix) const noexcept
{
    const auto* begin = suffixIndex.data();
    const auto* end   = begin + suffixIndexCount;
    const auto* it = std::lower_bound(begin, end, suffix,
                                      [](const SuffixIndex& e, const char* s) { return std::strcmp(e.suffix, s) < 0; });
    return (it != end && std::strcmp(it->suffix, suffix) == 0) ? it->index : -1;
}

// Listener side, any thread: raise the dirty flags (suffix flag first, then the rhythm flag, so a drain
// that clears the rhythm flag and scans always finds the suffix flag). On the message thread apply at
// once; elsewhere ask for the message-thread deferred-work pass (one atomic store, no allocation).
void PluginProcessor::markRhythmParamDirty(int ri, const char* suffix) noexcept
{
    if (ri < 0 || ri >= RhythmSequencer::MaxRhythms) return;
    const int index = findRhythmParamIndex(suffix);
    if (index < 0) return;

    pendingDirty[(size_t) ri][(size_t) index].store(true, std::memory_order_release);
    rhythmHasPending[(size_t) ri].store(true, std::memory_order_release);

    if (isMessageThread())
        servicePendingRhythmParams();
    else
        requestDeferredWork();
}

void PluginProcessor::servicePendingRhythmParams()
{
    for (int ri = 0; ri < RhythmSequencer::MaxRhythms; ++ri)
        if (rhythmHasPending[(size_t) ri].exchange(false, std::memory_order_acq_rel))
            applyPendingRhythmParams(ri);
}

// Forget every queued change of one rhythm (the caller is about to apply all of its parameters).
void PluginProcessor::dropPendingRhythmParams(int ri) noexcept
{
    rhythmHasPending[(size_t) ri].store(false, std::memory_order_release);
    for (auto& dirty : pendingDirty[(size_t) ri])
        dirty.store(false, std::memory_order_release);
}

// Message thread. Applies the changed suffixes of one rhythm from the parameters' own values, then
// rebuilds the pattern and hands the voice parameters to the engine (unless a bulk load is running:
// its orchestrator does the full resync afterwards).
void PluginProcessor::applyPendingRhythmParams(int ri)
{
    if (ri >= sequencer.getNumRhythms())
    {
        dropPendingRhythmParams(ri);   // no rhythm to apply to
        return;
    }

    Rhythm& r = sequencer.getRhythm(ri);
    bool patternDirty = false;
    bool voiceDirty   = false;

    // Clear each flag first, then read the parameter: a change that lands after the clear re-raises it.
    // voiceParamsLock is held per field so the audio thread's brief seed-copy can slip in between;
    // the audio thread only tries it, and the message thread (this one) is the only writer.
    for (int i = 0; i < kRhythmParamCount; ++i)
    {
        if (pendingDirty[(size_t) ri][(size_t) i].exchange(false, std::memory_order_acq_rel)
            && rawParams[(size_t) ri][(size_t) i] != nullptr)
        {
            mu_core::spinLock(r.voiceParamsLock);
            kRhythmParamDefs[i].apply(rawParams[(size_t) ri][(size_t) i]->load(), r, patternDirty, voiceDirty);
            mu_core::spinUnlock(r.voiceParamsLock);
        }
    }

    if (!apvtsLoading.load(std::memory_order_acquire))
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
void PluginProcessor::forceSyncRhythmFromApvts(int ri)
{
    if (ri < 0 || ri >= RhythmSequencer::MaxRhythms) return;
    if (ri >= sequencer.getNumRhythms()) return;

    Rhythm& r = sequencer.getRhythm(ri);
    const juce::String prefix = "r" + juce::String(ri) + "_";

    bool patternDirty = false;
    bool voiceDirty   = false;
    // Every parameter is applied below, so changes queued for this rhythm would only repeat it.
    dropPendingRhythmParams(ri);
    // Acquire/release per-field so the audio thread can squeeze its (very
    // brief) seed-copy lock window in between our writes. Holding the lock
    // across the whole bulk apply (~52 entries) would starve the audio thread
    // for tens of microseconds — unacceptable even though forceSync usually
    // runs from preset / state-restore paths.
    for (int j = 0; j < kRhythmParamCount; ++j)
    {
        if (auto* raw = apvts.getRawParameterValue(prefix + kRhythmParamDefs[j].suffix))
        {
            mu_core::spinLock(r.voiceParamsLock);
            kRhythmParamDefs[j].apply(raw->load(), r, patternDirty, voiceDirty);
            mu_core::spinUnlock(r.voiceParamsLock);
        }
    }

    if (patternDirty) sequencer.updatePattern(ri);
    if (voiceEngines[ri]) voiceEngines[ri]->setParams(r.voiceParams);
}

// syncFXParam / syncMixerParam removed — mu-clid now routes its FX / return /
// master / channel-strip parameterChanged callbacks to the shared
// ProcessorBase::syncGlobalFxParam (mu-core), matching mu-tant. See the backlog.

//==============================================================================
void PluginProcessor::pushRhythmToApvts(int ri)
{
    if (ri < 0 || ri >= sequencer.getNumRhythms()) return;
    const Rhythm& r = sequencer.getRhythm(ri);
    const juce::String px = "r" + juce::String(ri) + "_";

    auto set = [this](const juce::String& id, float v)
    {
        if (auto* p = apvts.getParameter(id))
            p->setValueNotifyingHost(p->convertTo0to1(v));
    };

    // iterate the declarative param table instead of hand-listing every
    // (id, value) pair. Each entry's `push` lambda reads the current value from
    // the Rhythm — semantics match the prior code exactly (HitGen ints become
    // floats; ADSR sustain scales 0..1 → 0..100; aEnvRel reflects the
    // ampRelToEnd sentinel; bool fields write 0.0 / 1.0).
    for (int i = 0; i < mu_clid::kRhythmParamCount; ++i)
    {
        const auto& def = mu_clid::kRhythmParamDefs[i];
        set(px + def.suffix, def.push(r));
    }
}

void PluginProcessor::pushMixerChannelToApvts(int idx)
{
    if (idx < 0 || idx >= RhythmSequencer::MaxRhythms) return;
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

} // namespace mu_clid
