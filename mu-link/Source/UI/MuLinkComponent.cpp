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
    : server(serverToShow)
{
    setLookAndFeel(&lnf);

    // Audio/MIDI device picker now lives behind the Options button (a dialog, like the
    // synth standalones), freeing the main window for a full-width mixer.
    optionsButton.onClick = [this] { showOptions(); };
    addAndMakeVisible(optionsButton);

    styleLabel(titleLabel,    juce::String(juce::CharPointer_UTF8("\xce\xbc-link")), juce::Justification::centredLeft, MuLookAndFeel::headingText, 26.0f, true);
    styleLabel(subtitleLabel, juce::String(juce::CharPointer_UTF8("master clock  \xc2\xb7  audio bus")), juce::Justification::centredLeft, MuLookAndFeel::labelText, 13.0f);
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
        styleLabel(strip.name, juce::String::charToString(0x2014), juce::Justification::centred, MuLookAndFeel::mutedText, 10.0f);
        addAndMakeVisible(strip.name);

        // Per-client gain — vertical fader (matches the standard app mixer), linear 0–1.5,
        // unity at 1.0.
        strip.gain.setSliderStyle(juce::Slider::LinearVertical);
        strip.gain.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        strip.gain.setRange(0.0, 1.5, 0.01);
        strip.gain.setValue(1.0, juce::dontSendNotification);
        strip.gain.setDoubleClickReturnValue(true, 1.0);   // double-click → unity
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

        // Per-client 3-band EQ insert — four small vertical knobs (Low / Mid / Mid-Hz / High).
        // Normalised 0..1, 0.5 = flat; the server arms the EQ only when a band leaves centre.
        static const char* const kEqNames[4] = { "Low", "Mid", "Hz", "High" };
        for (int b = 0; b < 4; ++b)
        {
            auto& k = strip.eq[(size_t) b];
            k.setSliderStyle(juce::Slider::RotaryVerticalDrag);
            k.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
            k.setRange(0.0, 1.0, 0.001);
            k.setDoubleClickReturnValue(true, 0.5);   // double-click → flat
            k.setValue(server.clientEqValue(i, b), juce::dontSendNotification);
            k.onValueChange = [this, i, b] { server.setClientEqParam(i, b, (float) clients[(size_t) i].eq[(size_t) b].getValue()); };
            addAndMakeVisible(k);
            styleLabel(strip.eqLabel[(size_t) b], kEqNames[b], juce::Justification::centred, MuLookAndFeel::mutedText, 9.0f);
            addAndMakeVisible(strip.eqLabel[(size_t) b]);
        }
    }

    // Master gain — vertical fader beside the master meter (matches the standard app mixer).
    masterGain.setSliderStyle(juce::Slider::LinearVertical);
    masterGain.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    masterGain.setRange(0.0, 1.5, 0.01);
    masterGain.setValue(1.0, juce::dontSendNotification);
    masterGain.setDoubleClickReturnValue(true, 1.0);
    masterGain.onValueChange = [this] { server.setMasterGain((float) masterGain.getValue()); };
    addAndMakeVisible(masterGain);

    // ── Scenes: trigger buttons + the selected scene's per-client (program, channel) cells ──
    styleLabel(scenesHeading, juce::String(juce::CharPointer_UTF8("SCENES  \xc2\xb7  click to recall")), juce::Justification::centredLeft,
               MuLookAndFeel::labelText, 12.0f, true);
    addAndMakeVisible(scenesHeading);

    for (int s = 0; s < kNumScenes; ++s)
    {
        auto& b = sceneButtons[(size_t) s];
        b.setButtonText(juce::String(s + 1));
        b.setClickingTogglesState(false);
        b.setColour(juce::TextButton::buttonColourId,   lnf.colour(MuLookAndFeel::panelBackground));
        b.setColour(juce::TextButton::buttonOnColourId, lnf.colour(MuLookAndFeel::knobLevel));
        b.onClick = [this, s] { selectScene(s); triggerScene(s); };
        addAndMakeVisible(b);
    }

    for (int i = 0; i < (int) clients.size(); ++i)
    {
        auto& strip = clients[(size_t) i];
        strip.sceneOn.setColour(juce::ToggleButton::tickColourId, lnf.colour(MuLookAndFeel::knobLevel));
        strip.sceneOn.onClick = [this, i]
        {
            scenes[(size_t) editScene][(size_t) i].enabled = clients[(size_t) i].sceneOn.getToggleState();
            saveScenes();
        };
        addAndMakeVisible(strip.sceneOn);

        auto styleNum = [this](juce::Label& l)
        {
            l.setEditable(true);
            l.setJustificationType(juce::Justification::centred);
            l.setColour(juce::Label::textColourId,       lnf.colour(MuLookAndFeel::valueText));
            l.setColour(juce::Label::backgroundColourId, lnf.colour(MuLookAndFeel::windowBackground));
            l.setFont(juce::Font(juce::FontOptions(11.0f, juce::Font::plain)));
            addAndMakeVisible(l);
        };
        styleNum(strip.scenePc);
        styleNum(strip.sceneCh);
        strip.scenePc.onTextChange = [this, i]
        {
            scenes[(size_t) editScene][(size_t) i].program =
                juce::jlimit(0, 127, clients[(size_t) i].scenePc.getText().getIntValue());
            refreshSceneEditors();
            saveScenes();
        };
        strip.sceneCh.onTextChange = [this, i]
        {
            scenes[(size_t) editScene][(size_t) i].channel =
                juce::jlimit(1, 16, clients[(size_t) i].sceneCh.getText().getIntValue());
            refreshSceneEditors();
            saveScenes();
        };
    }

    loadScenes();
    selectScene(0);

    // Start stopped — the user presses Play to run the master transport.
    server.setTempo(120.0);
    server.setPlaying(false);

    setSize(900, 880);   // taller: the per-strip EQ stack needs the room
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

