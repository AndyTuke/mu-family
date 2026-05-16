#include "MixerChannel.h"
#include "../PluginProcessor.h"
#include <cmath>

// First-visit defaults for each insert algorithm.  Fields map to VoiceParams members:
// driveDrive | driveOutput | drvDither | driveTone | eqMidGain | drvBits | driveRate
const MixerChannel::InsertAlgoSnapshot MixerChannel::kInsertDefaults[11] = {
    { 0.0f,   0.0f, 0.0f,   20000.0f, 0.0f, 16.0f, 48000.0f },  // 0  None
    { 0.0f,   0.0f, 0.0f,   20000.0f, 0.0f, 16.0f, 48000.0f },  // 1  Soft Clip  (0% drive = transparent)
    { 0.0f,   0.0f, 0.0f,   20000.0f, 0.0f, 16.0f, 48000.0f },  // 2  Hard Clip
    { 0.0f,   0.0f, 0.0f,   20000.0f, 0.0f, 16.0f, 48000.0f },  // 3  Fold
    { 0.0f,   0.0f, 0.0f,   20000.0f, 0.0f, 16.0f, 48000.0f },  // 4  Bitcrusher (16-bit, 48 kHz, flat)
    { 100.0f, 0.0f, 0.0f,   20000.0f, 0.0f, 16.0f, 48000.0f },  // 5  Clipper    (100% = full range, no clipping)
    { 50.0f,  0.0f, 50.0f,  1000.0f,  0.0f, 16.0f, 48000.0f },  // 6  EQ         (0 dB all bands, 1 kHz mid)
    { 30.0f,  0.0f, 0.0f,    200.0f,  0.0f, 16.0f, 48000.0f },  // 7  Compressor (−12 dB thresh, 200 ms release)
    { 30.0f,  0.0f, 0.0f,    200.0f,  0.0f, 16.0f, 48000.0f },  // 8  Limiter    (−12 dB ceiling, 200 ms)
    { 50.0f,  0.0f, 0.0f,    440.0f,  0.0f, 16.0f, 48000.0f },  // 9  Ring Mod   (50% mix, 440 Hz)
    { 0.0f,   0.0f, 0.0f,   20000.0f, 0.0f, 16.0f, 48000.0f },  // 10 Tape Sat  (0% drive = transparent)
};

