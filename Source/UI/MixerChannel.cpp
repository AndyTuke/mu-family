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
}

//==============================================================================
void MixerChannel::bindRhythm(MixerEngine::ChannelState& state, juce::Atomic<float>& peak,
                               PluginProcessor* proc, const juce::String& prefix)
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
    }

    vuMeter.getLevel = [&peak] { return peak.get(); };
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
    const int topY   = kColourBarH + kNameH;
    const int faderY = topY + kTopAreaH;

    // Send slots at fixed positions; hide slots that don't apply to this channel type.
    // Pan is always at topY + kSendH*3 so all faders start at the same Y.
    sendEffect.setVisible(channelType == Type::Rhythm);
    sendDelay .setVisible(channelType == Type::Rhythm || channelType == Type::EffectReturn);
    sendReverb.setVisible(channelType == Type::Rhythm || channelType == Type::EffectReturn
                          || channelType == Type::DelayReturn);

    sendEffect.setBounds(0, topY,            w, kSendH);
    sendDelay .setBounds(0, topY + kSendH,   w, kSendH);
    sendReverb.setBounds(0, topY + kSendH*2, w, kSendH);

    panKnob.setBounds(0, topY + kSendH * 3, w, kPanH);

    // Fader fills from below top area to just above mute/solo.
    const int muteY  = hasMuteSolo() ? h - kButtonH : h;
    const int dbY    = muteY - kDbH;
    const int faderH = juce::jmax(40, dbY - faderY - 2);

    fader.setBounds  (0,        faderY, w - kVUW, faderH);
    vuMeter.setBounds(w - kVUW, faderY, kVUW,     faderH);
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

    // Inactive channels get a translucent overlay to indicate no rhythm is assigned.
    if (!active)
    {
        g.setColour(juce::Colour(0x88000000));
        g.fillRect(0, kColourBarH + kNameH, w, h - kColourBarH - kNameH);
    }
}
