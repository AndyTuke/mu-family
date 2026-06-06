#include "MuLinkComponent.h"

namespace
{
    // Style a small section/label in the family palette.
    void styleLabel(juce::Label& l, const juce::String& text, juce::Justification j,
                    int colourId, float fontHeight, bool bold = false)
    {
        l.setText(text, juce::dontSendNotification);
        l.setJustificationType(j);
        l.setColour(juce::Label::textColourId, MuLookAndFeel{}.colour((MuLookAndFeel::ColourIds) colourId));
        l.setFont(juce::Font(juce::FontOptions((float) fontHeight, bold ? juce::Font::bold : juce::Font::plain)));
    }
}

MuLinkComponent::MuLinkComponent(mu_link::AudioServer& serverToShow)
    : server(serverToShow),
      deviceSelector(serverToShow.audioDeviceManager(),
                     0, 0,        // no audio inputs
                     2, 2,        // stereo output
                     true,        // MIDI input options (pick the external clock source)
                     true,        // MIDI output selector (drives MIDI-clock-out)
                     true,        // channels as stereo pairs
                     false)       // show advanced options
{
    setLookAndFeel(&lnf);
    addAndMakeVisible(deviceSelector);

    styleLabel(titleLabel,    "mu-link",                 juce::Justification::centredLeft, MuLookAndFeel::headingText, 26.0f, true);
    styleLabel(subtitleLabel, "master clock  \xc2\xb7  audio bus", juce::Justification::centredLeft, MuLookAndFeel::labelText, 13.0f);
    styleLabel(clientsHeading,"CONNECTED CLIENTS",       juce::Justification::centredLeft, MuLookAndFeel::labelText, 12.0f, true);
    addAndMakeVisible(titleLabel);
    addAndMakeVisible(subtitleLabel);
    addAndMakeVisible(clientsHeading);

    // Transport.
    playButton.onClick = [this] { togglePlay(); };
    addAndMakeVisible(playButton);

    // Clock source toggle: Internal (mu-link is master) ↔ External MIDI (slave to MIDI clock).
    clockSourceButton.setClickingTogglesState(true);
    clockSourceButton.setColour(juce::TextButton::buttonColourId,   lnf.colour(MuLookAndFeel::panelBackground));
    clockSourceButton.setColour(juce::TextButton::buttonOnColourId, lnf.colour(MuLookAndFeel::knobReverb));
    clockSourceButton.onClick = [this]
    {
        const bool external = clockSourceButton.getToggleState();
        clockSourceButton.setButtonText(external ? "Clock: Ext MIDI" : "Clock: Internal");
        server.setClockSource(external ? mu_link::ClockSource::ExternalMidi
                                       : mu_link::ClockSource::Internal);
    };
    addAndMakeVisible(clockSourceButton);

    tempoSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    tempoSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 72, 22);
    tempoSlider.setRange(20.0, 300.0, 0.1);
    tempoSlider.setValue(120.0, juce::dontSendNotification);
    tempoSlider.setTextValueSuffix(" BPM");
    tempoSlider.onValueChange = [this] { server.setTempo(tempoSlider.getValue()); };
    addAndMakeVisible(tempoSlider);

    // Meters — eight client slots + the summed master.
    masterMeter.getLevel = [this] { return server.masterPeak(); };
    addAndMakeVisible(masterMeter);
    styleLabel(masterLabel, "MASTER", juce::Justification::centred, MuLookAndFeel::valueText, 11.0f, true);
    addAndMakeVisible(masterLabel);

    for (int i = 0; i < (int) clients.size(); ++i)
    {
        auto& strip = clients[(size_t) i];
        strip.meter.getLevel = [this, i] { return server.clientPeak(i); };
        addAndMakeVisible(strip.meter);
        styleLabel(strip.name, "—", juce::Justification::centred, MuLookAndFeel::mutedText, 10.0f);
        addAndMakeVisible(strip.name);

        // Per-client gain knob (linear 0–1.5, unity at 1.0).
        strip.gain.setSliderStyle(juce::Slider::RotaryVerticalDrag);
        strip.gain.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        strip.gain.setRange(0.0, 1.5, 0.01);
        strip.gain.setValue(1.0, juce::dontSendNotification);
        strip.gain.onValueChange = [this, i] { server.setClientGain(i, (float) clients[(size_t) i].gain.getValue()); };
        addAndMakeVisible(strip.gain);

        // Mute / solo toggles.
        for (auto* b : { &strip.mute, &strip.solo })
        {
            b->setClickingTogglesState(true);
            b->setColour(juce::TextButton::buttonColourId,   lnf.colour(MuLookAndFeel::panelBackground));
            b->setColour(juce::TextButton::textColourOffId,  lnf.colour(MuLookAndFeel::labelText));
            addAndMakeVisible(b);
        }
        strip.mute.setColour(juce::TextButton::buttonOnColourId, lnf.colour(MuLookAndFeel::vuMeterClip));
        strip.solo.setColour(juce::TextButton::buttonOnColourId, lnf.colour(MuLookAndFeel::knobLevel));
        strip.mute.onClick = [this, i] { server.setClientMute(i, clients[(size_t) i].mute.getToggleState()); };
        strip.solo.onClick = [this, i] { server.setClientSolo(i, clients[(size_t) i].solo.getToggleState()); };
    }

    // Master gain knob, under the master meter.
    masterGain.setSliderStyle(juce::Slider::RotaryVerticalDrag);
    masterGain.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    masterGain.setRange(0.0, 1.5, 0.01);
    masterGain.setValue(1.0, juce::dontSendNotification);
    masterGain.onValueChange = [this] { server.setMasterGain((float) masterGain.getValue()); };
    addAndMakeVisible(masterGain);

    // Match the engine's initial state (it begins playing when a device opens).
    server.setTempo(120.0);
    server.setPlaying(true);

    setSize(900, 620);
    startTimerHz(12);
}

