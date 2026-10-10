#include "UI/PresetDropdown.h"   // shared preset scan + selector fill
#include "TransportBar.h"

static const juce::String kPlay = juce::String(juce::CharPointer_UTF8("\xe2\x96\xb6"));
static const juce::String kStop = juce::String(juce::CharPointer_UTF8("\xe2\x96\xa0"));
static const juce::String kGear = juce::String(juce::CharPointer_UTF8("\xe2\x9a\x99"));

TransportBar::TransportBar(ProcessorBase& p)
    : proc(p),
      isStandalone(p.wrapperType == juce::AudioProcessor::wrapperType_Standalone)
{
    playBtn.onClick = [this]
    {
        if (!isStandalone) return;
        proc.toggleInternalPlay();
        refreshPlayBtn();
    };
    playBtn.setEnabled(true); // always enabled so colour reflects state; click is no-op in plugin
    addAndMakeVisible(playBtn);

    if (isStandalone)
    {
        bpmInput.setDecimals(MuLookAndFeel::kBpmDecimals);
        bpmInput.setStep(MuLookAndFeel::kBpmStep);
        bpmInput.setFineStep(MuLookAndFeel::kBpmFineStep);
        bpmInput.setValueD(proc.getInternalBpm());
        bpmInput.onChangeD = [this](double v) {
            proc.setInternalBpm(v);
            if (onStatusUpdate) onStatusUpdate("BPM", juce::String(v, MuLookAndFeel::kBpmDecimals));
        };
        bpmInput.setShowStepButtons(false);
        bpmInput.setLabelInline(true);
        addAndMakeVisible(bpmInput);

        clockLamp.setLabel("Clock");
        clockLamp.onStatusUpdate = [this](const juce::String& n, const juce::String& v) { if (onStatusUpdate) onStatusUpdate(n, v); };
        addChildComponent(clockLamp);   // shown while MIDI clock ticks drive the transport
    }

    posLabel.setJustificationType(juce::Justification::centred);
    posLabel.setFont(juce::Font(juce::FontOptions{}.withHeight(11.0f)));
    posLabel.setText("1.1.1", juce::dontSendNotification);
    addAndMakeVisible(posLabel);

    presetDropdown.setPlaceholderText("<unnamed preset>");
    presetDropdown.onChange = [this](int id)
    {
        int idx = id - 1;
        if (idx >= 0 && idx < (int)presetFiles.size())
        {
            // don't clear the selection — keep the loaded preset's name
            // visible so the user always sees what's currently loaded. The
            // editor's onPresetSelected does the actual load.
            if (onPresetSelected) onPresetSelected(presetFiles[idx]);
        }
    };
    addAndMakeVisible(presetDropdown);

    // Staging badge for a pending full-preset hot-swap — orange "SWP" pill on the
    // preset name, mirroring the rhythm hot-swap badge on the sidebar items.
    // Added after (on top of) the dropdown; mouse-transparent so it doesn't block it.
    presetStagingBadge.setText("SWP", juce::dontSendNotification);
    presetStagingBadge.setJustificationType(juce::Justification::centred);
    presetStagingBadge.setFont(juce::Font(juce::FontOptions{}.withHeight(8.0f)));
    presetStagingBadge.setColour(juce::Label::backgroundColourId, juce::Colours::orange.withAlpha(0.85f));
    presetStagingBadge.setColour(juce::Label::textColourId, juce::Colours::black);
    presetStagingBadge.setInterceptsMouseClicks(false, false);
    addChildComponent(presetStagingBadge);   // hidden until a swap is staged

    newBtn.onClick  = [this] { if (onNewPreset)  onNewPreset();  };
    addAndMakeVisible(newBtn);

    saveBtn.onClick = [this] { if (onSavePreset) onSavePreset(); };
    addAndMakeVisible(saveBtn);

    mixerBtn.setClickingTogglesState(true);
    mixerBtn.onClick = [this] { if (onMixerToggle) onMixerToggle(); };
    addAndMakeVisible(mixerBtn);

    gearBtn.setButtonText(kGear);
    gearBtn.onClick = [this] { if (onSettingsToggle) onSettingsToggle(); };
    addAndMakeVisible(gearBtn);

    populatePresetDropdown();
    refreshPlayBtn();
    startTimerHz(mu_ui::kUiRefreshHz);
}

TransportBar::~TransportBar()
{
    stopTimer();
}

