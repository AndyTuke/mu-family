#include "MixerChannel.h"
#include "../PluginProcessor.h"
#include <cmath>

MixerChannel::MixerChannel(Type t, const juce::String& name, juce::Colour col)
    : channelType(t), channelName(name), channelColour(col)
{
    fader.setSliderStyle(juce::Slider::LinearVertical);
    fader.setRange(0.0, 1.0, 0.001);
    fader.setValue(0.75, juce::dontSendNotification);
    fader.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    addAndMakeVisible(fader);
    addAndMakeVisible(vuMeter);
    if (hasSidechain())
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

    if (hasSidechain())
    {
        // #238: descriptive labels — "Main" + "Out 1" … "Out 8".
        outBusBox.addItem("Main", 1);              // 0 -> Master
        for (int i = 1; i <= 8; ++i)
            outBusBox.addItem("Out " + juce::String(i), i + 1);
        outBusBox.setSelectedId(1, juce::dontSendNotification);
        addAndMakeVisible(outBusBox);

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
        insCharBox.addItem("None",       1);
        insCharBox.addItem("3-Band EQ",  7);
        insCharBox.addItem("Bitcrusher", 5);
        insCharBox.addItem("Clipper",    6);
        insCharBox.addItem("Compressor", 8);
        insCharBox.addItem("Fold",       4);
        insCharBox.addItem("Hard Clip",  3);
        insCharBox.addItem("Limiter",    9);
        insCharBox.addItem("Ring Mod",  10);
        insCharBox.addItem("Soft Clip",  2);
        insCharBox.addItem("Tape Sat",  11);
        insCharBox.setSelectedId(1, juce::dontSendNotification);
        addAndMakeVisible(insCharBox);

        auto noVal = [](double) { return juce::String(); };
        insDrive .setRange(0.0, 100.0, 0.1);   insDrive .setValue(0.0);
        insOutput.setRange(-24.0, 0.0, 0.1);   insOutput.setValue(0.0);
        insTone  .setRange(20.0, 20000.0, 1.0); insTone  .setValue(20000.0);
        insTone  .getSlider().setSkewFactorFromMidPoint(640.0);   // #216b: log feel default for full-range modes
        insDrive .getSlider().textFromValueFunction = noVal;
        insOutput.getSlider().textFromValueFunction = noVal;
        insTone  .getSlider().textFromValueFunction = noVal;
        addAndMakeVisible(insDrive);
        addAndMakeVisible(insOutput);
        addAndMakeVisible(insTone);
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
        // Sidechain controls write through APVTS
        if (hasSidechain())
        {
            outBusBox.setSelectedId(state.outputBus + 1, juce::dontSendNotification);
            outBusBox.onChange = [proc, prefix, this] {
                const int id = outBusBox.getSelectedId();   // 1 = Master, 2..9 = Out 1..8
                if (auto* p = proc->apvts.getParameter(prefix + "outBus"))
                    p->setValueNotifyingHost(p->convertTo0to1((float)(id - 1)));
            };

            scSourceBox.onChange = [proc, prefix, this] {
                int id = scSourceBox.getSelectedId();   // 1=none, 2-9=ch0-ch7
                int apvtsVal = (id <= 1) ? 0 : (id - 1);
                if (auto* p = proc->apvts.getParameter(prefix + "scSrc"))
                    p->setValueNotifyingHost(p->convertTo0to1((float)apvtsVal));
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
        if (hasSidechain())
        {
            outBusBox.setSelectedId(state.outputBus + 1, juce::dontSendNotification);
            outBusBox.onChange    = [&state, this] {
                state.outputBus = juce::jlimit(0, 8, outBusBox.getSelectedId() - 1);
            };

            scSourceBox.onChange    = [&state, this] {
                int id = scSourceBox.getSelectedId();
                state.sidechainSource = (id <= 1) ? -1 : (id - 2);
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
                               PluginProcessor* proc, const juce::String& prefix)
{
    fader.setValue(state.level, juce::dontSendNotification);
    panKnob.setValue(state.pan, juce::dontSendNotification);
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
    }

    vuMeter.getLevel = [&peak] { return peak.get(); };
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
        // Restore UI from engine state
        const auto& ip = engine.masterInsertParams;
        insCharBox.setSelectedId(ip.driveChar + 1, juce::dontSendNotification);
        configureInsertAlgorithm(ip.driveChar, proc);

        insCharBox.onChange = [this, proc]()
        {
            const int newChar = insCharBox.getSelectedId() - 1;
            // #244b: align knob rest positions when crossing into/out of EQ
            // mode — mirrors the per-rhythm fix from #147. EQ stores Low/High
            // as 0..100 (50=0dB) so 50 = neutral; distortion algos use 0..100
            // as drive amount where 0 = no drive. Without this, switching
            // from Soft Clip (drvDrv=0) to EQ would show Low at -18 dB.
            if (proc)
            {
                const int oldChar = proc->mixerEngine.masterInsertParams.driveChar;
                auto set = [proc](const juce::String& id, float v)
                {
                    if (auto* p = proc->apvts.getParameter(id))
                        p->setValueNotifyingHost(p->convertTo0to1(v));
                };
                if (newChar == 6 && oldChar != 6)
                {
                    set("mst_insDrv", 50.0f);   // Low → 0 dB
                    set("mst_insDit", 50.0f);   // High → 0 dB
                    set("mst_insMid",  0.0f);   // Mid → 0 dB (already direct)
                }
                else if (oldChar == 6 && newChar != 6)
                {
                    set("mst_insDrv", 0.0f);    // back to "no drive" for distortion modes
                    set("mst_insDit", 0.0f);
                }
            }
            configureInsertAlgorithm(newChar, proc);
        };
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
    if (!hasSidechain()) return;
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
            configureInsertAlgorithm(charId, nullptr);
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
    if (hasSidechain())
    {
        if (auto* p = apvts.getRawParameterValue(prefix + "outBus"))
            outBusBox.setSelectedId(juce::jlimit(0, 8, juce::roundToInt((float) *p)) + 1,
                                    juce::dontSendNotification);
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

    // ── Sidechain section (Rhythm channels, directly below name) ─────────────
    const int scH  = hasSidechain() ? kSidechainH : 0;
    if (hasSidechain())
    {
        scSourceBox.setBounds(0, nameBottom,                           stripW, kScSrcH);
        scAmount   .setBounds(0, nameBottom + kScSrcH,                 stripW, kScAmtH);
        const int hw = stripW / 2;
        scAttack .setBounds(0,  nameBottom + kScSrcH + kScAmtH, hw,          kScEnvH);
        scRelease.setBounds(hw, nameBottom + kScSrcH + kScAmtH, stripW - hw, kScEnvH);

        sidechainPaneBounds = { 1, nameBottom + 1, stripW - 2, kSidechainH - 2 };
    }
    else
    {
        sidechainPaneBounds = {};
    }

    // ── Sends + pan ───────────────────────────────────────────────────────────
    const int sendY  = nameBottom + scH;
    const int faderY = sendY + kTopAreaH;

    sendEffect.setVisible(channelType == Type::Rhythm);
    sendDelay .setVisible(channelType == Type::Rhythm || channelType == Type::EffectReturn);
    sendReverb.setVisible(channelType == Type::Rhythm || channelType == Type::EffectReturn
                          || channelType == Type::DelayReturn);

    sendEffect.setBounds(0, sendY,              stripW, kSendH);
    sendDelay .setBounds(0, sendY + kSendH,     stripW, kSendH);
    sendReverb.setBounds(0, sendY + kSendH * 2, stripW, kSendH);
    panKnob   .setBounds(0, sendY + kSendH * 3, stripW, kPanH);

    // Sends pane: from first visible send to end of pan.
    {
        const int firstOff = (channelType == Type::Rhythm)      ? 0
                           : (channelType == Type::EffectReturn) ? kSendH
                           : (channelType == Type::DelayReturn)  ? kSendH * 2
                           : kSendH * 3;  // ReverbReturn / Master: pan only
        const int paneH = kTopAreaH - firstOff;
        sendsPaneBounds = (paneH > 0) ? juce::Rectangle<int>{ 1, sendY + firstOff + 1,
                                                               stripW - 2, paneH - 2 }
                                      : juce::Rectangle<int>{};
    }

    // ── Fader + VU + GR ───────────────────────────────────────────────────────
    const int muteY   = hasMuteSolo() ? h - kButtonH : h;
    const int dbY     = muteY - kDbH;
    // outBus sits just above the dB label on Rhythm channels.
    const int busY    = hasSidechain() ? dbY - kOutBusH : dbY;
    const int faderEnd = hasSidechain() ? busY - 2 : dbY - 2;
    const int faderH  = juce::jmax(40, faderEnd - faderY);

    const int grW = hasSidechain() ? kGRW : 0;
    fader.setBounds  (0,                        faderY, stripW - kVUW - grW, faderH);
    if (hasSidechain())
        grMeter.setBounds(stripW - kVUW - grW,  faderY, grW,                 faderH);
    vuMeter.setBounds(stripW - kVUW,            faderY, kVUW,                faderH);

    if (hasSidechain())
        outBusBox.setBounds(0, busY, stripW, kOutBusH);

    dbLabel.setBounds(0, dbY, stripW, kDbH);

    if (hasMuteSolo())
    {
        const int hw = stripW / 2;
        muteBtn.setBounds(0,  muteY, hw,          kButtonH);
        soloBtn.setBounds(hw, muteY, stripW - hw, kButtonH);
    }

    faderPaneBounds = { 1, faderY - 2, stripW - 2, h - faderY + 1 };

    // ── Insert panel (Master channel, right of strip) ─────────────────────────
    // Knobs stacked vertically — only visible knobs are placed; height scales up to 80px.
    if (hasInsert())
    {
        const int pad    = 4;
        const int ipX    = stripW + pad;
        const int ipW    = insW - 2 * pad;
        const int topY   = nameBottom + pad;
        const int availH = h - topY - kInsCharH - pad;

        const int numVis = (insDrive.isVisible()  ? 1 : 0)
                         + (insOutput.isVisible() ? 1 : 0)
                         + (insTone.isVisible()   ? 1 : 0);
        const int knobH  = (numVis > 0) ? juce::jmin(80, availH / numVis) : 0;

        insCharBox.setBounds(ipX, topY, ipW, kInsCharH);

        int ky = topY + kInsCharH;
        if (insDrive.isVisible())  { insDrive .setBounds(ipX, ky, ipW, knobH); ky += knobH; }
        if (insOutput.isVisible()) { insOutput.setBounds(ipX, ky, ipW, knobH); ky += knobH; }
        if (insTone.isVisible())   { insTone  .setBounds(ipX, ky, ipW, knobH); }
    }
}

void MixerChannel::configureInsertAlgorithm(int charId, PluginProcessor* proc)
{
    if (!hasInsert()) return;

    // Null callbacks first — prevents spurious APVTS writes during range changes.
    insDrive .onValueChanged = nullptr;
    insOutput.onValueChanged = nullptr;
    insTone  .onValueChanged = nullptr;

    // #243: the lambda must keep working after this method is re-invoked from
    // loadFromAPVTS with proc=nullptr (the dropdown char value comes from APVTS,
    // so we don't want to write back — but the knob callbacks still need a live
    // proc handle for the user to actually drive the engine).
    PluginProcessor* const knobProc = masterInsertProc;
    auto setParam = [knobProc](const juce::String& id, double v)
    {
        if (!knobProc) return;
        if (auto* p = knobProc->apvts.getParameter(id))
            p->setValueNotifyingHost(p->convertTo0to1((float)v));
    };

    const VoiceParams& ip = masterInsertProc ? masterInsertProc->mixerEngine.masterInsertParams : VoiceParams{};

    switch (charId)
    {
        case 0: // None — hide all knobs
            insDrive .setVisible(false);
            insOutput.setVisible(false);
            insTone  .setVisible(false);
            if (proc) setParam("mst_insChar", 0);
            break;

        case 1: case 2: case 3: // Soft Clip / Hard Clip / Fold
        case 5:                  // Clipper — same Drive/Output/LPF layout
            insDrive .setLabel(charId == 5 ? "Threshold" : "Drive");
            insDrive .setRange(0.0, 100.0, 0.1);
            // #245: cases 1/2/3 display 0..100 as 0..40 dB input gain (matches
            // InsertProcessor's `preGain = pow(10, drvDrv/100 * 2)`). Clipper's
            // Threshold (case 5) uses a different formula and keeps raw display
            // for now (covered by a follow-up).
            if (charId != 5)
            {
                insDrive .getSlider().textFromValueFunction = [](double v) -> juce::String {
                    return juce::String(v * 0.4, 1) + " dB";
                };
                insDrive .getSlider().valueFromTextFunction = [](const juce::String& s) -> double {
                    return juce::jlimit(0.0, 100.0, s.retainCharacters("-0123456789.").getDoubleValue() * 2.5);
                };
            }
            else
            {
                insDrive .getSlider().textFromValueFunction = nullptr;
                insDrive .getSlider().valueFromTextFunction = nullptr;
            }
            insDrive .setValue(ip.driveDrive, juce::dontSendNotification);
            insDrive .setVisible(true);

            insOutput.setLabel("Output");
            insOutput.setRange(-24.0, 0.0, 0.1);
            insOutput.getSlider().textFromValueFunction = nullptr;
            insOutput.setValue(ip.driveOutput, juce::dontSendNotification);
            insOutput.setVisible(true);

            insTone  .setLabel("LPF");
            insTone  .setRange(20.0, 20000.0, 1.0);
            insTone  .getSlider().setSkewFactorFromMidPoint(640.0);   // #216b
            insTone  .getSlider().textFromValueFunction = [](double v) -> juce::String {
                return v >= 1000.0 ? juce::String(v / 1000.0, 2) + "kHz"
                                   : juce::String((int)v) + "Hz";
            };
            insTone  .setValue(ip.driveTone, juce::dontSendNotification);
            insTone  .setVisible(true);

            insDrive .onValueChanged = [setParam](double v) { setParam("mst_insDrv", v); };
            insOutput.onValueChanged = [setParam](double v) { setParam("mst_insOut", v); };
            insTone  .onValueChanged = [setParam](double v) { setParam("mst_insTon", v); };
            if (proc) setParam("mst_insChar", charId);
            break;

        case 4: // Bitcrusher — Bits / Rate / Dither
            insDrive .setLabel("Bits");
            insDrive .setRange(1.0, 16.0, 1.0);
            insDrive .getSlider().textFromValueFunction = [](double v) -> juce::String {
                return juce::String((int)v) + " bits";
            };
            insDrive .setValue(ip.drvBits, juce::dontSendNotification);
            insDrive .setVisible(true);

            insOutput.setLabel("Rate");
            insOutput.setRange(100.0, 48000.0, 1.0);
            insOutput.getSlider().setSkewFactorFromMidPoint(2190.0);
            insOutput.getSlider().textFromValueFunction = [](double v) -> juce::String {
                return v >= 1000.0 ? juce::String(v / 1000.0, 2) + "kHz"
                                   : juce::String((int)v) + "Hz";
            };
            insOutput.setValue(ip.driveRate, juce::dontSendNotification);
            insOutput.setVisible(true);

            insTone  .setLabel("Dither");
            insTone  .setRange(0.0, 100.0, 0.1);
            insTone  .getSlider().textFromValueFunction = [](double v) -> juce::String {
                return juce::String((int)std::round(v)) + "%";
            };
            insTone  .setValue(ip.drvDither, juce::dontSendNotification);
            insTone  .setVisible(true);

            insDrive .onValueChanged = [setParam](double v) { setParam("mst_insBits", v); };
            insOutput.onValueChanged = [setParam](double v) { setParam("mst_insRate", v); };
            insTone  .onValueChanged = [setParam](double v) { setParam("mst_insDit",  v); };
            if (proc) setParam("mst_insChar", 4);
            break;

        case 6: // 3-Band EQ — Low / Mid / High (#244a — Mid in middle slot)
        {
            auto dbFmt = [](double v) -> juce::String {
                return (v >= 0.0 ? "+" : "") + juce::String(v, 1) + " dB";
            };

            // Reset any log skew left over from prior algorithms (Compressor's
            // Release on insTone, Bitcrusher's Rate on insOutput, etc.) so the
            // ±18 dB EQ knobs centre visually at 0 dB.
            insDrive .getSlider().setSkewFactor(1.0);
            insOutput.getSlider().setSkewFactor(1.0);
            insTone  .getSlider().setSkewFactor(1.0);

            insDrive .setLabel("Low");
            insDrive .setRange(-18.0, 18.0, 0.1);
            insDrive .getSlider().textFromValueFunction = dbFmt;
            insDrive .setValue(ip.driveDrive / 100.0 * 36.0 - 18.0, juce::dontSendNotification);
            insDrive .setVisible(true);

            insOutput.setLabel("Mid");
            insOutput.setRange(-18.0, 18.0, 0.1);
            insOutput.getSlider().textFromValueFunction = dbFmt;
            insOutput.setValue(ip.eqMidGain, juce::dontSendNotification);
            insOutput.setVisible(true);

            insTone  .setLabel("High");
            insTone  .setRange(-18.0, 18.0, 0.1);
            insTone  .getSlider().textFromValueFunction = dbFmt;
            insTone  .setValue(ip.drvDither / 100.0 * 36.0 - 18.0, juce::dontSendNotification);
            insTone  .setVisible(true);

            // Low/high gains stored as 0-100 (50=0 dB), mid direct
            insDrive .onValueChanged = [setParam](double v) { setParam("mst_insDrv", (v + 18.0) / 36.0 * 100.0); };
            insOutput.onValueChanged = [setParam](double v) { setParam("mst_insMid", v); };
            insTone  .onValueChanged = [setParam](double v) { setParam("mst_insDit", (v + 18.0) / 36.0 * 100.0); };
            if (proc) setParam("mst_insChar", 6);
            break;
        }

        case 7: case 8: // Compressor / Limiter — Threshold / Output / Release
            insDrive .setLabel(charId == 8 ? "Ceiling" : "Threshold");
            insDrive .setRange(0.0, 100.0, 0.1);
            insDrive .getSlider().textFromValueFunction = [](double v) -> juce::String {
                return "-" + juce::String((int)std::round(v * 0.4)) + " dB";
            };
            insDrive .setValue(ip.driveDrive, juce::dontSendNotification);
            insDrive .setVisible(true);

            insOutput.setLabel("Output");
            insOutput.setRange(-24.0, 0.0, 0.1);
            insOutput.getSlider().textFromValueFunction = nullptr;
            insOutput.setValue(ip.driveOutput, juce::dontSendNotification);
            insOutput.setVisible(true);

            insTone  .setLabel("Release");
            insTone  .setRange(20.0, 2000.0, 1.0);
            insTone  .getSlider().setSkewFactorFromMidPoint(200.0);
            insTone  .getSlider().textFromValueFunction = [](double v) -> juce::String {
                return v < 1000.0 ? juce::String((int)v) + " ms"
                                  : juce::String(v / 1000.0, 2) + " s";
            };
            insTone  .setValue(juce::jlimit(20.0, 2000.0, (double)ip.driveTone), juce::dontSendNotification);
            insTone  .setVisible(true);

            insDrive .onValueChanged = [setParam](double v) { setParam("mst_insDrv", v); };
            insOutput.onValueChanged = [setParam](double v) { setParam("mst_insOut", v); };
            insTone  .onValueChanged = [setParam](double v) { setParam("mst_insTon", v); };
            if (proc) setParam("mst_insChar", charId);
            break;

        case 9: // Ring Modulator — Mix + Freq
            insDrive.setLabel("Mix");
            insDrive.setRange(0.0, 100.0, 0.1);
            insDrive.getSlider().textFromValueFunction = [](double v) -> juce::String {
                return juce::String((int)std::round(v)) + "%";
            };
            insDrive.setValue(ip.driveDrive, juce::dontSendNotification);
            insDrive.setVisible(true);

            insOutput.setVisible(false);

            insTone.setLabel("Freq");
            insTone.setRange(10.0, 5000.0, 1.0);
            insTone.getSlider().setSkewFactorFromMidPoint(223.6);  // log feel
            insTone.getSlider().textFromValueFunction = [](double v) -> juce::String {
                return v >= 1000.0 ? juce::String(v / 1000.0, 2) + "kHz"
                                   : juce::String((int)v) + "Hz";
            };
            insTone.setValue(juce::jlimit(10.0, 5000.0, (double)ip.driveTone), juce::dontSendNotification);
            insTone.setVisible(true);

            insDrive .onValueChanged = [setParam](double v) { setParam("mst_insDrv", v); };
            insTone  .onValueChanged = [setParam](double v) { setParam("mst_insTon", v); };
            if (proc) setParam("mst_insChar", 9);
            break;

        case 10: // Tape Saturation — Drive / Output / Tone
            insDrive.setLabel("Drive");
            insDrive.setRange(0.0, 100.0, 0.1);
            insDrive.getSlider().textFromValueFunction = nullptr;
            insDrive.setValue(ip.driveDrive, juce::dontSendNotification);
            insDrive.setVisible(true);

            insOutput.setLabel("Output");
            insOutput.setRange(-24.0, 0.0, 0.1);
            insOutput.getSlider().textFromValueFunction = nullptr;
            insOutput.setValue(ip.driveOutput, juce::dontSendNotification);
            insOutput.setVisible(true);

            insTone.setLabel("Tone");
            insTone.setRange(200.0, 20000.0, 1.0);
            insTone.getSlider().setSkewFactorFromMidPoint(2000.0);
            insTone.getSlider().textFromValueFunction = [](double v) -> juce::String {
                return v >= 1000.0 ? juce::String(v / 1000.0, 2) + "kHz"
                                   : juce::String((int)v) + "Hz";
            };
            insTone.setValue(juce::jlimit(200.0, 20000.0, (double)ip.driveTone), juce::dontSendNotification);
            insTone.setVisible(true);

            insDrive .onValueChanged = [setParam](double v) { setParam("mst_insDrv", v); };
            insOutput.onValueChanged = [setParam](double v) { setParam("mst_insOut", v); };
            insTone  .onValueChanged = [setParam](double v) { setParam("mst_insTon", v); };
            if (proc) setParam("mst_insChar", 10);
            break;

        default: break;
    }

    for (auto* k : { &insDrive, &insOutput, &insTone })
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
    if (!sidechainPaneBounds.isEmpty())
        g.drawText("SC", sidechainPaneBounds.getRight() - 14, sidechainPaneBounds.getY() + 1,
                   12, 10, juce::Justification::centredRight, false);
    if (!sendsPaneBounds.isEmpty())
        g.drawText("SND", sendsPaneBounds.getRight() - 18, sendsPaneBounds.getY() + 1,
                   16, 10, juce::Justification::centredRight, false);

    // ── Insert panel (Master: right portion) ─────────────────────────────────
    if (hasInsert())
    {
        // Vertical separator between strip and insert panel
        g.setColour(borderCol.withAlpha(0.6f));
        g.drawLine((float)stripW, (float)kColourBarH, (float)stripW, (float)h, 1.0f);

        // Colour bar continuation
        g.setColour(channelColour);
        g.fillRect(stripW, 0, kInsertPanelW, kColourBarH);

        // "INS" label in insert panel header
        g.setColour(active ? MuClidLookAndFeel::colour(Id::headingText)
                           : MuClidLookAndFeel::colour(Id::mutedText));
        g.setFont(juce::Font(juce::FontOptions{}.withHeight(10.0f)));
        g.drawText("Insert", stripW, kColourBarH, kInsertPanelW, kNameH,
                   juce::Justification::centred, false);
    }

    // Inactive overlay
    if (!active)
    {
        g.setColour(juce::Colour(0x88000000));
        g.fillRect(0, kColourBarH + kNameH, w, h - kColourBarH - kNameH);
    }
}
