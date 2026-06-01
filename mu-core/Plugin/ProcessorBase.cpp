#include "Plugin/ProcessorBase.h"
#include "Audio/FX/Slots/FXAlgorithmDef.h"   // FXAlgorithmRegistry
#include "Audio/AlgorithmNames.h"             // mu_audio::kInsertAlgorithmCount

ProcessorBase::ProcessorBase(const BusesProperties& props,
                             juce::AudioProcessorValueTreeState::ParameterLayout layout,
                             const juce::Identifier& stateTreeType)
    : juce::AudioProcessor(props),
      apvts(*this, nullptr, stateTreeType, std::move(layout))
{}

bool ProcessorBase::scanMidiProgramChanges(const juce::MidiBuffer& midi)
{
    const uint8_t chMask           = midiPresetMap.getChannelMask();
    const bool    fullPresetEnabled = midiFullPresetMap.isEnabled();
    if (chMask == 0 && ! fullPresetEnabled) return false;

    bool needPC = false;
    for (const auto& msgRef : midi)
    {
        const auto& m = msgRef.getMessage();
        if (! m.isProgramChange()) continue;
        const int ch = m.getChannel();      // 1-based, 1..16

        int  slot   = -1;
        bool isFull = false;
        if (ch >= 1 && ch <= 8)
        {
            if (! (chMask & (1 << (ch - 1)))) continue;
            slot = ch - 1;
        }
        else if (ch == MidiFullPresetMap::Channel)
        {
            if (! fullPresetEnabled) continue;
            isFull = true;
        }
        else
        {
            continue;
        }

        int start1, size1, start2, size2;
        pcFifo.prepareToWrite(1, start1, size1, start2, size2);
        if (size1 + size2 > 0)
        {
            const int dst = (size1 > 0) ? start1 : start2;
            pcQueue[(size_t) dst] = { slot, m.getProgramChangeNumber(), isFull };
            pcFifo.finishedWrite(1);
            needPC = true;
        }
    }
    return needPC;
}

void ProcessorBase::drainPendingMidiProgramChanges()
{
    const int ready = pcFifo.getNumReady();
    if (ready <= 0) return;

    int start1, size1, start2, size2;
    pcFifo.prepareToRead(ready, start1, size1, start2, size2);
    const int activeChannels = getNumActiveChannels();

    auto handle = [this, activeChannels](const ProgramChangeEvent& ev)
    {
        if (ev.fullPreset)
        {
            if (! midiFullPresetMap.hasPreset(ev.presetIndex)) return;
            const juce::File f { midiFullPresetMap.getPresetPath(ev.presetIndex) };
            if (f.existsAsFile())
                applyFullMidiPreset(f);
            return;
        }
        if (ev.slot < 0 || ev.slot >= activeChannels) return;
        if (! midiPresetMap.hasPreset(ev.presetIndex))   return;
        const juce::File f { midiPresetMap.getPresetPath(ev.presetIndex) };
        if (f.existsAsFile())
            applyMidiPresetSlot(ev.slot, f);
    };
    for (int i = 0; i < size1; ++i) handle(pcQueue[(size_t)(start1 + i)]);
    for (int i = 0; i < size2; ++i) handle(pcQueue[(size_t)(start2 + i)]);
    pcFifo.finishedRead(ready);
}

void ProcessorBase::syncGlobalFxParam(const juce::String& id, float v)
{
    // Prefix router → per-family helper. Keeps each automation tick a single
    // prefix test plus one bounded if/else inside the matching helper.
    if (id.length() >= 6 && id[0] == 'c' && id[1] == 'h' && id[3] == '_')   // ch{0-7}_{param}
    {
        const int i = id[2] - '0';
        if (i >= 0 && i < MixerEngine::MaxChannels)
            syncChannelStripParam(i, id.substring(4), v);
        return;
    }

    if (id.startsWith("ret_"))                                              // ret_{eff|dly|rev}_{param}
    {
        int retIdx = -1;
        if      (id.startsWith("ret_eff_")) retIdx = 0;
        else if (id.startsWith("ret_dly_")) retIdx = 1;
        else if (id.startsWith("ret_rev_")) retIdx = 2;
        if (retIdx >= 0)
            syncReturnStripParam(retIdx, id.substring(8), v);
        return;
    }

    if (syncMasterParam(id, v)) return;                                     // mstr_*, mst_ins*

    syncFxSlotParam(id, v);                                                 // eff_*, eff2*, dly_*, rev_*, echo_*
}