MixerChannel::MixerChannel(Type t, const juce::String& name, juce::Colour col)
    : channelType(t), channelName(name), channelColour(col)
{
    fader.setSliderStyle(juce::Slider::LinearVertical);
    fader.setRange(0.0, 1.0, 0.001);
    fader.setValue(0.75, juce::dontSendNotification);
    fader.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    addAndMakeVisible(fader);
    addAndMakeVisible(vuMeter);
    if (hasSidechainControls())
        addAndMakeVisible(grMeter);

    panKnob.setRange(-1.0, 1.0, 0.01);
    panKnob.setValue(0.0);
    panKnob.getSlider().textFromValueFunction = [](double) { return juce::String(); };
    addAndMakeVisible(panKnob);

    if (hasSends())
    {
        sendEffect.setRange(0.0, 1.0, 0.01);
        sendDelay.setRange (0.0, 1.0, 0.01);
        sendReverb.setRange(0.0, 1.0, 0.01);

        auto noValue = [](double) { return juce::String(); };
        sendEffect.getSlider().textFromValueFunction = noValue;
        sendDelay.getSlider().textFromValueFunction  = noValue;
        sendReverb.getSlider().textFromValueFunction = noValue;

        addAndMakeVisible(sendEffect);
        addAndMakeVisible(sendDelay);
        addAndMakeVisible(sendReverb);
    }

    dbLabel.setJustificationType(juce::Justification::centred);
    dbLabel.setFont(juce::Font(juce::FontOptions{}.withHeight(9.0f)));
    addAndMakeVisible(dbLabel);

    if (hasMuteSolo())
    {
        muteBtn.setClickingTogglesState(true);
        soloBtn.setClickingTogglesState(true);
        addAndMakeVisible(muteBtn);
        addAndMakeVisible(soloBtn);
    }

    if (hasOutputBus())
    {
        // #238: descriptive labels — "Main" + "Out 1" … "Out 8".
        outBusBox.addItem("Main", 1);              // 0 -> Master
        for (int i = 1; i <= 8; ++i)
            outBusBox.addItem("Out " + juce::String(i), i + 1);
        outBusBox.setSelectedId(1, juce::dontSendNotification);
        addAndMakeVisible(outBusBox);
    }

    if (hasSidechainControls())
    {
        scSourceBox.addItem(juce::String::charToString(0x2014), 1); // "—"
        scSourceBox.setSelectedId(1, juce::dontSendNotification);
        addAndMakeVisible(scSourceBox);

        auto noVal = [](double) { return juce::String(); };

        scAmount.setRange(0.0, 100.0, 1.0);
        scAmount.setValue(0.0);
        scAmount.getSlider().textFromValueFunction = noVal;
        addAndMakeVisible(scAmount);

        scAttack.setRange(1.0, 500.0, 1.0);
        scAttack.getSlider().setSkewFactorFromMidPoint(22.0);
        scAttack.setValue(5.0);
        scAttack.getSlider().textFromValueFunction = noVal;
        addAndMakeVisible(scAttack);

        scRelease.setRange(10.0, 2000.0, 1.0);
        scRelease.getSlider().setSkewFactorFromMidPoint(141.0);
        scRelease.setValue(100.0);
        scRelease.getSlider().textFromValueFunction = noVal;
        addAndMakeVisible(scRelease);
    }

    if (hasInsert())
    {
        auto addInsertCombo = [](juce::ComboBox& box) {
            box.addItem("None",       1);
            box.addItem("3-Band EQ",  7);
            box.addItem("Bitcrusher", 5);
            box.addItem("Clipper",    6);
            box.addItem("Compressor", 8);
            box.addItem("Fold",       4);
            box.addItem("Hard Clip",  3);
            box.addItem("Limiter",    9);
            box.addItem("Ring Mod",  10);
            box.addItem("Soft Clip",  2);
            box.addItem("Tape Sat",  11);
            box.setSelectedId(1, juce::dontSendNotification);
        };
        addInsertCombo(insCharBox);
        addInsertCombo(insCharBox2);
        addAndMakeVisible(insCharBox);
        addAndMakeVisible(insCharBox2);

        auto noVal = [](double) { return juce::String(); };
        for (auto* k : { &insDrive, &insDrive2 })
        {
            k->setRange(0.0, 100.0, 0.1);
            k->setValue(0.0);
            k->getSlider().textFromValueFunction = noVal;
            addAndMakeVisible(*k);
        }
        for (auto* k : { &insOutput, &insOutput2 })
        {
            k->setRange(-24.0, 0.0, 0.1);
            k->setValue(0.0);
            k->getSlider().textFromValueFunction = noVal;
            addAndMakeVisible(*k);
        }
        for (auto* k : { &insTone, &insTone2 })
        {
            k->setRange(20.0, 20000.0, 1.0);
            k->setValue(20000.0);
            k->getSlider().setSkewFactorFromMidPoint(640.0);   // #289
            k->getSlider().textFromValueFunction = noVal;
            addAndMakeVisible(*k);
        }
        for (auto* k : { &insExtra, &insExtra2 })
        {
            k->setRange(200.0, 8000.0, 1.0);
            k->getSlider().setSkewFactorFromMidPoint(1000.0);
            k->setValue(1000.0);
            k->getSlider().textFromValueFunction = noVal;
            k->setVisible(false);
            addAndMakeVisible(*k);
        }
    }
}

//==============================================================================
void MixerChannel::bindRhythm(MixerEngine::ChannelState& state, juce::Atomic<float>& peak,
                               PluginProcessor* proc, const juce::String& prefix,
                               juce::Atomic<float>* grAtomic)
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

    vuMeter.getLevel = [&peak] { return peak.get(); };
    if (grAtomic)
        grMeter.getGR = [grAtomic] { return grAtomic->get(); };
    updateDbLabel(state.level);
}