MuLinkComponent::~MuLinkComponent()
{
    stopTimer();
    setLookAndFeel(nullptr);
}

void MuLinkComponent::togglePlay()
{
    playing = ! playing;
    server.setPlaying(playing);
    playButton.setButtonText(playing ? "Stop" : "Play");
}

void MuLinkComponent::timerCallback()
{
    // Reflect live attach/detach in the client strip names + colours.
    auto& reg = server.registry();
    for (int i = 0; i < (int) clients.size(); ++i)
    {
        const bool active = reg.slots[i].active.load(std::memory_order_acquire) != 0;
        auto& name = clients[(size_t) i].name;
        if (active)
        {
            name.setText(juce::String(reg.slots[i].name), juce::dontSendNotification);
            name.setColour(juce::Label::textColourId, lnf.colour(MuLookAndFeel::valueText));
        }
        else
        {
            name.setText("—", juce::dontSendNotification);
            name.setColour(juce::Label::textColourId, lnf.colour(MuLookAndFeel::mutedText));
        }
    }

    // Keep MIDI-clock-out routed to whatever the picker has selected (none → no-op).
    server.setMidiClockOutput(server.audioDeviceManager().getDefaultMidiOutput());

    // In external-MIDI-clock mode the tempo + transport are driven by the incoming clock,
    // so reflect them read-only and lock out the user controls; restore them in internal mode.
    const bool external = server.clockSourceMode() == mu_link::ClockSource::ExternalMidi;
    tempoSlider.setEnabled(! external);
    playButton .setEnabled(! external);
    if (external)
    {
        const double bpm = server.externalBpm();
        if (bpm > 0.0) tempoSlider.setValue(bpm, juce::dontSendNotification);
        playButton.setButtonText(server.externalRunning() ? "Stop" : "Play");
    }
}

void MuLinkComponent::paint(juce::Graphics& g)
{
    g.fillAll(lnf.colour(MuLookAndFeel::windowBackground));

    // Subtle panel behind the meter strip so it reads as a unit.
    auto area = getLocalBounds().reduced(16);
    area.removeFromLeft(348 + 16);
    area.removeFromTop(54 + 8 + 40 + 16 + 20);
    g.setColour(lnf.colour(MuLookAndFeel::panelBackground));
    g.fillRoundedRectangle(area.toFloat(), 6.0f);
}

void MuLinkComponent::resized()
{
    auto area = getLocalBounds().reduced(16);

    // Left: the device picker.
    deviceSelector.setBounds(area.removeFromLeft(348));
    area.removeFromLeft(16);

    // Right column: header → transport → clients heading → meter strip.
    auto header = area.removeFromTop(54);
    titleLabel.setBounds(header.removeFromTop(34));
    subtitleLabel.setBounds(header);
    area.removeFromTop(8);

    auto transport = area.removeFromTop(40);
    playButton.setBounds(transport.removeFromLeft(76));
    transport.removeFromLeft(10);
    clockSourceButton.setBounds(transport.removeFromLeft(120));
    transport.removeFromLeft(14);
    tempoSlider.setBounds(transport);
    area.removeFromTop(16);

    clientsHeading.setBounds(area.removeFromTop(20));

    // Meter strip: master pinned right, the eight client meters fill the rest.
    auto meters = area.reduced(10, 8);
    const int labelH = 18;

    // Each column, bottom-up: mute|solo row → gain knob → name → meter.
    const int ctrlH = 20, knobH = 40;

    auto masterBlock = meters.removeFromRight(56);
    masterGain.setBounds(masterBlock.removeFromBottom(knobH).reduced(6, 2));
    masterLabel.setBounds(masterBlock.removeFromBottom(labelH));
    masterMeter.setBounds(masterBlock.reduced(10, 0));
    meters.removeFromRight(14);

    const int n  = (int) clients.size();
    const int cw = juce::jmax(1, meters.getWidth() / n);
    for (int i = 0; i < n; ++i)
    {
        auto& strip = clients[(size_t) i];
        auto  col   = meters.removeFromLeft(cw);

        auto ctrl = col.removeFromBottom(ctrlH);
        const int bw = juce::jmax(1, ctrl.getWidth() / 2 - 2);
        strip.mute.setBounds(ctrl.removeFromLeft(bw));
        strip.solo.setBounds(ctrl.removeFromRight(bw));
        strip.gain.setBounds(col.removeFromBottom(knobH).reduced(6, 2));
        strip.name.setBounds(col.removeFromBottom(labelH));
        strip.meter.setBounds(col.reduced(6, 0));
    }
}