void TransportBar::setLogoText(const juce::String& text)
{
    logoText = text;
    repaint();
}

void TransportBar::setShowPresetControls(bool show)
{
    showPresetControls = show;
    presetDropdown.setVisible(show);
    newBtn        .setVisible(show);
    saveBtn       .setVisible(show);
    if (!show) presetStagingBadge.setVisible(false);
    resized();
}

void TransportBar::setShowMixerToggle(bool show)
{
    showMixerToggle = show;
    mixerBtn.setVisible(show);
    resized();
}

void TransportBar::setShowSettingsButton(bool show)
{
    showSettingsButton = show;
    gearBtn.setVisible(show);
    resized();
}

void TransportBar::setLoopSection(juce::Component* component, int width)
{
    if (loopSection == component && loopSectionWidth == width) return;
    if (loopSection != nullptr)
        removeChildComponent(loopSection);
    loopSection      = component;
    loopSectionWidth = (component != nullptr) ? juce::jmax(0, width) : 0;
    if (loopSection != nullptr)
        addAndMakeVisible(loopSection);
    resized();
}

void TransportBar::timerCallback()
{
    refreshPlayBtn();
    updatePositionLabel();

    if (isStandalone)
    {
        const bool midiClockBpm = proc.getMidiSyncEnabled() && proc.getMidiSyncMessages() != 1;
        if (midiClockBpm && proc.getMidiClockBpm() > 0.0)   // 0 = no clock heard yet: keep the field's value
            bpmInput.setValueD(proc.getMidiClockBpm());
        bpmInput.setEnabled(!midiClockBpm);

        const bool midiTransport = proc.getMidiSyncEnabled() && proc.getMidiSyncMessages() != 0;
        playBtn.setEnabled(!midiTransport);

        // The clock lamp shows while the clock's ticks drive the transport; re-lay out only when
        // it appears or goes.
        if (midiClockBpm != clockShown)
        {
            clockShown = midiClockBpm;
            clockLamp.setVisible(clockShown);
            resized();
        }
        if (clockShown) refreshClockLamp();
    }

    // Show the staging badge while a full-preset hot-swap is queued for the loop point.
    if (showPresetControls)
        presetStagingBadge.setVisible(proc.hasPendingFullPreset());
}

void TransportBar::refreshClockLamp()
{
    using Id = MuLookAndFeel::ColourIds;
    using State = MidiClockSync::ClockState;
    const auto& L = MuLookAndFeel::lighting();
    const auto state = proc.getMidiClockState();

    juce::String status;
    switch (state)
    {
        case State::Locked:
            clockLamp.setState(MuLookAndFeel::colour(Id::segmentPositiveBorder), L.lampOn);
            status = "Locked at " + juce::String(proc.getMidiClockBpm(), MuLookAndFeel::kBpmDecimals) + " BPM";
            break;
        case State::Lost:
            clockLamp.setState(MuLookAndFeel::colour(Id::indicatorFault), juce::jmin(1.0f, L.lampOn + L.lampPlayhead));
            status = "Lost, transport stopped";
            break;
        case State::Waiting:
        case State::Off:
        default:
            clockLamp.setState(MuLookAndFeel::colour(Id::segmentWarningBorder), L.lampDim);
            status = "Waiting for clock";
            break;
    }
    clockLamp.setStatus("MIDI Clock", status);

    // A lost clock is announced once, without hover, so a dead cable shows on stage.
    if (state == State::Lost && shownClockState != State::Lost && onStatusUpdate)
        onStatusUpdate("MIDI Clock", status);
    shownClockState = state;
}

