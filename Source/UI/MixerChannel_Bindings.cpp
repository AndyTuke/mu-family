// #408: partial-class TU split from MixerChannel.cpp (which was 1107 lines and the
// largest file in the codebase post-#365). Contains the four bind* methods plus the
// related helpers that only run during binding (setSidechainSources, loadFromAPVTS).
// Mirrors the #365 PluginProcessor split pattern — same class definition, methods
// distributed across TUs. MixerChannel_Insert.cpp holds configureInsertAlgorithm.

#include "MixerChannel.h"
#include "../PluginProcessor.h"
void MixerChannel::bindRhythm(MixerEngine::ChannelState& state, std::atomic<float>& peak,
                               PluginProcessor* proc, const juce::String& prefix,
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
        }
    }
    else
    {
        fader.onValueChange = [&state, this] {
            state.level = (float)fader.getValue();
            updateDbLabel(state.level);
        };
        panKnob.onValueChanged    = [&state](double v) { state.pan        = (float)v; };
        sendEffect.onValueChanged = [&state](double v) { state.sendEffect = (float)v; };
        sendDelay.onValueChanged  = [&state](double v) { state.sendDelay  = (float)v; };
        sendReverb.onValueChanged = [&state](double v) { state.sendReverb = (float)v; };
        muteBtn.onClick = [&state, this] { state.mute = muteBtn.getToggleState(); };
        soloBtn.onClick = [&state, this] { state.solo = soloBtn.getToggleState(); };
        if (hasOutputBus())
        {
            outBusBox.setSelectedId(state.outputBus + 1, juce::dontSendNotification);
            outBusBox.onChange = [&state, this] {
                state.outputBus = juce::jlimit(0, 8, outBusBox.getSelectedId() - 1);
                if (onStatusUpdate) onStatusUpdate(channelName + " Output", outBusBox.getText(), channelColour);
            };
        }
        if (hasSidechainControls())
        {
            scSourceBox.onChange    = [&state, this] {
                int id = scSourceBox.getSelectedId();
                state.sidechainSource = (id <= 1) ? -1 : (id - 2);
                if (onStatusUpdate) onStatusUpdate(channelName + " Sidechain", scSourceBox.getText(), channelColour);
            };
            scAmount.onValueChanged  = [&state](double v) { state.sidechainAmount   = (float)v / 100.0f; };
            scAttack.onValueChanged  = [&state](double v) { state.sidechainAttackMs  = (float)v; };
            scRelease.onValueChanged = [&state](double v) { state.sidechainReleaseMs = (float)v; };
        }
    }

    vuMeter.getLevel = [&peak] { return peak.load(); };
    if (grAtomic)
        grMeter.getGR = [grAtomic] { return grAtomic->load(); };
    updateDbLabel(state.level);
}

void MixerChannel::bindReturn(MixerEngine::ReturnState& state, std::atomic<float>& peak,
                               PluginProcessor* proc, const juce::String& prefix,
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
            state.level = (float)fader.getValue();
            updateDbLabel(state.level);
        };
        panKnob.onValueChanged = [&state](double v) { state.pan  = (float)v; };
        muteBtn.onClick = [&state, this] { state.mute = muteBtn.getToggleState(); };
        soloBtn.onClick = [&state, this] { state.solo = soloBtn.getToggleState(); };
        if (hasSidechainControls())
        {
            scSourceBox.onChange    = [&state, this] {
                int id = scSourceBox.getSelectedId();
                state.sidechainSource = (id <= 1) ? -1 : (id - 2);
                if (onStatusUpdate) onStatusUpdate(channelName + " Sidechain", scSourceBox.getText(), channelColour);
            };
            scAmount.onValueChanged  = [&state](double v) { state.sidechainAmount   = (float)v / 100.0f; };
            scAttack.onValueChanged  = [&state](double v) { state.sidechainAttackMs  = (float)v; };
            scRelease.onValueChanged = [&state](double v) { state.sidechainReleaseMs = (float)v; };
        }
    }

    vuMeter.getLevel = [&peak] { return peak.load(); };
    if (grAtomic)
        grMeter.getGR = [grAtomic] { return grAtomic->load(); };
    updateDbLabel(state.level);
}

