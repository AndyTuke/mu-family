// partial-class TU split from MixerChannel.cpp. Contains the four bind* methods
// plus helpers that only run during binding (setSidechainSources, loadFromApvts).

#include "MixerChannel.h"
#include "Plugin/ProcessorBase.h"
#include "Audio/InsertSlotConfig.h"
// The controls every strip with a fader shares — level, pan, mute, solo and the sidechain —
// writing their `<prefix>*` parameters through APVTS.
void MixerChannel::bindStripToApvts(ProcessorBase* proc, const juce::String& prefix)
{
    auto write = [proc, prefix](const char* id, float actual)
    {
        if (auto* p = proc->apvts.getParameter(prefix + id))
            p->setValueNotifyingHost(p->convertTo0to1(actual));
    };
    fader.onValueChange = [write, this] {
        const auto v = (float) fader.getValue();
        write("lvl", v);
        updateDbLabel(v);
    };
    panKnob.onValueChanged = [write](double v) { write("pan", (float) v); };
    muteBtn.onClick = [write, this] { write("mute", muteBtn.getToggleState() ? 1.0f : 0.0f); };
    soloBtn.onClick = [write, this] { write("solo", soloBtn.getToggleState() ? 1.0f : 0.0f); };
    if (! hasSidechainControls()) return;
    scSourceBox.onChange = [write, this] {
        const int id = scSourceBox.getSelectedId();   // 1 = none, 2.. = ch0.. → param 0 / 1..; the external bus is last
        write("scSrc", (float) (id <= 1 ? 0 : id - 1));
        if (onStatusUpdate) onStatusUpdate(channelName + " Sidechain", scSourceBox.getText(), channelColour);
    };
    scAmount.onValueChanged  = [write](double v) { write("scAmt", (float) v / 100.0f); };
    scAttack.onValueChanged  = [write](double v) { write("scAtk", (float) v); };
    scRelease.onValueChanged = [write](double v) { write("scRel", (float) v); };
}

// The same controls writing straight into the engine state (no processor: tests / previews).
template <typename State>
void MixerChannel::bindStripToState(State& state)
{
    fader.onValueChange = [&state, this] {
        const float lv = (float) fader.getValue();
        state.level.store(lv, std::memory_order_relaxed);
        updateDbLabel(lv);
    };
    panKnob.onValueChanged = [&state](double v) { state.pan.store((float) v, std::memory_order_relaxed); };
    muteBtn.onClick = [&state, this] { state.mute.store(muteBtn.getToggleState(), std::memory_order_relaxed); };
    soloBtn.onClick = [&state, this] { state.solo.store(soloBtn.getToggleState(), std::memory_order_relaxed); };
    if (! hasSidechainControls()) return;
    scSourceBox.onChange = [&state, this] {
        const int id = scSourceBox.getSelectedId();
        state.sidechainSource.store(id <= 1 ? -1 : id - 2, std::memory_order_relaxed);
        if (onStatusUpdate) onStatusUpdate(channelName + " Sidechain", scSourceBox.getText(), channelColour);
    };
    scAmount.onValueChanged  = [&state](double v) { state.sidechainAmount.store   ((float) v / 100.0f, std::memory_order_relaxed); };
    scAttack.onValueChanged  = [&state](double v) { state.sidechainAttackMs.store ((float) v,          std::memory_order_relaxed); };
    scRelease.onValueChanged = [&state](double v) { state.sidechainReleaseMs.store((float) v,          std::memory_order_relaxed); };
}