void TransportBar::refreshPlayBtn()
{
    using Id = MuLookAndFeel::ColourIds;
    if (isStandalone)
    {
        const bool playing = proc.isInternalPlaying() || proc.isMidiClockPlaying();
        playBtn.setButtonText(playing ? kStop : kPlay);
        if (playing)
        {
            playBtn.setColour(juce::TextButton::buttonColourId,  MuLookAndFeel::colour(Id::transportWhilePlayingBg));
            playBtn.setColour(juce::TextButton::textColourOffId, MuLookAndFeel::colour(Id::textBright));
        }
        else
        {
            playBtn.setColour(juce::TextButton::buttonColourId,  MuLookAndFeel::colour(Id::transportWhileStoppedBg));
            playBtn.setColour(juce::TextButton::textColourOffId, MuLookAndFeel::colour(Id::textBright));
        }
    }
    else
    {
        // Reflect DAW transport state via icon + colour (matches standalone:
        // ▶ play → ■ stop while the host plays); button is non-interactive in
        // plugin mode — the host owns transport, so this is display-only.
        const bool dawPlaying = proc.isHostPlaying();

        playBtn.setButtonText(dawPlaying ? kStop : kPlay);
        if (dawPlaying)
        {
            playBtn.setColour(juce::TextButton::buttonColourId,  MuLookAndFeel::colour(Id::transportWhilePlayingBg));
            playBtn.setColour(juce::TextButton::textColourOffId, MuLookAndFeel::colour(Id::textBright));
        }
        else
        {
            playBtn.setColour(juce::TextButton::buttonColourId,  MuLookAndFeel::colour(Id::transportWhileStoppedBg));
            playBtn.setColour(juce::TextButton::textColourOffId, MuLookAndFeel::colour(Id::textBright));
        }
    }
}

void TransportBar::updatePositionLabel()
{
    double beatPos = 0.0;
    bool   gotPos  = false;
    int    num = 4, den = 4;          // the standalone counts in the meter chosen in Settings
    double barStart = 0.0;
    bool   hasBarStart = false;

    if (!isStandalone)
    {
        gotPos = proc.getHostPpqPosition(beatPos);
        proc.getHostTimeSignature(num, den);
        hasBarStart = proc.getHostBarStartPpq(barStart);
    }
    else
    {
        beatPos = proc.getInternalBeatPos();
        gotPos  = true;
        proc.getTimeSignature(num, den);
    }

    if (!gotPos)
    {
        if (shownPos != -1) posLabel.setText("---", juce::dontSendNotification);
        shownPos = -1;
        return;
    }

    const auto p = mu_core::barPositionOf(beatPos, num, den, hasBarStart, barStart);

    // Rebuild the text only when the shown position changes, not on every tick.
    const int pos = (p.bar * 64 + p.beat) * 16 + p.sub;
    if (pos == shownPos) return;
    shownPos = pos;
    posLabel.setText(juce::String(p.bar) + "." + juce::String(p.beat) + "." + juce::String(p.sub),
                     juce::dontSendNotification);
}

void TransportBar::populatePresetDropdown()
{
    mu_ui::fillPresetDropdown(presetDropdown, presetFiles,
                              mu_pp::listPresetsByCategory(proc.getPresetsDir(), proc.getFullPresetExtension()));
}

void TransportBar::refreshPresets()
{
    populatePresetDropdown();
}

void TransportBar::setLoadedPreset(const juce::File& file)
{
    // select the dropdown item whose stored File matches the loaded one
    // (by full path) so the dropdown text reflects the active preset. An
    // invalid file or one not in the dropdown list reverts to the placeholder.
    if (!file.existsAsFile())
    {
        presetDropdown.setSelectedId(0, juce::dontSendNotification);
        return;
    }
    const auto target = file.getFullPathName();
    for (int i = 0; i < (int)presetFiles.size(); ++i)
    {
        if (presetFiles[i].getFullPathName() == target)
        {
            presetDropdown.setSelectedId(i + 1, juce::dontSendNotification);
            return;
        }
    }
    presetDropdown.setSelectedId(0, juce::dontSendNotification);
}

juce::File TransportBar::getLoadedPresetFile() const
{
    const int id = presetDropdown.getSelectedId();
    const int idx = id - 1;
    if (idx >= 0 && idx < (int)presetFiles.size())
        return presetFiles[idx];
    return {};
}

void TransportBar::setMixerActive(bool active)
{
    mixerBtn.setButtonText(active ? "Sequencer" : "Mixer");
}

int TransportBar::getPresetDropdownLeft() const noexcept { return presetDropdown.getX(); }

void TransportBar::setSaveEnabled(bool enabled)
{
    saveBtn.setEnabled(enabled);
    saveBtn.setAlpha(enabled ? 1.0f : 0.35f);
}

void TransportBar::mouseDown(const juce::MouseEvent& e)
{
    if (e.x < mu_ui::s(kLogoW) && onLogoClicked)
        onLogoClicked();
}