void MuLinkComponent::showOptions()
{
    // Audio/MIDI device picker in a dialog (like the synth standalones). The dialog owns the
    // selector; it binds to the live AudioDeviceManager so changes apply immediately.
    auto selector = std::make_unique<juce::AudioDeviceSelectorComponent>(
        server.audioDeviceManager(),
        0, 0,        // no audio inputs
        2, 2,        // stereo output
        true,        // MIDI input (external clock source)
        true,        // MIDI output (MIDI-clock-out port)
        true,        // channels as stereo pairs
        false);      // no advanced options
    selector->setSize(380, 460);

    juce::DialogWindow::LaunchOptions opts;
    opts.content.setOwned(selector.release());
    opts.dialogTitle                  = juce::String(juce::CharPointer_UTF8("\xce\xbc-link \xe2\x80\x94 Audio / MIDI Options"));
    opts.dialogBackgroundColour       = lnf.colour(MuLookAndFeel::windowBackground);
    opts.escapeKeyTriggersCloseButton = true;
    opts.useNativeTitleBar            = true;
    opts.resizable                    = false;
    opts.launchAsync();
}

void MuLinkComponent::selectScene(int s)
{
    editScene = juce::jlimit(0, kNumScenes - 1, s);
    for (int i = 0; i < kNumScenes; ++i)
        sceneButtons[(size_t) i].setToggleState(i == editScene, juce::dontSendNotification);   // highlight
    refreshSceneEditors();
}

void MuLinkComponent::refreshSceneEditors()
{
    for (int i = 0; i < (int) clients.size(); ++i)
    {
        const auto& cell = scenes[(size_t) editScene][(size_t) i];
        clients[(size_t) i].sceneOn.setToggleState(cell.enabled, juce::dontSendNotification);
        clients[(size_t) i].scenePc.setText(juce::String(cell.program), juce::dontSendNotification);
        clients[(size_t) i].sceneCh.setText(juce::String(cell.channel), juce::dontSendNotification);
    }
}

void MuLinkComponent::triggerScene(int s)
{
    // Send each enabled + connected client its targeted program change; the client's
    // standalone bridge injects it → the product's existing PC→preset hot-swap fires.
    auto& reg = server.registry();
    for (int i = 0; i < (int) clients.size(); ++i)
    {
        const auto& cell = scenes[(size_t) s][(size_t) i];
        if (! cell.enabled)
            continue;
        if (reg.slots[i].active.load(std::memory_order_acquire) == 0)
            continue;   // slot not connected
        mu_link::sendProgramChange(reg.slots[i], cell.program, cell.channel);
    }
}

juce::File MuLinkComponent::scenesFile() const
{
    return juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
               .getChildFile("TDP").getChildFile("muLink").getChildFile("scenes.json");
}

void MuLinkComponent::saveScenes() const
{
    juce::Array<juce::var> sceneArr;
    for (int s = 0; s < kNumScenes; ++s)
    {
        juce::Array<juce::var> cells;
        for (int i = 0; i < (int) clients.size(); ++i)
        {
            const auto& c = scenes[(size_t) s][(size_t) i];
            auto* o = new juce::DynamicObject();
            o->setProperty("on", c.enabled);
            o->setProperty("pc", c.program);
            o->setProperty("ch", c.channel);
            cells.add(juce::var(o));
        }
        sceneArr.add(juce::var(cells));
    }
    const auto f = scenesFile();
    f.getParentDirectory().createDirectory();
    f.replaceWithText(juce::JSON::toString(juce::var(sceneArr)));
}

void MuLinkComponent::loadScenes()
{
    const auto f = scenesFile();
    if (! f.existsAsFile())
        return;
    const auto parsed = juce::JSON::parse(f.loadFileAsString());
    auto* sceneArr = parsed.getArray();
    if (sceneArr == nullptr)
        return;
    for (int s = 0; s < juce::jmin(kNumScenes, sceneArr->size()); ++s)
    {
        auto* cells = (*sceneArr)[s].getArray();
        if (cells == nullptr)
            continue;
        for (int i = 0; i < juce::jmin((int) clients.size(), cells->size()); ++i)
        {
            const auto& cv = (*cells)[i];
            auto& c = scenes[(size_t) s][(size_t) i];
            c.enabled = (bool) cv.getProperty("on", false);
            c.program = juce::jlimit(0, 127, (int) cv.getProperty("pc", 0));
            c.channel = juce::jlimit(1, 16,  (int) cv.getProperty("ch", 9));
        }
    }
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
            name.setText(juce::String::charToString(0x2014), juce::dontSendNotification);
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

    // Subtle panel behind the full-width mixer so it reads as a unit.
    auto area = getLocalBounds().reduced(16);
    area.removeFromTop(54 + 8 + 40 + 16 + 20);
    g.setColour(lnf.colour(MuLookAndFeel::panelBackground));
    g.fillRoundedRectangle(area.toFloat(), 6.0f);
}