void MixerChannel::bindChannel(MixerEngine::ChannelState& state, std::atomic<float>& peak,
                               ProcessorBase* proc, const juce::String& prefix,
                               std::atomic<float>* grAtomic)
{
    fader.setValue(state.level, juce::dontSendNotification);
    panKnob.setValue(state.pan, juce::dontSendNotification);
    sendEffect.setValue(state.sendEffect, juce::dontSendNotification);
    sendDelay.setValue (state.sendDelay,  juce::dontSendNotification);
    sendReverb.setValue(state.sendReverb, juce::dontSendNotification);
    muteBtn.setToggleState(state.mute, juce::dontSendNotification);
    soloBtn.setToggleState(state.solo, juce::dontSendNotification);

    if (proc)
    {
        bindStripToApvts(proc, prefix);
        sendEffect.onValueChanged = [proc, prefix](double v) {
            if (auto* p = proc->apvts.getParameter(prefix + "sendEff"))
                p->setValueNotifyingHost(p->convertTo0to1((float)v));
        };
        sendDelay.onValueChanged  = [proc, prefix](double v) {
            if (auto* p = proc->apvts.getParameter(prefix + "sendDly"))
                p->setValueNotifyingHost(p->convertTo0to1((float)v));
        };
        sendReverb.onValueChanged = [proc, prefix](double v) {
            if (auto* p = proc->apvts.getParameter(prefix + "sendRev"))
                p->setValueNotifyingHost(p->convertTo0to1((float)v));
        };
        if (hasOutputBus())
        {
            outBusBox.setSelectedId(state.outputBus + 1, juce::dontSendNotification);
            outBusBox.onChange = [proc, prefix, this] {
                const int id = outBusBox.getSelectedId();   // 1 = Master, 2..9 = Out 1..8
                if (auto* p = proc->apvts.getParameter(prefix + "outBus"))
                    p->setValueNotifyingHost(p->convertTo0to1((float)(id - 1)));
                if (onStatusUpdate) onStatusUpdate(channelName + " Output", outBusBox.getText(), channelColour);
            };
        }
        if (hasSidechainControls())
        {
            // Load the sidechain's current values so knobs reflect saved state on re-bind
            if (auto* p = proc->apvts.getRawParameterValue(prefix + "scAmt"))
                scAmount.setValue(*p * 100.0, juce::dontSendNotification);
            if (auto* p = proc->apvts.getRawParameterValue(prefix + "scAtk"))
                scAttack.setValue(*p, juce::dontSendNotification);
            if (auto* p = proc->apvts.getRawParameterValue(prefix + "scRel"))
                scRelease.setValue(*p, juce::dontSendNotification);
        }
    }
    else
    {
        bindStripToState(state);
        sendEffect.onValueChanged = [&state](double v) { state.sendEffect.store ((float)v, std::memory_order_relaxed); };
        sendDelay.onValueChanged  = [&state](double v) { state.sendDelay.store  ((float)v, std::memory_order_relaxed); };
        sendReverb.onValueChanged = [&state](double v) { state.sendReverb.store ((float)v, std::memory_order_relaxed); };
        if (hasOutputBus())
        {
            outBusBox.setSelectedId(state.outputBus + 1, juce::dontSendNotification);
            outBusBox.onChange = [&state, this] {
                state.outputBus.store(juce::jlimit(0, 8, outBusBox.getSelectedId() - 1), std::memory_order_relaxed);
                if (onStatusUpdate) onStatusUpdate(channelName + " Output", outBusBox.getText(), channelColour);
            };
        }
        if (hasSidechainControls())
        {
            scAmount.setValue(state.sidechainAmount * 100.0, juce::dontSendNotification);
            scAttack.setValue(state.sidechainAttackMs,       juce::dontSendNotification);
            scRelease.setValue(state.sidechainReleaseMs,     juce::dontSendNotification);
        }
    }

    vuMeter.getLevel  = [&peak] { return peak.load(); };
    vuMeter.getLevelR = nullptr;   // one source drives both bars
    if (grAtomic)
        grMeter.getGR = [grAtomic] { return grAtomic->load(); };
    updateDbLabel(state.level);
}

void MixerChannel::bindReturn(MixerEngine::ReturnState& state, std::atomic<float>& peak,
                               ProcessorBase* proc, const juce::String& prefix,
                               std::atomic<float>* grAtomic)
{
    fader.setValue(state.level, juce::dontSendNotification);
    panKnob.setValue(state.pan, juce::dontSendNotification);
    muteBtn.setToggleState(state.mute, juce::dontSendNotification);
    soloBtn.setToggleState(state.solo, juce::dontSendNotification);

    if (hasSidechainControls())
    {
        scAmount.setValue(state.sidechainAmount * 100.0, juce::dontSendNotification);
        scAttack.setValue(state.sidechainAttackMs,       juce::dontSendNotification);
        scRelease.setValue(state.sidechainReleaseMs,     juce::dontSendNotification);
    }

    if (proc)
    {
        bindStripToApvts(proc, prefix);
    }
    else
    {
        bindStripToState(state);
    }

    vuMeter.getLevel  = [&peak] { return peak.load(); };
    vuMeter.getLevelR = nullptr;   // one source drives both bars
    if (grAtomic)
        grMeter.getGR = [grAtomic] { return grAtomic->load(); };
    updateDbLabel(state.level);
}

void MixerChannel::bindMaster(MixerEngine& engine, ProcessorBase* proc)
{
    fader.setValue(engine.masterLevel.load(std::memory_order_relaxed), juce::dontSendNotification);
    panKnob.setValue(engine.masterPan.load(std::memory_order_relaxed), juce::dontSendNotification);

    if (proc)
    {
        fader.onValueChange = [proc, this] {
            auto v = (float)fader.getValue();
            if (auto* p = proc->apvts.getParameter("mstr_lvl"))
                p->setValueNotifyingHost(p->convertTo0to1(v));
            updateDbLabel(v);
        };
        panKnob.onValueChanged = [proc](double v) {
            if (auto* p = proc->apvts.getParameter("mstr_pan"))
                p->setValueNotifyingHost(p->convertTo0to1((float)v));
        };
    }
    else
    {
        fader.onValueChange = [&engine, this] {
            const float v = (float)fader.getValue();
            engine.masterLevel.store(v, std::memory_order_relaxed);
            updateDbLabel(v);
        };
        panKnob.onValueChanged = [&engine](double v) { engine.masterPan.store((float)v, std::memory_order_relaxed); };
    }

    vuMeter.getLevel  = [&engine] { return engine.masterPeakL.load(); };
    vuMeter.getLevelR = [&engine] { return engine.masterPeakR.load(); };
    updateDbLabel(engine.masterLevel.load(std::memory_order_relaxed));
}

