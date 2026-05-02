#include "MixerChannel.h"
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
    dbLabel.setFont(juce::Font(9.0f));
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
void MixerChannel::bindRhythm(MixerEngine::ChannelState& state, juce::Atomic<float>& peak)
{
    fader.setValue(state.level, juce::dontSendNotification);
    panKnob.setValue(state.pan);
    sendEffect.setValue(state.sendEffect);
    sendDelay.setValue (state.sendDelay);
    sendReverb.setValue(state.sendReverb);
    muteBtn.setToggleState(state.mute, juce::dontSendNotification);
    soloBtn.setToggleState(state.solo, juce::dontSendNotification);

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

    vuMeter.getLevel = [&peak] { return peak.get(); };
    updateDbLabel(state.level);
}

void MixerChannel::bindReturn(MixerEngine::ReturnState& state, juce::Atomic<float>& peak)
{
    fader.setValue(state.level, juce::dontSendNotification);
    panKnob.setValue(state.pan);
    muteBtn.setToggleState(state.mute, juce::dontSendNotification);
    soloBtn.setToggleState(state.solo, juce::dontSendNotification);

    fader.onValueChange = [&state, this] {
        state.level = (float)fader.getValue();
        updateDbLabel(state.level);
    };
    panKnob.onValueChanged = [&state](double v) { state.pan  = (float)v; };
    muteBtn.onClick = [&state, this] { state.mute = muteBtn.getToggleState(); };
    soloBtn.onClick = [&state, this] { state.solo = soloBtn.getToggleState(); };

    vuMeter.getLevel = [&peak] { return peak.get(); };
    updateDbLabel(state.level);
}

void MixerChannel::bindMaster(MixerEngine& engine)
{
    fader.setValue(engine.masterLevel, juce::dontSendNotification);
    panKnob.setValue(engine.masterPan);

    fader.onValueChange = [&engine, this] {
        engine.masterLevel = (float)fader.getValue();
        updateDbLabel(engine.masterLevel);
    };
    panKnob.onValueChanged = [&engine](double v) { engine.masterPan = (float)v; };

    vuMeter.getLevel = [&engine] { return engine.masterPeak.get(); };
    updateDbLabel(engine.masterLevel);
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
    const int w = getWidth();
    const int h = getHeight();
    int y = kColourBarH + kNameH;

    if (hasSends())
    {
        sendEffect.setBounds(0, y,             w, kSendH);
        sendDelay.setBounds (0, y + kSendH,    w, kSendH);
        sendReverb.setBounds(0, y + kSendH * 2, w, kSendH);
        y += kSendH * 3;
    }

    panKnob.setBounds(0, y, w, kPanH);
    y += kPanH;

    const int bottomH = kDbH + (hasMuteSolo() ? kButtonH + 2 : 0);
    const int faderH  = juce::jmax(40, juce::jmin(kFaderMaxH, h - y - bottomH - 2));

    fader.setBounds  (0,        y, w - kVUW, faderH);
    vuMeter.setBounds(w - kVUW, y, kVUW,     faderH);
    y += faderH + 2;

    dbLabel.setBounds(0, y, w, kDbH);
    y += kDbH;

    if (hasMuteSolo())
    {
        const int hw = w / 2;
        muteBtn.setBounds(0,  y, hw,     kButtonH);
        soloBtn.setBounds(hw, y, w - hw, kButtonH);
    }
}

void MixerChannel::paint(juce::Graphics& g)
{
    const int w = getWidth();
    const int h = getHeight();

    g.setColour(channelColour);
    g.fillRect(0, 0, w, kColourBarH);

    g.setColour(MuClidLookAndFeel::colour(MuClidLookAndFeel::headingText));
    g.setFont(juce::Font(10.0f));
    g.drawText(channelName, 0, kColourBarH, w, kNameH,
               juce::Justification::centred, true);

    g.setColour(MuClidLookAndFeel::colour(MuClidLookAndFeel::segmentInactiveBorder));
    g.drawLine((float)(w - 1), 0.0f, (float)(w - 1), (float)h, 0.5f);
}
