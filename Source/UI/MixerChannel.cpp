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
        outBusBox.addItem("M", 1);                 // 0 -> Master
        for (int i = 1; i <= 8; ++i)
            outBusBox.addItem(juce::String(i), i + 1);  // i -> Out i
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
    const int w        = getWidth();
    const int h        = getHeight();
    const int outBusY  = kColourBarH + kNameH;
    const int outBusH  = hasSidechain() ? kOutBusH : 0;
    const int topY     = outBusY + outBusH;
    const int scH      = hasSidechain() ? kSidechainH : 0;
    const int sendY    = topY + scH;
    const int faderY   = sendY + kTopAreaH;

    // Out dropdown — Rhythm channels only.
    if (hasSidechain())
        outBusBox.setBounds(0, outBusY, w, kOutBusH);

    // Sidechain section — Rhythm channels only
    if (hasSidechain())
    {
        scSourceBox.setBounds(0, topY, w, kScSrcH);
        scAmount   .setBounds(0, topY + kScSrcH, w, kScAmtH);
        const int hw = w / 2;
        scAttack .setBounds(0,  topY + kScSrcH + kScAmtH, hw,     kScEnvH);
        scRelease.setBounds(hw, topY + kScSrcH + kScAmtH, w - hw, kScEnvH);
    }

    // Send slots; hide those that don't apply to this channel type.
    sendEffect.setVisible(channelType == Type::Rhythm);
    sendDelay .setVisible(channelType == Type::Rhythm || channelType == Type::EffectReturn);
    sendReverb.setVisible(channelType == Type::Rhythm || channelType == Type::EffectReturn
                          || channelType == Type::DelayReturn);

    sendEffect.setBounds(0, sendY,            w, kSendH);
    sendDelay .setBounds(0, sendY + kSendH,   w, kSendH);
    sendReverb.setBounds(0, sendY + kSendH*2, w, kSendH);

    panKnob.setBounds(0, sendY + kSendH * 3, w, kPanH);

    // Fader fills from below top area to just above mute/solo.
    const int muteY  = hasMuteSolo() ? h - kButtonH : h;
    const int dbY    = muteY - kDbH;
    const int faderH = juce::jmax(40, dbY - faderY - 2);

    const int grW = hasSidechain() ? kGRW : 0;
    fader.setBounds  (0,                faderY, w - kVUW - grW, faderH);
    if (hasSidechain())
        grMeter.setBounds(w - kVUW - grW, faderY, grW,          faderH);
    vuMeter.setBounds(w - kVUW,         faderY, kVUW,           faderH);
    dbLabel.setBounds(0, dbY, w, kDbH);

    if (hasMuteSolo())
    {
        const int hw = w / 2;
        muteBtn.setBounds(0,  muteY, hw,     kButtonH);
        soloBtn.setBounds(hw, muteY, w - hw, kButtonH);
    }
}

void MixerChannel::setEffectSendLabel(const juce::String& name)
{
    sendEffect.setLabel(name);
}

void MixerChannel::paint(juce::Graphics& g)
{
    const int w = getWidth();
    const int h = getHeight();

    g.setColour(channelColour);
    g.fillRect(0, 0, w, kColourBarH);

    g.setColour(active ? MuClidLookAndFeel::colour(MuClidLookAndFeel::headingText)
                       : MuClidLookAndFeel::colour(MuClidLookAndFeel::mutedText));
    g.setFont(juce::Font(juce::FontOptions{}.withHeight(10.0f)));
    g.drawText(channelName, 0, kColourBarH, w, kNameH,
               juce::Justification::centred, true);

    g.setColour(MuClidLookAndFeel::colour(MuClidLookAndFeel::segmentInactiveBorder));
    g.drawLine((float)(w - 1), 0.0f, (float)(w - 1), (float)h, 0.5f);

    // Sidechain section header — thin separator + "SC" label
    if (hasSidechain())
    {
        const int scTop = kColourBarH + kNameH;
        g.setColour(MuClidLookAndFeel::colour(MuClidLookAndFeel::segmentInactiveBorder)
                        .withAlpha(0.6f));
        g.fillRect(0, scTop, w, 1);
        g.fillRect(0, scTop + kSidechainH, w, 1);
        g.setColour(MuClidLookAndFeel::colour(MuClidLookAndFeel::mutedText));
        g.setFont(juce::Font(juce::FontOptions{}.withHeight(8.0f)));
        g.drawText("SC", w - 14, scTop + 1, 12, 10, juce::Justification::centredRight, false);
    }

    // Inactive channels get a translucent overlay to indicate no rhythm is assigned.
    if (!active)
    {
        g.setColour(juce::Colour(0x88000000));
        g.fillRect(0, kColourBarH + kNameH, w, h - kColourBarH - kNameH);
    }
}