void MixerChannel::bindReturn(MixerEngine::ReturnState& state, juce::Atomic<float>& peak,
                               PluginProcessor* proc, const juce::String& prefix,
                               juce::Atomic<float>* grAtomic)
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

    vuMeter.getLevel = [&peak] { return peak.get(); };
    if (grAtomic)
        grMeter.getGR = [grAtomic] { return grAtomic->get(); };
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

    vuMeter.getLevel = [&engine] { return engine.masterPeak.get(); };
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

                    const InsertAlgoSnapshot& snap = snapValid[newChar]
                                                     ? snaps[newChar]
                                                     : kInsertDefaults[newChar];
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

//==============================================================================
void MixerChannel::updateDbLabel(float level)
{
    if (level <= 0.0f)
        dbLabel.setText("-inf", juce::dontSendNotification);
    else
        dbLabel.setText(juce::String(20.0f * std::log10(level), 1) + "dB",
                        juce::dontSendNotification);
}

void MixerChannel::resized()
{
    const int w      = getWidth();
    const int h      = getHeight();

    // Master: right portion is the insert panel; everything else uses the strip width.
    const int insW   = hasInsert() ? kInsertPanelW : 0;
    const int stripW = w - insW;

    const int nameBottom = kColourBarH + kNameH;   // y=25

    // ── Sidechain section (Rhythm + Returns): ~20% of strip height, min = kSidechainH ──
    const int scH = hasSidechainControls()
        ? juce::jmax(kSidechainH, juce::roundToInt(h * 0.20f))
        : 0;
    if (hasSidechainControls())
    {
        const int scRemain  = scH - kScSrcH;
        const int scAmtH_l  = juce::jmax(kScAmtH, juce::roundToInt(scRemain * 0.55f));
        const int scEnvH_l  = scH - kScSrcH - scAmtH_l;

        scSourceBox.setBounds(0, nameBottom, stripW, kScSrcH);
        scAmount   .setBounds(0, nameBottom + kScSrcH, stripW, scAmtH_l);
        const int hw = stripW / 2;
        scAttack .setBounds(0,  nameBottom + kScSrcH + scAmtH_l, hw,          scEnvH_l);
        scRelease.setBounds(hw, nameBottom + kScSrcH + scAmtH_l, stripW - hw, scEnvH_l);

        sidechainPaneBounds = { 1, nameBottom + 1, stripW - 2, scH - 2 };
    }
    else
    {
        sidechainPaneBounds = {};
    }

    // ── Sends + pan: ~35% of strip height ────────────────────────────────────
    const int sendY  = nameBottom + scH;
    const int spH    = juce::jmax(4 * 36, juce::roundToInt(h * 0.35f));
    const int sendH  = spH / 4;
    const int panH   = spH - 3 * sendH;
    const int faderY = sendY + spH;

    sendEffect.setVisible(hasSends() && channelType == Type::Rhythm);
    sendDelay .setVisible(hasSends() && channelType != Type::ReverbReturn);
    sendReverb.setVisible(hasSends());

    sendEffect.setBounds(0, sendY,             stripW, sendH);
    sendDelay .setBounds(0, sendY + sendH,     stripW, sendH);
    sendReverb.setBounds(0, sendY + sendH * 2, stripW, sendH);
    panKnob   .setBounds(0, sendY + sendH * 3, stripW, panH);

    // Sends pane: from first visible send to end of pan.
    {
        const int firstOff = (channelType == Type::Rhythm)      ? 0
                           : (channelType == Type::EffectReturn) ? sendH
                           : (channelType == Type::DelayReturn)  ? sendH * 2
                           : sendH * 3;  // ReverbReturn / Master: pan only
        const int paneH = spH - firstOff;
        sendsPaneBounds = (paneH > 0) ? juce::Rectangle<int>{ 1, sendY + firstOff + 1,
                                                               stripW - 2, paneH - 2 }
                                      : juce::Rectangle<int>{};
    }

    // ── Fader + VU + GR ───────────────────────────────────────────────────────
    const int muteY   = hasMuteSolo() ? h - kButtonH : h;
    const int dbY     = muteY - kDbH;
    // outBus sits just above the dB label on Rhythm channels.
    const int busY    = hasOutputBus() ? dbY - kOutBusH : dbY;
    const int faderEnd = hasOutputBus() ? busY - 2 : dbY - 2;
    const int faderH  = juce::jmax(40, faderEnd - faderY);

    const int grW = hasSidechainControls() ? kGRW : 0;
    fader.setBounds  (0,                        faderY, stripW - kVUW - grW, faderH);
    if (hasSidechainControls())
        grMeter.setBounds(stripW - kVUW - grW,  faderY, grW,                 faderH);
    vuMeter.setBounds(stripW - kVUW,            faderY, kVUW,                faderH);

    if (hasOutputBus())
        outBusBox.setBounds(0, busY, stripW, kOutBusH);

    dbLabel.setBounds(0, dbY, stripW, kDbH);

    if (hasMuteSolo())
    {
        const int hw = stripW / 2;
        muteBtn.setBounds(0,  muteY, hw,          kButtonH);
        soloBtn.setBounds(hw, muteY, stripW - hw, kButtonH);
    }

    faderPaneBounds = { 1, faderY - 2, stripW - 2, h - faderY + 1 };

    // ── Insert panels (Master channel, right of strip) — two slots stacked top/bottom ─
    if (hasInsert())
    {
        const int pad    = 4;
        const int ipX    = stripW + pad;
        const int ipW    = insW - 2 * pad;
        const int insTop = nameBottom;
        const int insH   = h - insTop - pad;
        const int halfH  = insH / 2;
        insertMidY = insTop + halfH;

        // Both slots reserve kNameH for their own label, giving equal knob space.
        const int slot1CharY = insTop + kNameH + pad;
        const int slot2CharY = insertMidY + kNameH + pad;

        auto layoutSlot = [&](int charBoxY, int endY,
                               juce::ComboBox& charBox,
                               KnobWithLabel& drv, KnobWithLabel& out,
                               KnobWithLabel& ton, KnobWithLabel& ext)
        {
            charBox.setBounds(ipX, charBoxY, ipW, kInsCharH);
            const int ky      = charBoxY + kInsCharH + 2;
            const int availH  = endY - ky - 2;

            if (charBox.getSelectedId() == 7) // EQ: single column High/Mid/MidHz/Low
            {
                const int rowH = juce::jmin(60, availH / 4);
                KnobWithLabel* const eqOrder[] = { &ton, &out, &ext, &drv };
                for (int i = 0; i < 4; ++i)
                    eqOrder[i]->setBounds(ipX, ky + i * rowH, ipW, rowH);
            }
            else
            {
                KnobWithLabel* vis[4];
                int nVis = 0;
                KnobWithLabel* const knobs[] = { &drv, &out, &ton, &ext };
                for (auto* k : knobs)
                    if (k->isVisible()) vis[nVis++] = k;

                if (nVis > 0)
                {
                    const int nRows = (nVis + 1) / 2;
                    const int rowH  = juce::jmin(60, availH / nRows);
                    const int halfW = ipW / 2;

                    for (int i = 0; i < nVis; ++i)
                    {
                        const bool isLastOdd = (i == nVis - 1) && (nVis % 2 == 1);
                        const int kw = isLastOdd ? ipW : halfW;
                        const int kx = ipX + (i % 2) * halfW;
                        vis[i]->setBounds(kx, ky + (i / 2) * rowH, kw, rowH);
                    }
                }
            }
        };

        layoutSlot(slot1CharY, insertMidY,  insCharBox,  insDrive,  insOutput,  insTone,  insExtra);
        layoutSlot(slot2CharY, h - pad,     insCharBox2, insDrive2, insOutput2, insTone2, insExtra2);
    }
}