void TransportBar::paint(juce::Graphics& g)
{
    using Id = MuLookAndFeel::ColourIds;

    g.setColour(MuLookAndFeel::colour(Id::panelBackground));
    g.fillAll();

    // Two sub-pane borders: transport (play+bpm+pos) and optional loop section
    // (when the product has supplied a loop component via setLoopSection).
    const juce::Colour borderCol = MuLookAndFeel::colour(Id::segmentInactiveBorder);
    g.setColour(borderCol);
    if (!transportPaneBounds.isEmpty())
        g.drawRoundedRectangle(transportPaneBounds.toFloat(), mu_ui::sf(3.0f), 1.0f);
    if (!loopPaneBounds.isEmpty())
        g.drawRoundedRectangle(loopPaneBounds.toFloat(), mu_ui::sf(3.0f), 1.0f);

    g.setColour(MuLookAndFeel::colour(Id::headingText));
    g.setFont(juce::Font(juce::FontOptions{}.withHeight(mu_ui::sf(14.0f))));
    g.drawText(logoText, mu_ui::s(8), 0, mu_ui::s(kLogoW - 8), getHeight(),
               juce::Justification::centredLeft, false);
}

void TransportBar::resized()
{
    using mu_ui::s;
    const int h      = getHeight();
    const int inset  = s(2);   // pane border vertical inset
    const int pad    = s(5);   // item vertical padding — gives visible gap top/bottom
    const int btnY   = pad;
    const int btnH   = h - 2 * pad;
    const int gap    = s(kGap);
    const int padIn  = s(6);   // inner pane padding — matches inter-item gap for even ends

    // ── Transport sub-pane: [play] [bpm] [pos] ────────────────────────────
    const int tpOuterX = s(kLogoW) + gap;
    int x = tpOuterX + padIn;

    playBtn.setBounds(x, btnY, s(kPlayW), btnH);
    x += s(kPlayW) + gap;

    if (isStandalone)
    {
        bpmInput.setBounds(x, btnY, s(kBpmW), btnH);
        x += s(kBpmW) + gap;
        if (clockShown)
        {
            clockLamp.setBounds(x, btnY, s(kClockW), btnH);
            x += s(kClockW) + gap;
        }
    }

    posLabel.setBounds(x, btnY, s(kPosW), btnH);
    x += s(kPosW) + padIn;

    transportPaneBounds = { tpOuterX, inset, x - tpOuterX, h - 2 * inset };

    // ── Optional loop section: positioned right of the transport pane ─────
    if (loopSection != nullptr && loopSectionWidth > 0)
    {
        // loopSectionWidth is an unscaled baseline (the product passes kWidth) — scale
        // it like every other transport element so the pane matches its s()-scaled
        // children in Large mode (otherwise the step counter overflows + clips).
        const int lpOuterX = transportPaneBounds.getRight() + gap;
        loopPaneBounds = { lpOuterX, inset, s(loopSectionWidth), h - 2 * inset };
        loopSection->setBounds(loopPaneBounds);
    }
    else
    {
        loopPaneBounds = {};
    }

    // ── Right group (right to left): Mixer | Gear | Save | Preset ─────────
    int rightEdge = getWidth() - gap;

    if (showMixerToggle)
    {
        mixerBtn.setBounds(rightEdge - s(kMixerW), btnY, s(kMixerW), btnH);
        rightEdge -= s(kMixerW) + gap;
    }

    if (showSettingsButton)
    {
        gearBtn.setBounds(rightEdge - s(kGearW), btnY, s(kGearW), btnH);
        rightEdge -= s(kGearW) + gap;
    }

    if (showPresetControls)
    {
        saveBtn.setBounds(rightEdge - s(kSaveW), btnY, s(kSaveW), btnH);
        rightEdge -= s(kSaveW) + gap;

        newBtn.setBounds(rightEdge - s(kNewW), btnY, s(kNewW), btnH);
        rightEdge -= s(kNewW) + gap;

        const int presetLeft = (loopPaneBounds.isEmpty() ? transportPaneBounds.getRight()
                                                          : loopPaneBounds.getRight()) + gap;
        presetDropdown.setBounds(presetLeft, btnY, rightEdge - presetLeft, btnH);

        // Staging badge: small pill at the top-right of the preset dropdown.
        const int badgeW = s(28);
        const int badgeH = s(11);
        presetStagingBadge.setBounds(presetDropdown.getRight() - badgeW - s(4),
                                     presetDropdown.getY() + s(2), badgeW, badgeH);
    }
}
