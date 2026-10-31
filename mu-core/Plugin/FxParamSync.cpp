#include "Plugin/FxParamSync.h"
#include "Plugin/MixerFxParams.h"           // mu_mixfx::isGlobalFxParamId
#include "Audio/FX/Slots/FXAlgorithmDef.h"   // FXAlgorithmRegistry
#include "Audio/AlgorithmNames.h"             // mu_audio::kInsertAlgorithmCount
#include <cstring>                            // std::strcmp — alloc-free suffix compare

namespace mu_core
{

FxParamSync::FxParamSync(juce::AudioProcessor& p, juce::AudioProcessorValueTreeState& a, MixerEngine& m,
                         FXChain& f, std::atomic<bool>& mute)
    : proc(p), apvts(a), mixerEngine(m), fxChain(f), muteChanged(mute) {}

bool FxParamSync::isFxParamId(const juce::String& id)
{
    return id.startsWith("ch") || mu_mixfx::isGlobalFxParamId(id);
}

namespace
{
// For an id of the form ch{N}_{suffix} (N one or two digits), writes N to `index` and returns a pointer to the
// suffix inside the id's own buffer; otherwise returns nullptr. Allocation-free (audio-thread automation path).
const char* channelStripSuffix(const juce::String& id, int& index) noexcept
{
    const char* s = id.toRawUTF8();
    if (s[0] != 'c' || s[1] != 'h' || s[2] < '0' || s[2] > '9') return nullptr;
    int n = s[2] - '0';
    const char* p = s + 3;
    if (*p >= '0' && *p <= '9') { n = n * 10 + (*p - '0'); ++p; }
    if (*p != '_' || p[1] == '\0') return nullptr;
    index = n;
    return p + 1;
}
}

void FxParamSync::onChanged(const juce::String& id, float v)
{
    syncGlobal(id, v);
    // ch{N}_mute: a flag for the message-thread poll (the lamps follow a mute made anywhere); alloc-free.
    int stripIdx = -1;
    if (const char* suffix = channelStripSuffix(id, stripIdx); suffix != nullptr && std::strcmp(suffix, "mute") == 0)
        muteChanged.store(true, std::memory_order_relaxed);
    // bus{B}_mute too: the bus lamps follow a mute made anywhere.
    else if (id.startsWith("bus") && id.endsWith("_mute"))
        muteChanged.store(true, std::memory_order_relaxed);
}

// Listen to every mixer / global-FX parameter, then seed the engines from the current values.
void FxParamSync::start()
{
    jassert(fxParamListener == nullptr);   // once per processor
    fxParamListener = std::make_unique<Listener>(*this);
    for (auto* p : proc.getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*>(p))
            if (isFxParamId(rp->getParameterID()))
                apvts.addParameterListener(rp->getParameterID(), fxParamListener.get());
    syncAll();   // JUCE doesn't fire parameterChanged on construction
}

// Stops listening (idempotent): the owner calls it from its destructor, before the members the listener touches die.
void FxParamSync::stop()
{
    if (fxParamListener == nullptr) return;
    for (auto* p : proc.getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*>(p))
            if (isFxParamId(rp->getParameterID()))
                apvts.removeParameterListener(rp->getParameterID(), fxParamListener.get());
    fxParamListener.reset();
}

// rt-ok-fn: constructor and state restore only (message thread); not the automation path
void FxParamSync::syncAll()
{
    for (auto* p : proc.getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*>(p))
        {
            const auto id = rp->getParameterID();
            if (isFxParamId(id))
                if (auto* a = apvts.getRawParameterValue(id))
                    syncGlobal(id, a->load());
        }
}

void FxParamSync::syncGlobal(const juce::String& id, float v)
{
    // Prefix router → per-family helper. Keeps each automation tick a single
    // prefix test plus one bounded if/else inside the matching helper.
    int stripIdx = -1;
    if (const char* suffix = channelStripSuffix(id, stripIdx); suffix != nullptr)   // ch{N}_{param}
    {
        if (stripIdx < MixerEngine::MaxChannels)
            syncChannelStripParam(stripIdx, suffix, v);   // suffix into id buffer, no alloc
        return;
    }

    if constexpr (MixerEngine::kNumBuses > 0)
        if (id.startsWith("bus"))                                           // bus{B}_{param}, bus{B}_i{I}_{param}
        {
            syncBusParam(id, v);
            return;
        }

    if (id.startsWith("ret_"))                                              // ret_{eff|dly|rev}_{param}
    {
        int retIndex = -1;
        if      (id.startsWith("ret_eff_")) retIndex = 0;
        else if (id.startsWith("ret_dly_")) retIndex = 1;
        else if (id.startsWith("ret_rev_")) retIndex = 2;
        if (retIndex >= 0)
            syncReturnStripParam(retIndex, id.toRawUTF8() + 8, v);   // suffix into id buffer, no alloc
        return;
    }

    if (syncMasterParam(id, v)) return;                                     // mstr_*, mst_ins*

    syncFxSlotParam(id, v);                                                 // eff_*, eff2*, dly_*, rev_*, echo_*
}

// param / rest are raw NUL-terminated suffixes pointing into the caller's id
// buffer (param IDs are pure ASCII). std::strcmp avoids building any juce::String,
// so this stays allocation-free on the audio-thread automation path.
void FxParamSync::syncChannelStripParam(int channel, const char* param, float v)
{
    auto& ch = mixerEngine.channels[(size_t) channel];
    auto is = [param](const char* s) { return std::strcmp(param, s) == 0; };
    if      (is("lvl"))     ch.level.store(v, std::memory_order_relaxed);
    else if (is("pan"))     ch.pan.store(v, std::memory_order_relaxed);
    else if (is("mute"))    ch.mute.store(v > 0.5f, std::memory_order_relaxed);
    else if (is("solo"))    ch.solo.store(v > 0.5f, std::memory_order_relaxed);
    else if (is("sendEff")) ch.sendEffect.store(v, std::memory_order_relaxed);
    else if (is("sendDly")) ch.sendDelay.store(v, std::memory_order_relaxed);
    else if (is("sendRev")) ch.sendReverb.store(v, std::memory_order_relaxed);
    else if (is("scSrc"))   ch.sidechainSource.store(juce::roundToInt(v) - 1, std::memory_order_relaxed);
    else if (is("scAmt"))   ch.sidechainAmount.store(v, std::memory_order_relaxed);
    else if (is("scAtk"))   ch.sidechainAttackMs.store(v, std::memory_order_relaxed);
    else if (is("scRel"))   ch.sidechainReleaseMs.store(v, std::memory_order_relaxed);
    else if (is("outBus"))  ch.outputBus.store(juce::jlimit(0, 8, juce::roundToInt(v)), std::memory_order_relaxed);
    else if (is("bus"))     ch.busAssign.store(juce::jlimit(0, MixerEngine::kNumBuses, juce::roundToInt(v)), std::memory_order_relaxed);
}

// bus{B}_{suffix}: the strip's own suffixes, or i{I}_{drvChar|insP1..4} for an insert. Pointer arithmetic on the
// id's buffer only, so the automation path allocates nothing.
void FxParamSync::syncBusParam(const juce::String& id, float v)
{
    const char* s = id.toRawUTF8();
    if (s[3] < '0' || s[3] > '9') return;
    const int b = s[3] - '0';
    if (b >= MixerEngine::kNumBuses || s[4] != '_') return;
    const char* rest = s + 5;
    auto& bus = mixerEngine.buses[(size_t) b];

    if (rest[0] == 'i' && rest[1] >= '0' && rest[1] <= '9' && rest[2] == '_')   // i{I}_{param}
    {
        const int i = rest[1] - '0';
        if (i >= MixerEngine::kBusInserts) return;
        auto& ins = bus.insert[i];
        const char* param = rest + 3;
        auto is = [param](const char* t) { return std::strcmp(param, t) == 0; };
        if      (is("drvChar")) ins.insertAlgo.store(juce::jlimit(0, mu_audio::kInsertAlgorithmCount - 1, juce::roundToInt(v)), std::memory_order_relaxed);
        else if (is("insP1"))   ins.insertParam[0].store(v, std::memory_order_relaxed);
        else if (is("insP2"))   ins.insertParam[1].store(v, std::memory_order_relaxed);
        else if (is("insP3"))   ins.insertParam[2].store(v, std::memory_order_relaxed);
        else if (is("insP4"))   ins.insertParam[3].store(v, std::memory_order_relaxed);
        return;
    }

    auto is = [rest](const char* t) { return std::strcmp(rest, t) == 0; };
    if      (is("lvl"))     bus.level.store(v, std::memory_order_relaxed);
    else if (is("pan"))     bus.pan.store(v, std::memory_order_relaxed);
    else if (is("mute"))    bus.mute.store(v > 0.5f, std::memory_order_relaxed);
    else if (is("solo"))    bus.solo.store(v > 0.5f, std::memory_order_relaxed);
    else if (is("sendEff")) bus.sendEffect.store(v, std::memory_order_relaxed);
    else if (is("sendDly")) bus.sendDelay.store(v, std::memory_order_relaxed);
    else if (is("sendRev")) bus.sendReverb.store(v, std::memory_order_relaxed);
    else if (is("scSrc"))   bus.sidechainSource.store(juce::jlimit(0, mu_mixfx::kScSrcMax, juce::roundToInt(v)) - 1, std::memory_order_relaxed);
    else if (is("scAmt"))   bus.sidechainAmount.store(v, std::memory_order_relaxed);
    else if (is("scAtk"))   bus.sidechainAttackMs.store(v, std::memory_order_relaxed);
    else if (is("scRel"))   bus.sidechainReleaseMs.store(v, std::memory_order_relaxed);
}

void FxParamSync::syncReturnStripParam(int retIndex, const char* rest, float v)
{
    auto& ret = mixerEngine.returns[(size_t) retIndex];
    auto is = [rest](const char* s) { return std::strcmp(rest, s) == 0; };
    if      (is("lvl"))   ret.level.store(v, std::memory_order_relaxed);
    else if (is("pan"))   ret.pan.store(v, std::memory_order_relaxed);
    else if (is("mute"))  ret.mute.store(v > 0.5f, std::memory_order_relaxed);
    else if (is("solo"))  ret.solo.store(v > 0.5f, std::memory_order_relaxed);
    else if (is("scSrc")) ret.sidechainSource.store(juce::jlimit(0, mu_mixfx::kScSrcMax, (int) v) - 1, std::memory_order_relaxed);
    else if (is("scAmt")) ret.sidechainAmount.store(juce::jlimit(0.0f, 1.0f, v), std::memory_order_relaxed);
    else if (is("scAtk")) ret.sidechainAttackMs.store(v, std::memory_order_relaxed);
    else if (is("scRel")) ret.sidechainReleaseMs.store(v, std::memory_order_relaxed);
}

bool FxParamSync::syncMasterParam(const juce::String& id, float v)
{
    if (id == "mstr_lvl")     { mixerEngine.masterLevel.store(v, std::memory_order_relaxed); return true; }
    if (id == "mstr_pan")     { mixerEngine.masterPan.store(v, std::memory_order_relaxed);   return true; }
    if (id == "mst_insChar")  { mixerEngine.masterInsert1Algo.store(juce::jlimit(0, mu_audio::kInsertAlgorithmCount - 1, (int) v), std::memory_order_relaxed); return true; }
    if (id == "mst_insP1")    { mixerEngine.masterInsert1Param[0].store(v, std::memory_order_relaxed); return true; }
    if (id == "mst_insP2")    { mixerEngine.masterInsert1Param[1].store(v, std::memory_order_relaxed); return true; }
    if (id == "mst_insP3")    { mixerEngine.masterInsert1Param[2].store(v, std::memory_order_relaxed); return true; }
    if (id == "mst_insP4")    { mixerEngine.masterInsert1Param[3].store(v, std::memory_order_relaxed); return true; }
    if (id == "mst_ins2Char") { mixerEngine.masterInsert2Algo.store(juce::jlimit(0, mu_audio::kInsertAlgorithmCount - 1, (int) v), std::memory_order_relaxed); return true; }
    if (id == "mst_ins2P1")   { mixerEngine.masterInsert2Param[0].store(v, std::memory_order_relaxed); return true; }
    if (id == "mst_ins2P2")   { mixerEngine.masterInsert2Param[1].store(v, std::memory_order_relaxed); return true; }
    if (id == "mst_ins2P3")   { mixerEngine.masterInsert2Param[2].store(v, std::memory_order_relaxed); return true; }
    if (id == "mst_ins2P4")   { mixerEngine.masterInsert2Param[3].store(v, std::memory_order_relaxed); return true; }
    return false;
}

// rt-ok-fn: automation path, can run on the audio thread; getRawParameterValue(StringRef) is a non-allocating std::map find and ReverbSlot::setParam uses strcmp: bounded and alloc-free; a cached-pointer version is tracked
void FxParamSync::syncFxSlotParam(const juce::String& id, float v)
{
    auto& eff = fxChain.effectSlot();
    auto& dly = fxChain.delaySlot();
    auto& rev = fxChain.reverbSlot();
    auto readDenom = [this](const char* p) {
        static const int denoms[] = { 32, 16, 8, 4 };
        auto* a = apvts.getRawParameterValue(p);
        return denoms[juce::jlimit(0, 3, a ? (int) a->load() : 0)];
    };
    auto readBool = [this](const char* p) {
        auto* a = apvts.getRawParameterValue(p); return a && a->load() > 0.5f;
    };

    if      (id == "eff_algo")
    {
        eff.setAlgorithm((int) v);

        // The new algorithm starts from its own member defaults while eff_p0..4 still hold the old algorithm's
        // knob values (a host automating the algorithm leaves them alone), so the knobs and the sound disagree.
        // Push the knob values into the new algorithm. The editor's algorithm menu rewrites the knobs itself
        // right after this, which then overrides what is pushed here.
        static constexpr const char* kEffKnobIds[5] = { "eff_p0", "eff_p1", "eff_p2", "eff_p3", "eff_p4" };
        const auto& algos = FXAlgorithmRegistry::effectAlgorithms();
        const int ai = eff.getAlgorithmIndex();
        if (ai >= 0 && ai < (int) algos.size())
        {
            const auto& params = algos[(size_t) ai].params;
            for (int i = 0; i < (int) params.size() && i < 5; ++i)
                if (auto* knob = apvts.getRawParameterValue(kEffKnobIds[i]))
                    eff.setParam(params[(size_t) i].id, params[(size_t) i].actualFromNorm(knob->load()));
        }
    }
    else if (id == "eff_en")   eff.setEnabled(v > 0.5f);
    else if (id.startsWith("eff_p"))
    {
        const int idx = id.getTrailingIntValue();   // "eff_pN" → N, alloc-free (no substring)
        const auto& algos = FXAlgorithmRegistry::effectAlgorithms();
        const int ai = eff.getAlgorithmIndex();
        if (ai < (int) algos.size() && idx < (int) algos[(size_t) ai].params.size())
        {
            const auto& pd = algos[(size_t) ai].params[(size_t) idx];
            eff.setParam(pd.id, pd.actualFromNorm(v));
        }
    }
    else if (id == "dly_en")   dly.setEnabled(v > 0.5f);
    else if (id == "dly_mode") dly.setTimeMode(v > 0.5f ? DelaySlot::TimeMode::Sync : DelaySlot::TimeMode::Free);
    else if (id == "dly_ms")   dly.setDelayMs(v);
    else if (id == "dly_syncDenom" || id == "dly_syncDot" || id == "dly_syncTrip")
        dly.setTimeDivision(readDenom("dly_syncDenom"), readBool("dly_syncDot"), readBool("dly_syncTrip"));
    else if (id == "dly_count")  dly.setTimeCount(juce::jmax(1, (int) v));
    else if (id == "dly_fb")     dly.setFeedback(v);
    else if (id == "dly_spread") dly.setSpread(v);
    else if (id == "dly_dirt")   dly.setDirt(v);
    else if (id == "rev_algo")
    {
        rev.setAlgorithm((int) v);   // applies the algorithm's preset (size, damp, diffusion, mod, pre-delay)

        // As with the effect: a host automating the algorithm leaves the knobs where they were, so put the
        // knob values back into the slot. The editor's algorithm menu then rewrites the knobs from the preset.
        struct KnobToSlot { const char* apvtsId; const char* slotId; };
        static constexpr KnobToSlot kRevKnobs[] = { { "rev_size", "size" }, { "rev_decay", "decay" }, { "rev_pre", "predelay" },
                                                    { "rev_diff", "diffusion" }, { "rev_damp", "damp" },
                                                    { "rev_mod", "mod" } };
        for (const auto& k : kRevKnobs)
            if (auto* knob = apvts.getRawParameterValue(k.apvtsId))
                rev.setParam(k.slotId, knob->load());
    }
    else if (id == "rev_en")     rev.setEnabled(v > 0.5f);
    else if (id == "rev_lvl")    rev.setLevel(v);
    else if (id == "rev_size")   rev.setParam("size",      v);
    else if (id == "rev_decay")  rev.setParam("decay",     v);
    else if (id == "rev_pre")    rev.setParam("predelay",  v);
    else if (id == "rev_diff")   rev.setParam("diffusion", v);
    else if (id == "rev_damp")   rev.setParam("damp",      v);
    else if (id == "rev_mod")    rev.setParam("mod",       v);
    else if (id == "rev_dirt")   rev.setParam("dirt",      v);
    else if (id == "eff2dly")    fxChain.setEffectToDelaySend(v);
    else if (id == "eff2rev")    fxChain.setEffectToReverbSend(v);
    else if (id == "dly2rev")    fxChain.setDelayToReverbSend(v);
    else if (id == "echo_en")    eff.getEchoDelay().setEnabled(v > 0.5f);
    else if (id == "echo_mode")  eff.getEchoDelay().setTimeMode(v > 0.5f ? DelaySlot::TimeMode::Sync : DelaySlot::TimeMode::Free);
    else if (id == "echo_ms")    eff.getEchoDelay().setDelayMs(v);
    else if (id == "echo_syncDenom" || id == "echo_syncDot" || id == "echo_syncTrip")
        eff.getEchoDelay().setTimeDivision(readDenom("echo_syncDenom"), readBool("echo_syncDot"), readBool("echo_syncTrip"));
    else if (id == "echo_count")  eff.getEchoDelay().setTimeCount(juce::jmax(1, (int) v));
    else if (id == "echo_fb")     eff.getEchoDelay().setFeedback(v);
    else if (id == "echo_spread") eff.getEchoDelay().setSpread(v);
    else if (id == "echo_dirt")   eff.getEchoDelay().setDirt(v);
}

} // namespace mu_core