void MixerChannel::bindReturnSends(juce::AudioProcessorValueTreeState& apvts,
                                    const juce::String& dlySendParam,
                                    const juce::String& revSendParam)
{
    if (dlySendParam.isNotEmpty())
    {
        if (auto* raw = apvts.getRawParameterValue(dlySendParam))
            sendDelay.setValue(*raw, juce::dontSendNotification);
        sendDelay.onValueChanged = [&apvts, dlySendParam](double v) {
            if (auto* p = apvts.getParameter(dlySendParam))
                p->setValueNotifyingHost(p->convertTo0to1((float)v));
        };
    }
    if (revSendParam.isNotEmpty())
    {
        if (auto* raw = apvts.getRawParameterValue(revSendParam))
            sendReverb.setValue(*raw, juce::dontSendNotification);
        sendReverb.onValueChanged = [&apvts, revSendParam](double v) {
            if (auto* p = apvts.getParameter(revSendParam))
                p->setValueNotifyingHost(p->convertTo0to1((float)v));
        };
    }
}

//==============================================================================
void MixerChannel::setSidechainSources(int ownIndex, const juce::StringArray& names)
{
    if (!hasSidechainControls()) return;
    const int prevId = scSourceBox.getSelectedId();
    scSourceBox.clear(juce::dontSendNotification);
    scSourceBox.addItem(juce::String::charToString(0x2014), 1);  // "—"
    for (int i = 0; i < names.size(); ++i)
    {
        if (i == ownIndex) continue;
        const juce::String label = names[i].isEmpty() ? ("Ch " + juce::String(i + 1))
                                                      : names[i];
        scSourceBox.addItem(label, i + 2);  // ID = channel index + 2
    }
    // External DAW sidechain bus — ID = MaxChannels + 2 (maps to apvts = kMaxLayers + 1, engine = kExtSidechainSrc).
    scSourceBox.addItem("Ext In", MixerEngine::MaxChannels + 2);
    // Restore previous selection if still valid, else default to none
    if (prevId > 1 && scSourceBox.indexOfItemId(prevId) >= 0)
        scSourceBox.setSelectedId(prevId, juce::dontSendNotification);
    else
        scSourceBox.setSelectedId(1, juce::dontSendNotification);
}

//==============================================================================
void MixerChannel::loadFromApvts(juce::AudioProcessorValueTreeState& apvts,
                                  const juce::String& prefix)
{
    if (isMaster())
    {
        // The master strip's own level and pan; its insert column is a separate panel.
        if (auto* p = apvts.getRawParameterValue("mstr_lvl"))
        {
            fader.setValue(*p, juce::dontSendNotification);
            updateDbLabel(*p);
        }
        if (auto* p = apvts.getRawParameterValue("mstr_pan"))
            panKnob.setValue(*p, juce::dontSendNotification);
        return;
    }

    if (auto* p = apvts.getRawParameterValue(prefix + "lvl"))
    {
        fader.setValue(*p, juce::dontSendNotification);
        updateDbLabel(*p);
    }
    if (auto* p = apvts.getRawParameterValue(prefix + "pan"))
        panKnob.setValue(*p, juce::dontSendNotification);

    if (hasSends())
    {
        if (auto* p = apvts.getRawParameterValue(prefix + "sendEff"))
            sendEffect.setValue(*p, juce::dontSendNotification);
        if (auto* p = apvts.getRawParameterValue(prefix + "sendDly"))
            sendDelay.setValue(*p,  juce::dontSendNotification);
        if (auto* p = apvts.getRawParameterValue(prefix + "sendRev"))
            sendReverb.setValue(*p, juce::dontSendNotification);
    }
    if (hasMuteSolo())
    {
        if (auto* p = apvts.getRawParameterValue(prefix + "mute"))
            muteBtn.setToggleState(*p > 0.5f, juce::dontSendNotification);
        if (auto* p = apvts.getRawParameterValue(prefix + "solo"))
            soloBtn.setToggleState(*p > 0.5f, juce::dontSendNotification);
    }
    if (hasOutputBus())
    {
        if (auto* p = apvts.getRawParameterValue(prefix + "outBus"))
            outBusBox.setSelectedId(juce::jlimit(0, 8, juce::roundToInt((float) *p)) + 1,
                                    juce::dontSendNotification);
    }
    if (hasSidechainControls())
    {
        if (auto* p = apvts.getRawParameterValue(prefix + "scSrc"))
        {
            int apvtsVal = juce::roundToInt((float)*p);
            scSourceBox.setSelectedId(apvtsVal + 1, juce::dontSendNotification);
        }
        if (auto* p = apvts.getRawParameterValue(prefix + "scAmt"))
            scAmount.setValue(*p * 100.0, juce::dontSendNotification);
        if (auto* p = apvts.getRawParameterValue(prefix + "scAtk"))
            scAttack.setValue(*p, juce::dontSendNotification);
        if (auto* p = apvts.getRawParameterValue(prefix + "scRel"))
            scRelease.setValue(*p, juce::dontSendNotification);
    }
}

