// partial-class TU split from MixerChannel.cpp. Contains the four bind* methods
// plus helpers that only run during binding (setSidechainSources, loadFromAPVTS).
// MixerChannel_Insert.cpp holds configureInsertAlgorithm.

#include "MixerChannel.h"
#include "Plugin/ProcessorBase.h"
#include "Audio/InsertSlotConfig.h"
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
        fader.onValueChange = [proc, prefix, this] {
            auto v = (float)fader.getValue();
            if (auto* p = proc->apvts.getParameter(prefix + "lvl"))
                p->setValueNotifyingHost(p->convertTo0to1(v));
            updateDbLabel(v);
        };
        panKnob.onValueChanged    = [proc, prefix](double v) {
            if (auto* p = proc->apvts.getParameter(prefix + "pan"))
                p->setValueNotifyingHost(p->convertTo0to1((float)v));
        };
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
        muteBtn.onClick = [proc, prefix, this] {
            if (auto* p = proc->apvts.getParameter(prefix + "mute"))
                p->setValueNotifyingHost(muteBtn.getToggleState() ? 1.0f : 0.0f);
        };
        soloBtn.onClick = [proc, prefix, this] {
            if (auto* p = proc->apvts.getParameter(prefix + "solo"))
                p->setValueNotifyingHost(soloBtn.getToggleState() ? 1.0f : 0.0f);
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
        // Sidechain controls write through APVTS
        if (hasSidechainControls())
        {
            scSourceBox.onChange = [proc, prefix, this] {
                int id = scSourceBox.getSelectedId();   // 1=none, 2-9=ch0-ch7
                int apvtsVal = (id <= 1) ? 0 : (id - 1);
                if (auto* p = proc->apvts.getParameter(prefix + "scSrc"))
                    p->setValueNotifyingHost(p->convertTo0to1((float)apvtsVal));
                if (onStatusUpdate) onStatusUpdate(channelName + " Sidechain", scSourceBox.getText(), channelColour);
            };
            scAmount.onValueChanged = [proc, prefix](double v) {
                if (auto* p = proc->apvts.getParameter(prefix + "scAmt"))
                    p->setValueNotifyingHost(p->convertTo0to1((float)v / 100.0f));
            };
            scAttack.onValueChanged = [proc, prefix](double v) {
                if (auto* p = proc->apvts.getParameter(prefix + "scAtk"))
                    p->setValueNotifyingHost(p->convertTo0to1((float)v));
            };
            scRelease.onValueChanged = [proc, prefix](double v) {
                if (auto* p = proc->apvts.getParameter(prefix + "scRel"))
                    p->setValueNotifyingHost(p->convertTo0to1((float)v));
            };
            // Load current values so knobs reflect saved state on re-bind
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
        fader.onValueChange = [&state, this] {
            const float lv = (float)fader.getValue();
            state.level.store(lv, std::memory_order_relaxed);
            updateDbLabel(lv);
        };
        panKnob.onValueChanged    = [&state](double v) { state.pan.store        ((float)v, std::memory_order_relaxed); };
        sendEffect.onValueChanged = [&state](double v) { state.sendEffect.store ((float)v, std::memory_order_relaxed); };
        sendDelay.onValueChanged  = [&state](double v) { state.sendDelay.store  ((float)v, std::memory_order_relaxed); };
        sendReverb.onValueChanged = [&state](double v) { state.sendReverb.store ((float)v, std::memory_order_relaxed); };
        muteBtn.onClick = [&state, this] { state.mute.store(muteBtn.getToggleState(), std::memory_order_relaxed); };
        soloBtn.onClick = [&state, this] { state.solo.store(soloBtn.getToggleState(), std::memory_order_relaxed); };
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
            scSourceBox.onChange    = [&state, this] {
                int id = scSourceBox.getSelectedId();
                state.sidechainSource.store((id <= 1) ? -1 : (id - 2), std::memory_order_relaxed);
                if (onStatusUpdate) onStatusUpdate(channelName + " Sidechain", scSourceBox.getText(), channelColour);
            };
            scAmount.onValueChanged  = [&state](double v) { state.sidechainAmount.store   ((float)v / 100.0f, std::memory_order_relaxed); };
            scAttack.onValueChanged  = [&state](double v) { state.sidechainAttackMs.store ((float)v,          std::memory_order_relaxed); };
            scRelease.onValueChanged = [&state](double v) { state.sidechainReleaseMs.store((float)v,          std::memory_order_relaxed); };
            scAmount.setValue(state.sidechainAmount * 100.0, juce::dontSendNotification);
            scAttack.setValue(state.sidechainAttackMs,       juce::dontSendNotification);
            scRelease.setValue(state.sidechainReleaseMs,     juce::dontSendNotification);
        }
    }

    vuMeter.getLevel = [&peak] { return peak.load(); };
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
        fader.onValueChange = [proc, prefix, this] {
            auto v = (float)fader.getValue();
            if (auto* p = proc->apvts.getParameter(prefix + "lvl"))
                p->setValueNotifyingHost(p->convertTo0to1(v));
            updateDbLabel(v);
        };
        panKnob.onValueChanged = [proc, prefix](double v) {
            if (auto* p = proc->apvts.getParameter(prefix + "pan"))
                p->setValueNotifyingHost(p->convertTo0to1((float)v));
        };
        muteBtn.onClick = [proc, prefix, this] {
            if (auto* p = proc->apvts.getParameter(prefix + "mute"))
                p->setValueNotifyingHost(muteBtn.getToggleState() ? 1.0f : 0.0f);
        };
        soloBtn.onClick = [proc, prefix, this] {
            if (auto* p = proc->apvts.getParameter(prefix + "solo"))
                p->setValueNotifyingHost(soloBtn.getToggleState() ? 1.0f : 0.0f);
        };
        if (hasSidechainControls())
        {
            scSourceBox.onChange = [proc, prefix, this] {
                int id = scSourceBox.getSelectedId();   // 1=none, 2-9=ch0-ch7
                int apvtsVal = (id <= 1) ? 0 : (id - 1);
                if (auto* p = proc->apvts.getParameter(prefix + "scSrc"))
                    p->setValueNotifyingHost(p->convertTo0to1((float)apvtsVal));
                if (onStatusUpdate) onStatusUpdate(channelName + " Sidechain", scSourceBox.getText(), channelColour);
            };
            scAmount.onValueChanged = [proc, prefix](double v) {
                if (auto* p = proc->apvts.getParameter(prefix + "scAmt"))
                    p->setValueNotifyingHost(p->convertTo0to1((float)v / 100.0f));
            };
            scAttack.onValueChanged = [proc, prefix](double v) {
                if (auto* p = proc->apvts.getParameter(prefix + "scAtk"))
                    p->setValueNotifyingHost(p->convertTo0to1((float)v));
            };
            scRelease.onValueChanged = [proc, prefix](double v) {
                if (auto* p = proc->apvts.getParameter(prefix + "scRel"))
                    p->setValueNotifyingHost(p->convertTo0to1((float)v));
            };
        }
    }
    else
    {
        fader.onValueChange = [&state, this] {
            const float lv = (float)fader.getValue();
            state.level.store(lv, std::memory_order_relaxed);
            updateDbLabel(lv);
        };
        panKnob.onValueChanged = [&state](double v) { state.pan.store((float)v, std::memory_order_relaxed); };
        muteBtn.onClick = [&state, this] { state.mute.store(muteBtn.getToggleState(), std::memory_order_relaxed); };
        soloBtn.onClick = [&state, this] { state.solo.store(soloBtn.getToggleState(), std::memory_order_relaxed); };
        if (hasSidechainControls())
        {
            scSourceBox.onChange    = [&state, this] {
                int id = scSourceBox.getSelectedId();
                state.sidechainSource.store((id <= 1) ? -1 : (id - 2), std::memory_order_relaxed);
                if (onStatusUpdate) onStatusUpdate(channelName + " Sidechain", scSourceBox.getText(), channelColour);
            };
            scAmount.onValueChanged  = [&state](double v) { state.sidechainAmount.store   ((float)v / 100.0f, std::memory_order_relaxed); };
            scAttack.onValueChanged  = [&state](double v) { state.sidechainAttackMs.store ((float)v,          std::memory_order_relaxed); };
            scRelease.onValueChanged = [&state](double v) { state.sidechainReleaseMs.store((float)v,          std::memory_order_relaxed); };
        }
    }

    vuMeter.getLevel = [&peak] { return peak.load(); };
    if (grAtomic)
        grMeter.getGR = [grAtomic] { return grAtomic->load(); };
    updateDbLabel(state.level);
}