void ProcessorBase::syncChannelStripParam(int channel, const juce::String& param, float v)
{
    auto& ch = mixerEngine.channels[(size_t) channel];
    if      (param == "lvl")     ch.level.store(v, std::memory_order_relaxed);
    else if (param == "pan")     ch.pan.store(v, std::memory_order_relaxed);
    else if (param == "mute")    ch.mute.store(v > 0.5f, std::memory_order_relaxed);
    else if (param == "solo")    ch.solo.store(v > 0.5f, std::memory_order_relaxed);
    else if (param == "sendEff") ch.sendEffect.store(v, std::memory_order_relaxed);
    else if (param == "sendDly") ch.sendDelay.store(v, std::memory_order_relaxed);
    else if (param == "sendRev") ch.sendReverb.store(v, std::memory_order_relaxed);
    else if (param == "scSrc")   ch.sidechainSource.store(juce::roundToInt(v) - 1, std::memory_order_relaxed);
    else if (param == "scAmt")   ch.sidechainAmount.store(v, std::memory_order_relaxed);
    else if (param == "scAtk")   ch.sidechainAttackMs.store(v, std::memory_order_relaxed);
    else if (param == "scRel")   ch.sidechainReleaseMs.store(v, std::memory_order_relaxed);
    else if (param == "outBus")  ch.outputBus.store(juce::jlimit(0, 8, juce::roundToInt(v)), std::memory_order_relaxed);
}

void ProcessorBase::syncReturnStripParam(int retIdx, const juce::String& rest, float v)
{
    auto& ret = mixerEngine.returns[(size_t) retIdx];
    if      (rest == "lvl")   ret.level.store(v, std::memory_order_relaxed);
    else if (rest == "pan")   ret.pan.store(v, std::memory_order_relaxed);
    else if (rest == "mute")  ret.mute.store(v > 0.5f, std::memory_order_relaxed);
    else if (rest == "solo")  ret.solo.store(v > 0.5f, std::memory_order_relaxed);
    else if (rest == "scSrc") ret.sidechainSource.store(juce::jlimit(0, 9, (int) v) - 1, std::memory_order_relaxed);
    else if (rest == "scAmt") ret.sidechainAmount.store(juce::jlimit(0.0f, 1.0f, v), std::memory_order_relaxed);
    else if (rest == "scAtk") ret.sidechainAttackMs.store(v, std::memory_order_relaxed);
    else if (rest == "scRel") ret.sidechainReleaseMs.store(v, std::memory_order_relaxed);
}

bool ProcessorBase::syncMasterParam(const juce::String& id, float v)
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

void ProcessorBase::syncFxSlotParam(const juce::String& id, float v)
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

    if      (id == "eff_algo") eff.setAlgorithm((int) v);
    else if (id == "eff_en")   eff.setEnabled(v > 0.5f);
    else if (id.startsWith("eff_p"))
    {
        const int idx = id.substring(5).getIntValue();
        const auto& algos = FXAlgorithmRegistry::effectAlgorithms();
        const int ai = eff.getAlgorithmIndex();
        if (ai < (int) algos.size() && idx < (int) algos[(size_t) ai].params.size())
        {
            const auto& pd = algos[(size_t) ai].params[(size_t) idx];
            eff.setParam(pd.id, pd.minVal + v * (pd.maxVal - pd.minVal));
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
    else if (id == "rev_algo")   rev.setAlgorithm((int) v);
    else if (id == "rev_en")     rev.setEnabled(v > 0.5f);
    else if (id == "rev_lvl")    rev.setLevel(v);
    else if (id == "rev_size")   rev.setParam("size",      v);
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

void ProcessorBase::processCoreBlock(juce::AudioBuffer<float>&                masterBus,
                                     std::unique_ptr<VoiceEngine>*            voices,
                                     int                                      numVoices,
                                     int                                      numSamples,
                                     double                                   effectiveBpm,
                                     std::array<juce::AudioBuffer<float>*, MixerEngine::MaxChannels>* directOuts,
                                     juce::AudioBuffer<float>*                fxReturnsOut,
                                     const RetiredVoices*                     retired,
                                     const MixerEngine::RenderChannelFn*      renderChannel)
{
    fxChain.setHostBpm(effectiveBpm);
    mixerEngine.processBlock(masterBus, numVoices, voices, fxChain, numSamples,
                             directOuts, fxReturnsOut, retired, renderChannel);
}