void MuLinkComponent::resized()
{
    auto area = getLocalBounds().reduced(16);
    const int masterW = 60;

    // Header → transport → clients heading → full-width mixer.
    auto header = area.removeFromTop(54);
    titleLabel.setBounds(header.removeFromTop(34));
    subtitleLabel.setBounds(header);
    area.removeFromTop(8);

    auto transport = area.removeFromTop(40);
    playButton.setBounds(transport.removeFromLeft(76));
    transport.removeFromLeft(10);
    clockSourceButton.setBounds(transport.removeFromLeft(120));
    transport.removeFromLeft(14);
    optionsButton.setBounds(transport.removeFromRight(96));
    transport.removeFromRight(14);
    tempoSlider.setBounds(transport);
    area.removeFromTop(16);

    clientsHeading.setBounds(area.removeFromTop(20));

    // Scenes band pinned to the bottom; the mixer fills the space between.
    auto scenesBand = area.removeFromBottom(108);
    scenesHeading.setBounds(scenesBand.removeFromTop(18));
    scenesBand.removeFromTop(4);
    {
        auto btnRow = scenesBand.removeFromTop(28);
        const int sbw = juce::jmax(1, btnRow.getWidth() / kNumScenes);
        for (int s = 0; s < kNumScenes; ++s)
            sceneButtons[(size_t) s].setBounds(btnRow.removeFromLeft(sbw).reduced(3, 2));
    }
    scenesBand.removeFromTop(6);
    {
        // Per-client cells aligned under the strip columns: mirror the mixer's 10 px
        // horizontal inset + the master block on the right, so the 8 cells line up.
        auto cellsRow = scenesBand.reduced(10, 0);
        cellsRow.removeFromRight(masterW + 14);
        const int n = (int) clients.size();
        const int cw = juce::jmax(1, cellsRow.getWidth() / n);
        for (int i = 0; i < n; ++i)
        {
            auto cell = cellsRow.removeFromLeft(cw).reduced(3, 0);
            clients[(size_t) i].sceneOn.setBounds(cell.removeFromTop(20));
            const int half = juce::jmax(1, cell.getWidth() / 2 - 1);
            clients[(size_t) i].scenePc.setBounds(cell.removeFromLeft(half));
            clients[(size_t) i].sceneCh.setBounds(cell.removeFromRight(half));
        }
    }
    area.removeFromBottom(12);

    // Mixer (full width): master pinned right, the eight client strips fill the rest.
    // Each strip top→bottom: name → EQ stack (High→Low) → fader + VU → mute/solo.
    auto meters = area.reduced(10, 8);
    const int labelH = 18, ctrlH = 20, vuW = 14, vuGap = 4;
    const int eqLabelH = 11, eqKnobH = 26;

    auto masterBlock = meters.removeFromRight(masterW);
    masterLabel.setBounds(masterBlock.removeFromTop(labelH));
    {
        auto fv = masterBlock.reduced(4, 2);
        masterMeter.setBounds(fv.removeFromRight(vuW));
        fv.removeFromRight(vuGap);
        masterGain.setBounds(fv);
    }
    meters.removeFromRight(14);

    const int n  = (int) clients.size();
    const int cw = juce::jmax(1, meters.getWidth() / n);
    for (int i = 0; i < n; ++i)
    {
        auto& strip = clients[(size_t) i];
        auto  col   = meters.removeFromLeft(cw).reduced(3, 0);

        strip.name.setBounds(col.removeFromTop(labelH));

        // EQ stack: High at the top → Low at the bottom (b = 3 High … 0 Low).
        for (int b = 3; b >= 0; --b)
        {
            auto band = col.removeFromTop(eqLabelH + eqKnobH);
            strip.eqLabel[(size_t) b].setBounds(band.removeFromTop(eqLabelH));
            strip.eq[(size_t) b].setBounds(band.withSizeKeepingCentre(eqKnobH, eqKnobH));
        }

        // Mute/solo pinned at the bottom; the fader + VU fill the middle.
        auto ctrl = col.removeFromBottom(ctrlH);
        const int bw = juce::jmax(1, ctrl.getWidth() / 2 - 2);
        strip.mute.setBounds(ctrl.removeFromLeft(bw));
        strip.solo.setBounds(ctrl.removeFromRight(bw));
        col.removeFromBottom(4);

        auto fv = col;
        strip.meter.setBounds(fv.removeFromRight(vuW));
        fv.removeFromRight(vuGap);
        strip.gain.setBounds(fv);
    }
}