void MixerChannel::bindMaster(MixerEngine& engine, ProcessorBase* proc)
{
    masterInsertProc = proc;   // keep knob lambdas alive across loadFromAPVTS rebinds
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

    vuMeter.getLevel = [&engine] { return engine.masterPeak.load(); };
    updateDbLabel(engine.masterLevel.load(std::memory_order_relaxed));

    if (hasInsert())
    {
        auto wireInsertSlot = [this, &engine, proc](int slot)
        {
            const VoiceParams ip = engine.getMasterInsertParams(slot);
            juce::ComboBox& charBox = slot == 0 ? insCharBox : insCharBox2;
            // Pointer-to-array — ternary across two different array members
            // gives us a decayed pointer, not a reference; capturing as
            // pointer keeps the type system happy.
            float (* const snaps    )[mu_ui::kInsertSlotCount] =
                slot == 0 ? insertSnapshots     : insertSnapshots2;
            bool  * const snapValid =
                slot == 0 ? insertSnapshotValid : insertSnapshotValid2;
            const juce::String pSlot[4] = {
                slot == 0 ? juce::String("mst_insP1") : juce::String("mst_ins2P1"),
                slot == 0 ? juce::String("mst_insP2") : juce::String("mst_ins2P2"),
                slot == 0 ? juce::String("mst_insP3") : juce::String("mst_ins2P3"),
                slot == 0 ? juce::String("mst_insP4") : juce::String("mst_ins2P4"),
            };

            charBox.setSelectedId(ip.insertAlgo + 1, juce::dontSendNotification);
            configureInsertAlgorithm(ip.insertAlgo, slot, proc);

            // Capture `snaps` and `snapValid` by VALUE — both are pointers into
            // the MixerChannel member arrays, so a copy is just the address. A
            // by-reference capture would dangle once `wireInsertSlot` returns
            // (the locals live in this lambda's stack frame, not on the heap),
            // and dereferencing the dangle inside `onChange` crashes the host.
            charBox.onChange = [this, proc, slot, snaps, snapValid, pSlot]()
            {
                juce::ComboBox& cb = slot == 0 ? insCharBox : insCharBox2;
                const int newChar = cb.getSelectedId() - 1;
                if (proc)
                {
                    const VoiceParams cur = proc->mixerEngine.getMasterInsertParams(slot);
                    const int oldChar = cur.insertAlgo;
                    auto set = [proc](const juce::String& id, float v)
                    {
                        if (auto* p = proc->apvts.getParameter(id))
                            p->setValueNotifyingHost(p->convertTo0to1(v));
                    };

                    if (oldChar >= 0 && oldChar < InsertProcessor::kNumInsertAlgos)
                    {
                        for (int s = 0; s < mu_ui::kInsertSlotCount; ++s)
                            snaps[oldChar][s] = mu_ui::normToActual(cur.insertParam[s], oldChar, s);
                        snapValid[oldChar] = true;
                    }

                    // Master insert doesn't ride the same RhythmPanel
                    // inline-refresh path as the per-rhythm insert —
                    // MixerOverlay's listener is a deferred apvtsDirty flag
                    // flushed by its 30 Hz timer, so the multi-write doesn't
                    // produce intermediate stale-algo UI states. drvChar is
                    // set inside configureInsertAlgorithm below.
                    for (int s = 0; s < mu_ui::kInsertSlotCount; ++s)
                    {
                        const float actual = snapValid[newChar]
                            ? snaps[newChar][s]
                            : mu_ui::kInsertAlgoDefaults[newChar][s];
                        set(pSlot[s], mu_ui::actualToNorm(actual, newChar, s));
                    }
                }
                configureInsertAlgorithm(newChar, slot, proc);
            };
        };

        wireInsertSlot(0);
        wireInsertSlot(1);
    }
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
void MixerChannel::setSidechainSources(int ownIdx, const juce::StringArray& names)
{
    if (!hasSidechainControls()) return;
    const int prevId = scSourceBox.getSelectedId();
    scSourceBox.clear(juce::dontSendNotification);
    scSourceBox.addItem(juce::String::charToString(0x2014), 1);  // "—"
    for (int i = 0; i < names.size(); ++i)
    {
        if (i == ownIdx) continue;
        const juce::String label = names[i].isEmpty() ? ("Ch " + juce::String(i + 1))
                                                      : names[i];
        scSourceBox.addItem(label, i + 2);  // ID = channel index + 2
    }
    // External DAW sidechain bus — ID = MaxChannels + 2 (maps to apvts=9, engine=kExtSidechainSrc).
    scSourceBox.addItem("Ext In", MixerEngine::MaxChannels + 2);
    // Restore previous selection if still valid, else default to none
    if (prevId > 1 && scSourceBox.indexOfItemId(prevId) >= 0)
        scSourceBox.setSelectedId(prevId, juce::dontSendNotification);
    else
        scSourceBox.setSelectedId(1, juce::dontSendNotification);
}

//==============================================================================
void MixerChannel::loadFromAPVTS(juce::AudioProcessorValueTreeState& apvts,
                                  const juce::String& prefix)
{
    if (hasInsert())
    {
        // Master insert — uses dedicated mst_ins* params, not a prefix.
        if (auto* p = apvts.getRawParameterValue("mstr_lvl"))
        {
            fader.setValue(*p, juce::dontSendNotification);
            updateDbLabel(*p);
        }
        if (auto* p = apvts.getRawParameterValue("mstr_pan"))
            panKnob.setValue(*p, juce::dontSendNotification);

        if (auto* p = apvts.getRawParameterValue("mst_insChar"))
        {
            const int charId = juce::jlimit(0, InsertProcessor::kNumInsertAlgos - 1, juce::roundToInt((float)*p));
            insCharBox.setSelectedId(charId + 1, juce::dontSendNotification);
            configureInsertAlgorithm(charId, 0, nullptr);
        }
        if (auto* p = apvts.getRawParameterValue("mst_ins2Char"))
        {
            const int charId = juce::jlimit(0, InsertProcessor::kNumInsertAlgos - 1, juce::roundToInt((float)*p));
            insCharBox2.setSelectedId(charId + 1, juce::dontSendNotification);
            configureInsertAlgorithm(charId, 1, nullptr);
        }
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

