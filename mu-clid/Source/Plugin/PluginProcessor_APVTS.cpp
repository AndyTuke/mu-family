// partial-class TU split from PluginProcessor.cpp. Contains:
// - createParameterLayout (the APVTS layout: the Rhythm rows from Persistence/RhythmParams, then the globals)
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
#include "Audio/FX/Slots/FXAlgorithmDef.h"
#include "Plugin/MixerFxParams.h"
#include "Persistence/RhythmParams.h"   // the Rhythm type's rows (addLayerParams)

#include <algorithm>
#include <cstring>
#include <thread>

namespace mu_clid {

juce::AudioProcessorValueTreeState::ParameterLayout PluginProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    auto addI = [&](const juce::String& id, const juce::String& name, int mn, int mx, int def)
    {
        layout.add(std::make_unique<juce::AudioParameterInt>(id, name, mn, mx, def));
    };

    // ── Per-rhythm parameters (one set per rhythm slot): the Rhythm type's table ─────────
    for (int i = 0; i < mu_limits::kMaxLayers; ++i)
        addLayerParams(layout, i);

    // ── Shared global FX + return + master layout (mu-core) ───────────────────
    // Declares eff_/dly_/rev_/eff2*/echo_/ret_*/mstr_lvl/mstr_pan/mst_ins* with
    // the family's reference defaults; synced to fxChain/mixerEngine via
    // ProcessorBase::syncGlobalFxParam. mu-clid keeps only its product-specific
    // globals below (the ch{i}_ rhythm strips + mstrLoop).
    mu_mixfx::addGlobalFxParams(layout);

    // ── Rhythm channel strips (11 × 8 = 88) ──────────────────────────────────
    for (int i = 0; i < mu_limits::kMaxLayers; ++i)
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
    if (raw[0] == 'r' && raw[1] >= '0' && raw[1] < '0' + mu_limits::kMaxLayers && raw[2] == '_' && raw[3] != '\0')
    {
        markRhythmParamDirty(raw[1] - '0', raw + 3);
        return;
    }
    // (Mixer / global-FX ids never reach here: ProcessorBase's own listener syncs them.)
    // Master loop length (a relaxed atomic in the step clock, so safe from any thread)
    if (std::strcmp(raw, "mstrLoop") == 0)
    {
        stepClock.setMasterLoopSteps(juce::jmin((int)v * 16, 256));   // 0 = free
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
    if (ri < 0 || ri >= mu_limits::kMaxLayers) return;
    const int index = findRhythmParamIndex(suffix);
    if (index < 0) return;

    pendingDirty[(size_t) ri][(size_t) index].store(true, std::memory_order_release);
    rhythmHasPending[(size_t) ri].store(true, std::memory_order_release);

    // If an offline render holds the apply, hand the change to the deferred-work pass instead of stranding it.
    if (! isMessageThread() || ! servicePendingRhythmParams())
        requestDeferredWork();
}

// Returns false when another thread held the apply and this call did not wait it out (the caller then
// asks for a later pass); true once this call has applied everything queued.
bool PluginProcessor::servicePendingRhythmParams(bool waitForOtherApply)
{
    // One applier at a time: an offline render drains from its own thread while the message thread's
    // poll may do the same. The render waits its turn (bounded, so a misreported offline flag cannot
    // stall a live callback for long); the poll just leaves the work to whoever holds the flag.
    constexpr int kMaxWaitYields = 2000;
    int yields = 0;
    while (applyingRhythmParams.test_and_set(std::memory_order_acquire))
    {
        if (! waitForOtherApply || ++yields > kMaxWaitYields)
            return false;
        std::this_thread::yield();
    }
    for (int ri = 0; ri < mu_limits::kMaxLayers; ++ri)
        if (rhythmHasPending[(size_t) ri].exchange(false, std::memory_order_acq_rel))
            applyPendingRhythmParams(ri);
    applyingRhythmParams.clear(std::memory_order_release);
    return true;
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
    if (ri >= getNumRhythms())
    {
        dropPendingRhythmParams(ri);   // no rhythm to apply to
        return;
    }

    Rhythm& r = getRhythm(ri);
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
        if (patternDirty) updateRhythmPattern(ri);
        if (voiceDirty && voiceEngine(ri)) voiceEngine(ri)->setParams(r.voiceParams);
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
    if (ri < 0 || ri >= mu_limits::kMaxLayers) return;
    if (ri >= getNumRhythms()) return;

    Rhythm& r = getRhythm(ri);
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

    if (patternDirty) updateRhythmPattern(ri);
    if (voiceEngine(ri)) voiceEngine(ri)->setParams(r.voiceParams);
}

// syncFXParam / syncMixerParam removed — mu-clid now routes its FX / return /
// master / channel-strip parameterChanged callbacks to the shared
// ProcessorBase::syncGlobalFxParam (mu-core), matching mu-tant. See the backlog.

//==============================================================================
// rt-ok-fn: layer install and hot-swap on the message thread
void PluginProcessor::pushRhythmToApvts(int ri)
{
    if (ri < 0 || ri >= getNumRhythms()) return;
    const Rhythm& r = getRhythm(ri);
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

// rt-ok-fn: layer install and hot-swap on the message thread
void PluginProcessor::pushMixerChannelToApvts(int idx)
{
    if (idx < 0 || idx >= mu_limits::kMaxLayers) return;
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