void MixerChannel::configureInsertAlgorithm(int charId, int slot, PluginProcessor* proc)
{
    if (!hasInsert()) return;

    KnobWithLabel& drive  = slot == 0 ? insDrive  : insDrive2;
    KnobWithLabel& output = slot == 0 ? insOutput : insOutput2;
    KnobWithLabel& tone   = slot == 0 ? insTone   : insTone2;
    KnobWithLabel& extra  = slot == 0 ? insExtra  : insExtra2;

    // Null callbacks first — prevents spurious APVTS writes during range changes.
    drive .onValueChanged = nullptr;
    output.onValueChanged = nullptr;
    tone  .onValueChanged = nullptr;
    extra .onValueChanged = nullptr;
    output.setGRSource(nullptr);  // #246: cleared here; comp/limiter cases re-set below

    // #243: the lambda must keep working after this method is re-invoked from
    // loadFromAPVTS with proc=nullptr (the dropdown char value comes from APVTS,
    // so we don't want to write back — but the knob callbacks still need a live
    // proc handle for the user to actually drive the engine).
    PluginProcessor* const knobProc = masterInsertProc;
    const juce::String pDrv  = slot == 0 ? "mst_insDrv"  : "mst_ins2Drv";
    const juce::String pOut  = slot == 0 ? "mst_insOut"   : "mst_ins2Out";
    const juce::String pDit  = slot == 0 ? "mst_insDit"   : "mst_ins2Dit";
    const juce::String pTon  = slot == 0 ? "mst_insTon"   : "mst_ins2Ton";
    const juce::String pMid  = slot == 0 ? "mst_insMid"   : "mst_ins2Mid";
    const juce::String pBit  = slot == 0 ? "mst_insBits"  : "mst_ins2Bits";
    const juce::String pRte  = slot == 0 ? "mst_insRate"  : "mst_ins2Rate";
    const juce::String pChar = slot == 0 ? "mst_insChar"  : "mst_ins2Char";

    auto setParam = [knobProc](const juce::String& id, double v)
    {
        if (!knobProc) return;
        if (auto* p = knobProc->apvts.getParameter(id))
            p->setValueNotifyingHost(p->convertTo0to1((float)v));
    };

    const VoiceParams& ip = masterInsertProc
        ? (slot == 0 ? masterInsertProc->mixerEngine.masterInsertParams
                     : masterInsertProc->mixerEngine.masterInsertParams2)
        : VoiceParams{};

    switch (charId)
    {
        case 0: // None — hide all knobs
            drive .setVisible(false);
            output.setVisible(false);
            tone  .setVisible(false);
            extra .setVisible(false);
            if (proc) setParam(pChar, 0);
            break;

        case 1: case 2: case 3: // Soft Clip / Hard Clip / Fold
        case 5:                  // Clipper — same Drive/Output/LPF layout
            drive .setLabel(charId == 5 ? "Threshold" : "Drive");
            drive .setRange(0.0, 100.0, 0.1);
            if (charId != 5)
            {
                drive .getSlider().textFromValueFunction = [](double v) -> juce::String {
                    return juce::String(v * 0.4, 1) + " dB";
                };
                drive .getSlider().valueFromTextFunction = [](const juce::String& s) -> double {
                    return juce::jlimit(0.0, 100.0, s.retainCharacters("-0123456789.").getDoubleValue() * 2.5);
                };
            }
            else
            {
                drive .getSlider().textFromValueFunction = nullptr;
                drive .getSlider().valueFromTextFunction = nullptr;
            }
            drive .setValue(ip.driveDrive, juce::dontSendNotification);
            drive .setVisible(true);

            output.setLabel("Output");
            output.setRange(-24.0, 0.0, 0.1);
            output.getSlider().textFromValueFunction = nullptr;
            output.setValue(ip.driveOutput, juce::dontSendNotification);
            output.setVisible(true);

            tone  .setLabel("LPF");
            tone  .setRange(20.0, 20000.0, 1.0);
            tone  .getSlider().setSkewFactorFromMidPoint(640.0);   // #289
            tone  .getSlider().textFromValueFunction = [](double v) -> juce::String {
                return v >= 1000.0 ? juce::String(v / 1000.0, 2) + "kHz"
                                   : juce::String((int)v) + "Hz";
            };
            tone  .setValue(ip.driveTone, juce::dontSendNotification);
            tone  .setVisible(true);

            extra .setVisible(false);
            drive .onValueChanged = [setParam, pDrv](double v) { setParam(pDrv, v); };
            output.onValueChanged = [setParam, pOut](double v) { setParam(pOut, v); };
            tone  .onValueChanged = [setParam, pTon](double v) { setParam(pTon, v); };
            if (proc) setParam(pChar, charId);
            break;

        case 4: // Bitcrusher — Bits / Rate / Dither
            drive .setLabel("Bits");
            drive .setRange(1.0, 16.0, 1.0);
            drive .getSlider().textFromValueFunction = [](double v) -> juce::String {
                return juce::String((int)v) + " bits";
            };
            drive .setValue(ip.drvBits, juce::dontSendNotification);
            drive .setVisible(true);

            output.setLabel("Rate");
            output.setRange(100.0, 48000.0, 1.0);
            output.getSlider().setSkewFactorFromMidPoint(2190.0);
            output.getSlider().textFromValueFunction = [](double v) -> juce::String {
                return v >= 1000.0 ? juce::String(v / 1000.0, 2) + "kHz"
                                   : juce::String((int)v) + "Hz";
            };
            output.setValue(ip.driveRate, juce::dontSendNotification);
            output.setVisible(true);

            tone  .setLabel("Dither");
            tone  .setRange(0.0, 100.0, 0.1);
            tone  .getSlider().textFromValueFunction = [](double v) -> juce::String {
                return juce::String((int)std::round(v)) + "%";
            };
            tone  .setValue(ip.drvDither, juce::dontSendNotification);
            tone  .setVisible(true);

            extra .setVisible(false);
            drive .onValueChanged = [setParam, pBit](double v) { setParam(pBit, v); };
            output.onValueChanged = [setParam, pRte](double v) { setParam(pRte, v); };
            tone  .onValueChanged = [setParam, pDit](double v) { setParam(pDit, v); };
            if (proc) setParam(pChar, 4);
            break;

        case 6: // 3-Band EQ — Low / Mid gain / Mid Hz / High (#248: 4 knobs)
        {
            auto dbFmt = [](double v) -> juce::String {
                return (v >= 0.0 ? "+" : "") + juce::String(v, 1) + " dB";
            };
            auto hzFmt = [](double v) -> juce::String {
                return v >= 1000.0 ? juce::String(v / 1000.0, 2) + "kHz"
                                   : juce::String((int)v) + "Hz";
            };

            drive .getSlider().setSkewFactor(1.0);
            output.getSlider().setSkewFactor(1.0);
            tone  .getSlider().setSkewFactor(1.0);

            drive .setLabel("Low");
            drive .setRange(-18.0, 18.0, 0.1);
            drive .getSlider().textFromValueFunction = dbFmt;
            drive .setValue(ip.driveDrive / 100.0 * 36.0 - 18.0, juce::dontSendNotification);
            drive .setVisible(true);

            output.setLabel("Mid");
            output.setRange(-18.0, 18.0, 0.1);
            output.getSlider().textFromValueFunction = dbFmt;
            output.setValue(ip.eqMidGain, juce::dontSendNotification);
            output.setVisible(true);

            tone  .setLabel("High");
            tone  .setRange(-18.0, 18.0, 0.1);
            tone  .getSlider().textFromValueFunction = dbFmt;
            tone  .setValue(ip.drvDither / 100.0 * 36.0 - 18.0, juce::dontSendNotification);
            tone  .setVisible(true);

            extra .setLabel("Mid Hz");
            extra .setRange(200.0, 8000.0, 1.0);
            extra .getSlider().setSkewFactorFromMidPoint(1000.0);
            extra .getSlider().textFromValueFunction = hzFmt;
            extra .setValue(juce::jlimit(200.0, 8000.0, (double)ip.driveTone), juce::dontSendNotification);
            extra .setVisible(true);

            drive .onValueChanged = [setParam, pDrv](double v) { setParam(pDrv, (v + 18.0) / 36.0 * 100.0); };
            output.onValueChanged = [setParam, pMid](double v) { setParam(pMid, v); };
            tone  .onValueChanged = [setParam, pDit](double v) { setParam(pDit, (v + 18.0) / 36.0 * 100.0); };
            extra .onValueChanged = [setParam, pTon](double v) { setParam(pTon, v); };
            if (proc) setParam(pChar, 6);
            break;
        }

        case 7: case 8: // Compressor / Limiter — Threshold / Output / Release
            drive .setLabel(charId == 8 ? "Ceiling" : "Threshold");
            drive .setRange(0.0, 100.0, 0.1);
            drive .getSlider().textFromValueFunction = [](double v) -> juce::String {
                return "-" + juce::String((int)std::round(v * 0.4)) + " dB";
            };
            drive .setValue(ip.driveDrive, juce::dontSendNotification);
            drive .setVisible(true);

            output.setLabel("Output");
            output.setRange(-24.0, 24.0, 0.1);
            output.getSlider().textFromValueFunction = nullptr;
            output.setValue(ip.driveOutput, juce::dontSendNotification);
            output.setVisible(true);

            tone  .setLabel("Release");
            tone  .setRange(20.0, 2000.0, 1.0);
            tone  .getSlider().setSkewFactorFromMidPoint(200.0);
            tone  .getSlider().textFromValueFunction = [](double v) -> juce::String {
                return v < 1000.0 ? juce::String((int)v) + " ms"
                                  : juce::String(v / 1000.0, 2) + " s";
            };
            tone  .setValue(juce::jlimit(20.0, 2000.0, (double)ip.driveTone), juce::dontSendNotification);
            tone  .setVisible(true);

            extra .setVisible(false);
            drive .onValueChanged = [setParam, pDrv](double v) { setParam(pDrv, v); };
            output.onValueChanged = [setParam, pOut](double v) { setParam(pOut, v); };
            tone  .onValueChanged = [setParam, pTon](double v) { setParam(pTon, v); };
            // #246: GR meter on the Output knob
            output.setGRSource(masterInsertProc
                ? (slot == 0 ? &masterInsertProc->mixerEngine.masterInsert.grReduction
                             : &masterInsertProc->mixerEngine.masterInsert2.grReduction)
                : nullptr);
            if (proc) setParam(pChar, charId);
            break;

        case 9: // Ring Modulator — Mix + Freq
            drive.setLabel("Mix");
            drive.setRange(0.0, 100.0, 0.1);
            drive.getSlider().textFromValueFunction = [](double v) -> juce::String {
                return juce::String((int)std::round(v)) + "%";
            };
            drive.setValue(ip.driveDrive, juce::dontSendNotification);
            drive.setVisible(true);

            output.setVisible(false);

            tone.setLabel("Freq");
            tone.setRange(10.0, 5000.0, 1.0);
            tone.getSlider().setSkewFactorFromMidPoint(223.6);  // log feel
            tone.getSlider().textFromValueFunction = [](double v) -> juce::String {
                return v >= 1000.0 ? juce::String(v / 1000.0, 2) + "kHz"
                                   : juce::String((int)v) + "Hz";
            };
            tone.setValue(juce::jlimit(10.0, 5000.0, (double)ip.driveTone), juce::dontSendNotification);
            tone.setVisible(true);

            extra.setVisible(false);
            drive.onValueChanged = [setParam, pDrv](double v) { setParam(pDrv, v); };
            tone .onValueChanged = [setParam, pTon](double v) { setParam(pTon, v); };
            if (proc) setParam(pChar, 9);
            break;

        case 10: // Tape Saturation — Drive / Output / Tone
            drive.setLabel("Drive");
            drive.setRange(0.0, 100.0, 0.1);
            drive.getSlider().textFromValueFunction = nullptr;
            drive.setValue(ip.driveDrive, juce::dontSendNotification);
            drive.setVisible(true);

            output.setLabel("Output");
            output.setRange(-24.0, 0.0, 0.1);
            output.getSlider().textFromValueFunction = nullptr;
            output.setValue(ip.driveOutput, juce::dontSendNotification);
            output.setVisible(true);

            tone.setLabel("Tone");
            tone.setRange(200.0, 20000.0, 1.0);
            tone.getSlider().setSkewFactorFromMidPoint(2000.0);
            tone.getSlider().textFromValueFunction = [](double v) -> juce::String {
                return v >= 1000.0 ? juce::String(v / 1000.0, 2) + "kHz"
                                   : juce::String((int)v) + "Hz";
            };
            tone.setValue(juce::jlimit(200.0, 20000.0, (double)ip.driveTone), juce::dontSendNotification);
            tone.setVisible(true);

            extra.setVisible(false);
            drive .onValueChanged = [setParam, pDrv](double v) { setParam(pDrv, v); };
            output.onValueChanged = [setParam, pOut](double v) { setParam(pOut, v); };
            tone  .onValueChanged = [setParam, pTon](double v) { setParam(pTon, v); };
            if (proc) setParam(pChar, 10);
            break;

        default: break;
    }

    for (auto* k : { &drive, &output, &tone, &extra })
    { k->getSlider().updateText(); k->repaint(); }

    resized();
}