void MixerChannel::bindMaster(MixerEngine& engine, PluginProcessor* proc)
{
    masterInsertProc = proc;   // #243 — keep knob lambdas alive across loadFromAPVTS rebinds
    fader.setValue(engine.masterLevel, juce::dontSendNotification);
    panKnob.setValue(engine.masterPan, juce::dontSendNotification);

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
            engine.masterLevel = (float)fader.getValue();
            updateDbLabel(engine.masterLevel);
        };
        panKnob.onValueChanged = [&engine](double v) { engine.masterPan = (float)v; };
    }

    vuMeter.getLevel = [&engine] { return engine.masterPeak.load(); };
    updateDbLabel(engine.masterLevel);

    if (hasInsert())
    {
        auto wireInsertSlot = [this, &engine, proc](int slot)
        {
            const VoiceParams& ip = slot == 0 ? engine.masterInsertParams
                                              : engine.masterInsertParams2;
            juce::ComboBox& charBox = slot == 0 ? insCharBox : insCharBox2;
            InsertAlgoSnapshot* snaps      = slot == 0 ? insertSnapshots  : insertSnapshots2;
            bool*               snapValid  = slot == 0 ? insertSnapshotValid : insertSnapshotValid2;
            const juce::String  pDrv  = slot == 0 ? "mst_insDrv"  : "mst_ins2Drv";
            const juce::String  pOut  = slot == 0 ? "mst_insOut"   : "mst_ins2Out";
            const juce::String  pDit  = slot == 0 ? "mst_insDit"   : "mst_ins2Dit";
            const juce::String  pTon  = slot == 0 ? "mst_insTon"   : "mst_ins2Ton";
            const juce::String  pMid  = slot == 0 ? "mst_insMid"   : "mst_ins2Mid";
            const juce::String  pBit  = slot == 0 ? "mst_insBits"  : "mst_ins2Bits";
            const juce::String  pRte  = slot == 0 ? "mst_insRate"  : "mst_ins2Rate";

            charBox.setSelectedId(ip.driveChar + 1, juce::dontSendNotification);
            configureInsertAlgorithm(ip.driveChar, slot, proc);

            charBox.onChange = [this, proc, slot, snaps, snapValid,
                                pDrv, pOut, pDit, pTon, pMid, pBit, pRte]()
            {
                juce::ComboBox& cb = slot == 0 ? insCharBox : insCharBox2;
                const int newChar = cb.getSelectedId() - 1;
                if (proc)
                {
                    const VoiceParams& cur = slot == 0 ? proc->mixerEngine.masterInsertParams
                                                       : proc->mixerEngine.masterInsertParams2;
                    const int oldChar = cur.driveChar;
                    auto set = [proc](const juce::String& id, float v)
                    {
                        if (auto* p = proc->apvts.getParameter(id))
                            p->setValueNotifyingHost(p->convertTo0to1(v));
                    };

                    if (oldChar >= 0 && oldChar <= 10)
                    {
                        auto& snap       = snaps[oldChar];
                        snap.driveDrive  = cur.driveDrive;
                        snap.driveOutput = cur.driveOutput;
                        snap.drvDither   = cur.drvDither;
                        snap.driveTone   = cur.driveTone;
                        snap.eqMidGain   = cur.eqMidGain;
                        snap.drvBits     = cur.drvBits;
                        snap.driveRate   = cur.driveRate;
                        snapValid[oldChar] = true;
                    }

                    // #435: defaults shared with VoiceSection via UI/InsertAlgoDefaults.h.
                    const InsertAlgoSnapshot& snap = snapValid[newChar]
                                                     ? snaps[newChar]
                                                     : mu_ui::kInsertAlgoDefaults[newChar];
                    set(pDrv, snap.driveDrive);
                    set(pOut, snap.driveOutput);
                    set(pDit, snap.drvDither);
                    set(pTon, snap.driveTone);
                    set(pMid, snap.eqMidGain);
                    set(pBit, snap.drvBits);
                    set(pRte, snap.driveRate);
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
            const int charId = juce::jlimit(0, 10, juce::roundToInt((float)*p));
            insCharBox.setSelectedId(charId + 1, juce::dontSendNotification);
            configureInsertAlgorithm(charId, 0, nullptr);
        }
        if (auto* p = apvts.getRawParameterValue("mst_ins2Char"))
        {
            const int charId = juce::jlimit(0, 10, juce::roundToInt((float)*p));
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