void MixerChannel::setEffectSendLabel(const juce::String& name)
{
    sendEffect.setLabel(name);
}

void MixerChannel::paint(juce::Graphics& g)
{
    const int w = getWidth();
    const int h = getHeight();

    // Colour bar and name
    g.setColour(channelColour);
    g.fillRect(0, 0, w, kColourBarH);

    const int stripW = w - (hasInsert() ? kInsertPanelW : 0);

    g.setColour(active ? MuClidLookAndFeel::colour(Id::headingText)
                       : MuClidLookAndFeel::colour(Id::mutedText));
    g.setFont(juce::Font(juce::FontOptions{}.withHeight(10.0f)));
    g.drawText(channelName, 0, kColourBarH, stripW, kNameH,
               juce::Justification::centred, true);

    // Right-edge channel divider
    g.setColour(MuClidLookAndFeel::colour(Id::segmentInactiveBorder));
    g.drawLine((float)(w - 1), 0.0f, (float)(w - 1), (float)h, 0.5f);

    // ── Section pane borders ──────────────────────────────────────────────────
    const juce::Colour borderCol = MuClidLookAndFeel::colour(Id::segmentInactiveBorder)
                                       .withAlpha(0.45f);
    g.setColour(borderCol);
    if (!sidechainPaneBounds.isEmpty())
        g.drawRoundedRectangle(sidechainPaneBounds.toFloat(), 2.0f, 1.0f);
    if (!sendsPaneBounds.isEmpty())
        g.drawRoundedRectangle(sendsPaneBounds.toFloat(), 2.0f, 1.0f);
    if (!faderPaneBounds.isEmpty())
        g.drawRoundedRectangle(faderPaneBounds.toFloat(), 2.0f, 1.0f);

    // Section labels (tiny, top-right of each pane)
    g.setFont(juce::Font(juce::FontOptions{}.withHeight(8.0f)));
    g.setColour(MuClidLookAndFeel::colour(Id::mutedText));
    if (!sidechainPaneBounds.isEmpty() && hasOutputBus())  // "SC" tag only on rhythm strips (others have no output bus label)
        g.drawText("SC", sidechainPaneBounds.getRight() - 14, sidechainPaneBounds.getY() + 1,
                   12, 10, juce::Justification::centredRight, false);

    // ── Insert panel (Master: right portion) ─────────────────────────────────
    if (hasInsert())
    {
        // Vertical separator between strip and insert panel
        g.setColour(borderCol.withAlpha(0.6f));
        g.drawLine((float)stripW, (float)kColourBarH, (float)stripW, (float)h, 1.0f);

        // Colour bar continuation
        g.setColour(channelColour);
        g.fillRect(stripW, 0, kInsertPanelW, kColourBarH);

        // "Main Insert 1/2" labels sit inside each half; shared name row is clear.
        const juce::Colour labelCol = active ? MuClidLookAndFeel::colour(Id::headingText)
                                             : MuClidLookAndFeel::colour(Id::mutedText);
        const int insTop = kColourBarH + kNameH;  // start of insert content area
        g.setColour(labelCol);
        g.setFont(juce::Font(juce::FontOptions{}.withHeight(11.0f)));
        g.drawText("Main Insert 1", stripW, insTop, kInsertPanelW, kNameH,
                   juce::Justification::centred, false);
        if (insertMidY > 0)
        {
            // Horizontal divider between the two insert slots
            g.setColour(borderCol.withAlpha(0.5f));
            g.drawLine((float)stripW, (float)insertMidY,
                       (float)(stripW + kInsertPanelW), (float)insertMidY, 1.0f);
            // "Main Insert 2" label at the top of slot 2
            g.setColour(labelCol);
            g.drawText("Main Insert 2", stripW, insertMidY, kInsertPanelW, kNameH,
                       juce::Justification::centred, false);
        }
    }

    // Inactive overlay
    if (!active)
    {
        g.setColour(MuClidLookAndFeel::colour(MuClidLookAndFeel::backgroundMixerStripDim));
        g.fillRect(0, kColourBarH + kNameH, w, h - kColourBarH - kNameH);
    }
}
