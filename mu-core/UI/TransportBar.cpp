#include "UI/PresetDropdown.h"   // shared preset scan + selector fill
#include "TransportBar.h"

static const juce::String kPlay = juce::String(juce::CharPointer_UTF8("\xe2\x96\xb6"));
static const juce::String kStop = juce::String(juce::CharPointer_UTF8("\xe2\x96\xa0"));
static const juce::String kGear = juce::String(juce::CharPointer_UTF8("\xe2\x9a\x99"));

TransportBar::TransportBar(mu_core::ShellBackend& backend)
    : proc(backend),
      isStandalone(backend.isStandaloneApp())
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

        // Tap tempo (internal clock only): triggered on mouse DOWN so the press time is the beat.
        tapBtn.setTriggeredOnMouseDown(true);
        tapBtn.setTooltip("Tap tempo");
        tapBtn.onClick = [this]
        {
            double bpm = 0.0;
            if (tapTempo.tap(juce::Time::getMillisecondCounterHiRes() * 0.001, bpm))
            {
                proc.setInternalBpm(bpm);
                bpmInput.setValueD(bpm);
                if (onStatusUpdate) onStatusUpdate("BPM", juce::String(bpm, MuLookAndFeel::kBpmDecimals));
            }
        };
        tapBtn.onStateChange = [this] { if (tapBtn.isOver() && onStatusUpdate) onStatusUpdate("Tap", "tap the beat to set the tempo"); };
        addAndMakeVisible(tapBtn);

        // Nudge: hold to bend the groove, release to return to exactly the set tempo.
        nudgeSlowBtn.setTooltip("Nudge slower (hold)");
        nudgeFastBtn.setTooltip("Nudge faster (hold)");
        const juce::String bendText = juce::String(juce::roundToInt(mu_core::kNudgeBendPercent)) + "%";
        nudgeSlowBtn.onStateChange = [this, bendText]
        {
            updateNudge();
            if (nudgeSlowBtn.isOver() && onStatusUpdate) onStatusUpdate("Nudge", "hold to slow the groove " + bendText);
        };
        nudgeFastBtn.onStateChange = [this, bendText]
        {
            updateNudge();
            if (nudgeFastBtn.isOver() && onStatusUpdate) onStatusUpdate("Nudge", "hold to speed up the groove " + bendText);
        };
        addAndMakeVisible(nudgeSlowBtn);
        addAndMakeVisible(nudgeFastBtn);

        clockLamp.setLabel("Clock");
        clockLamp.onStatusUpdate = [this](const juce::String& n, const juce::String& v) { if (onStatusUpdate) onStatusUpdate(n, v); };
        addChildComponent(clockLamp);   // shown while MIDI clock ticks drive the transport
    }

    posLabel.setJustificationType(juce::Justification::centred);
    posLabel.setFont(juce::Font(juce::FontOptions{}.withHeight(11.0f)));
    posLabel.setText("1.1.1", juce::dontSendNotification);
    addAndMakeVisible(posLabel);

    // Performance mode: the indicator replaces the preset group while the mode is on (added before the
    // staging badge so the badge sits on top of it).
    stepIndicator.onStatusUpdate = [this](const juce::String& n, const juce::String& v) { if (onStatusUpdate) onStatusUpdate(n, v); };
    addChildComponent(stepIndicator);

    presetDropdown.setPlaceholderText("<unnamed preset>");
    presetDropdown.onChange = [this](int id)
    {
        // The last item opens the Performance chooser; the dropdown keeps showing the loaded preset.
        if (id == kPerformancesItemId)
        {
            setLoadedPreset(getLoadedPresetFile());
            if (onChoosePerformance) onChoosePerformance();
            return;
        }
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

    // The Perf toggle: hidden unless the backend supports Performance mode (syncPerformance shows it).
    perfBtn.setTooltip("Performance mode (Ctrl+P)");
    perfBtn.onClick       = [this] { if (! perfBtn.suppressClick && ! modeShown) enterPerformanceMode(); };
    perfBtn.onHoldPress   = [this] { beginExitHold(juce::Time::getMillisecondCounterHiRes()); };
    perfBtn.onHoldCancel  = [this]
    {
        const bool wasHolding = exitHold.isHolding();
        cancelExitHold();
        // A release before the hold completed is the usual first try: say how leaving works, for longer than a glance.
        if (wasHolding && modeShown)
            showTimedStatus("Performance", "Keep the Perf button pressed for a full second to leave", MuLookAndFeel::kPerfHintMs);
    };
    addChildComponent(perfBtn);

    controllerWatch.onChange = [this] { controllerStatusChanged(); };

    populatePresetDropdown();
    refreshPlayBtn();
    syncPerformance();
    startTimerHz(mu_ui::kUiRefreshHz);
}

TransportBar::~TransportBar()
{
    proc.setNudgeDirection(0);   // a held nudge must not outlive the bar
    stopTimer();
}

// ─── Performance mode ────────────────────────────────────────────────────────

void TransportBar::PerfToggle::mouseDown(const juce::MouseEvent& e)
{
    juce::TextButton::mouseDown(e);
    if (getToggleState() && onHoldPress) onHoldPress();   // lit: the press starts the exit hold
}

void TransportBar::PerfToggle::mouseUp(const juce::MouseEvent& e)
{
    juce::TextButton::mouseUp(e);   // a click on the off button fires onClick here
    if (onHoldCancel) onHoldCancel();
    suppressClick = false;
}

void TransportBar::PerfToggle::mouseDrag(const juce::MouseEvent& e)
{
    juce::TextButton::mouseDrag(e);
    if (! reallyContains(e.getPosition(), false) && onHoldCancel) onHoldCancel();   // dragging off cancels
}

bool TransportBar::isPerformanceAvailable() const
{
    return proc.supportsPerformanceMode() && proc.getLiveView() != nullptr && proc.getControlSink() != nullptr;
}

void TransportBar::showTimedStatus(const juce::String& name, const juce::String& value, int ms)
{
    if (! onStatusUpdate) return;
    onStatusUpdate(name, value);

    // Clear it after a few seconds, unless a newer timed line has replaced it.
    const int serial = ++statusSerial;
    juce::Component::SafePointer<TransportBar> safe(this);
    juce::Timer::callAfterDelay(ms, [safe, serial]
    {
        if (auto* bar = safe.getComponent())
            if (bar->statusSerial == serial && bar->onStatusUpdate) bar->onStatusUpdate({}, {});
    });
}

bool TransportBar::enterPerformanceMode()
{
    if (! isPerformanceAvailable() || proc.getLiveView()->isPerformanceMode()) return false;
    if (proc.getLiveView()->getPerformance().count() == 0)
    {
        // The shell opens the chooser; a bar on its own just says so.
        if (onChoosePerformance) onChoosePerformance();
        else                     showTimedStatus("Performance", "Choose a performance first");
        return false;
    }

    // The editor's current layer seeds the live selection so nothing jumps on entry.
    mu_core::ControlAction action;
    action.type  = mu_core::ControlActionType::PerformanceMode;
    action.index = 1;
    action.layer = getEditorLayer ? getEditorLayer() : 0;
    const bool done = proc.getControlSink()->perform(action);
    syncPerformance();
    if (done) showTimedStatus("Performance", "Hold the Perf button for 1 second to leave", MuLookAndFeel::kPerfHintMs);
    return done;
}

void TransportBar::beginExitHold(double nowMs)
{
    if (! modeShown) return;
    exitHold.press(nowMs);
    ++statusSerial;   // a pending timed clear must not wipe this line
    holdLineShown = true;
    if (onStatusUpdate) onStatusUpdate("Performance", "Hold to leave Performance mode");
}

// Takes the 'Hold to leave' line down when the hold ends (released, dragged off or completed).
void TransportBar::clearHoldLine()
{
    if (! holdLineShown) return;
    holdLineShown = false;
    ++statusSerial;
    if (onStatusUpdate) onStatusUpdate({}, {});
}

void TransportBar::setExitFill(float progress)
{
    perfBtn.getProperties().set("muFill", progress);
    perfBtn.repaint();
}

void TransportBar::cancelExitHold()
{
    if (! exitHold.isHolding()) return;
    exitHold.cancel();
    setExitFill(0.0f);
    clearHoldLine();
}

// Advances the hold: the face fills with the elapsed fraction; at the end the mode is left with no confirm.
void TransportBar::tickExitHold(double nowMs)
{
    if (! exitHold.isHolding()) return;
    if (! exitHold.finished(nowMs))
    {
        setExitFill(exitHold.progress(nowMs));
        return;
    }

    exitHold.cancel();
    setExitFill(0.0f);
    clearHoldLine();
    perfBtn.suppressClick = true;   // the release that follows must not enter again
    if (isPerformanceAvailable())
    {
        mu_core::ControlAction action;
        action.type  = mu_core::ControlActionType::PerformanceMode;
        action.index = 0;
        proc.getControlSink()->perform(action);
    }
    syncPerformance();
}

void TransportBar::updatePresetGroupVisibility()
{
    const bool show = showPresetControls && ! modeShown;
    presetDropdown.setVisible(show);
    newBtn        .setVisible(show);
    saveBtn       .setVisible(show);
    stepIndicator .setVisible(showPresetControls && modeShown);
}

// Brings the toggle, the preset group and the indicator in step with the live state.
void TransportBar::syncPerformance()
{
    const bool available = isPerformanceAvailable();
    const bool on        = available && proc.getLiveView()->isPerformanceMode();
    bool relayout = false;

    if (available != availableShown)
    {
        availableShown = available;
        perfBtn.setVisible(available);
        relayout = true;
    }
    if (on != modeShown)
    {
        modeShown = on;
        if (! on) { cancelExitHold(); shownQueued = {}; shownRevision = 0xffffffffu; clearLostLine(); }
        else      { shownRevision = 0xffffffffu; }   // rebuild the indicator on entry
        perfBtn.setToggleState(on, juce::dontSendNotification);
        perfBtn.setButtonText(on ? "LIVE" : "Perf");
        perfBtn.getProperties().set("muLit", on);
        perfBtn.setTooltip(on ? "Hold to leave Performance mode" : "Performance mode (Ctrl+P)");
        updatePresetGroupVisibility();
        relayout = true;
        repaint();
    }
    if (relayout) resized();
    if (modeShown) refreshIndication();
}

// The indicator's lettering and lamps from the live state, plus the status line for a queued switch.
void TransportBar::refreshIndication()
{
    // Rebuilt only when the live state or the loaded preset's name changed: the build stats up to eight
    // preset files, which must not happen on every refresh tick.
    const auto presetName = getLoadedPresetFile().getFileNameWithoutExtension();
    if (proc.getLiveView()->getRevision() == shownRevision && presetName == shownPresetName) return;
    shownRevision   = proc.getLiveView()->getRevision();
    shownPresetName = presetName;

    const auto ind = mu_ui::perf::buildIndication(*proc.getLiveView(), presetName);
    stepIndicator.setText(ind.lit, ind.ghost, ind.ghostPulses);
    stepIndicator.setLamps(ind.lamps);
    stepIndicator.setHint(ind.hint.first, ind.hint.second);
    for (size_t i = 0; i < ind.hints.size(); ++i)
        stepIndicator.setLampHint((int) i, ind.hints[i].first, ind.hints[i].second);

    if (ind.queuedValue != shownQueued)
    {
        shownQueued = ind.queuedValue;
        ++statusSerial;   // supersedes a pending timed clear
        if (onStatusUpdate)
        {
            if (shownQueued.isNotEmpty()) onStatusUpdate("Queued", shownQueued);
            else                          onStatusUpdate({}, {});   // committed or cancelled: the line goes
        }
    }
}

void TransportBar::setLogoText(const juce::String& text)
{
    logoText = text;
    repaint();
}

void TransportBar::setShowPresetControls(bool show)
{
    showPresetControls = show;
    updatePresetGroupVisibility();   // in Performance mode the indicator takes the group's place instead
    if (!show) presetStagingBadge.setVisible(false);
    resized();
}

void TransportBar::setShowMixerToggle(bool show)
{
    showMixerToggle = show;
    mixerBtn.setVisible(show);
    resized();
}

void TransportBar::setShowTapAndNudge(bool show)
{
    showTapAndNudge = show;
    tapBtn      .setVisible(show && ! clockShown);   // the timer keeps this in step with the clock lamp
    nudgeSlowBtn.setVisible(show);
    nudgeFastBtn.setVisible(show);
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

// The controller's status on the status line (docs/design-ui-family.md section 14). Lost is sticky because the
// PC is locked on stage and this is the only warning; it goes on reconnect, or when the mode ends and the
// manager lets go of the dead link.
void TransportBar::controllerStatusChanged()
{
    using S = mu_core::ControlStatus;
    auto* attached = controllerWatch.get();
    const S status = attached != nullptr ? attached->getStatus() : S::NotFound;
    const S before = shownControl;
    shownControl = status;

    if (attached != nullptr && status == S::Lost)
    {
        if (controlLostShown) return;
        controlLostShown = true;
        ++statusSerial;   // supersedes a pending timed clear
        if (onStatusUpdate) onStatusUpdate("Launchpad", "Lost: unplugged or in use elsewhere");
        return;
    }

    clearLostLine();
    if (attached == nullptr || status == before) return;

    if (status == S::Released)
        showTimedStatus("Launchpad", "Released");
    else if ((status == S::Found || status == S::InUse) && (before == S::NotFound || before == S::Released || before == S::Lost))
        showTimedStatus("Launchpad", "Connected");
}

void TransportBar::clearLostLine()
{
    if (! controlLostShown) return;
    controlLostShown = false;
    ++statusSerial;
    if (onStatusUpdate) onStatusUpdate({}, {});
}

void TransportBar::syncController()
{
    if (controllerWatch.update(proc.getControlAttachment()))
    {
        // Newly attached (or gone): take its state as the baseline, announcing nothing.
        shownControl = controllerWatch.get() != nullptr ? controllerWatch.get()->getStatus() : mu_core::ControlStatus::NotFound;
        if (shownControl != mu_core::ControlStatus::Lost) clearLostLine();
    }
}

void TransportBar::timerCallback()
{
    syncController();
    refreshPlayBtn();
    updatePositionLabel();

    if (isStandalone)
    {
        const bool midiClockBpm = proc.getMidiSyncEnabled() && proc.getMidiSyncMessages() != 1;
        if (midiClockBpm && proc.getMidiClockBpm() > 0.0)   // 0 = no clock heard yet: keep the field's value
        {
            // Display only (the sync itself uses the raw estimate): a real change snaps, small wander
            // inside the dead band is ignored, and larger drift is eased in, so the number holds still.
            const double est = proc.getMidiClockBpm();
            if (shownClockBpm <= 0.0 || std::abs(est - shownClockBpm) > 3.0) shownClockBpm = est;
            else if (std::abs(est - shownClockBpm) > 0.25)                  shownClockBpm += 0.15 * (est - shownClockBpm);
            bpmInput.setValueD(shownClockBpm);
        }
        else shownClockBpm = 0.0;
        bpmInput.setEnabled(!midiClockBpm);

        const bool midiTransport = proc.getMidiSyncEnabled() && proc.getMidiSyncMessages() != 0;
        playBtn.setEnabled(!midiTransport);

        // The shared slot: the clock lamp while the clock's ticks drive the transport, else Tap.
        clockShown = midiClockBpm;
        clockLamp.setVisible(clockShown);
        tapBtn.setVisible(showTapAndNudge && ! clockShown);
        if (clockShown) refreshClockLamp();
    }

    syncPerformance();
    if (exitHold.isHolding()) tickExitHold(juce::Time::getMillisecondCounterHiRes());

    // Show the staging badge while a full-preset hot-swap is queued for the loop point.
    if (showPresetControls)
        presetStagingBadge.setVisible(proc.hasPendingFullPreset());
}

void TransportBar::updateNudge()
{
    proc.setNudgeDirection((nudgeFastBtn.isDown() ? 1 : 0) - (nudgeSlowBtn.isDown() ? 1 : 0));   // both = no bend
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
        case State::Drifting:
            clockLamp.setState(MuLookAndFeel::colour(Id::segmentWarningBorder), L.lampOn);   // amber, brighter than Waiting
            status = "Drifting, re-aligning";
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

    // Where the Performance chooser is reached from: the same entry in every product's dropdown.
    if (isPerformanceAvailable() && onChoosePerformance)
    {
        presetDropdown.addSectionHeading("Performance");
        presetDropdown.addItem("Performances...", kPerformancesItemId);
    }
}

void TransportBar::refreshPresets()
{
    populatePresetDropdown();
}

void TransportBar::refreshTempo()
{
    if (isStandalone)
        bpmInput.setValueD(proc.getInternalBpm());
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

    // Performance mode: the lit Perf button's glow, behind the button and clipped to the bar.
    if (modeShown && perfBtn.isVisible())
        MuLookAndFeel::drawLitGlow(g, perfBtn.getBounds(), MuLookAndFeel::appAccent(*this), mu_ui::s(MuLookAndFeel::kPerfGlowR));
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
        tapBtn   .setBounds(x, btnY, s(kClockW), btnH);   // Tap and the lamp share the slot
        clockLamp.setBounds(x, btnY, s(kClockW), btnH);
        x += s(kClockW) + gap;
    }

    posLabel.setBounds(x, btnY, s(kPosW), btnH);
    x += s(kPosW);
    if (isStandalone && showTapAndNudge)
    {
        x += gap;
        nudgeSlowBtn.setBounds(x, btnY, s(kNudgeW), btnH);
        x += s(kNudgeW) + s(kNudgeGap);
        nudgeFastBtn.setBounds(x, btnY, s(kNudgeW), btnH);
        x += s(kNudgeW);
    }
    x += padIn;

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

    if (availableShown)   // Performance mode's toggle, left of Mixer
    {
        perfBtn.setBounds(rightEdge - s(MuLookAndFeel::kPerfBtnW), btnY, s(MuLookAndFeel::kPerfBtnW), btnH);
        rightEdge -= s(MuLookAndFeel::kPerfBtnW) + gap;
    }

    if (showSettingsButton)
    {
        gearBtn.setBounds(rightEdge - s(kGearW), btnY, s(kGearW), btnH);
        rightEdge -= s(kGearW) + gap;
    }

    if (showPresetControls)
    {
        const int groupRight = rightEdge;   // the preset group's right edge, where the indicator ends too
        saveBtn.setBounds(rightEdge - s(kSaveW), btnY, s(kSaveW), btnH);
        rightEdge -= s(kSaveW) + gap;

        newBtn.setBounds(rightEdge - s(kNewW), btnY, s(kNewW), btnH);
        rightEdge -= s(kNewW) + gap;

        const int presetLeft = (loopPaneBounds.isEmpty() ? transportPaneBounds.getRight()
                                                          : loopPaneBounds.getRight()) + gap;
        presetDropdown.setBounds(presetLeft, btnY, rightEdge - presetLeft, btnH);
        stepIndicator.setBounds(presetLeft, btnY, groupRight - presetLeft, btnH);

        // Staging badge: small pill at the top-right of the preset dropdown (the indicator in Performance mode).
        const int badgeW = s(28);
        const int badgeH = s(11);
        const auto& host = modeShown ? static_cast<juce::Component&>(stepIndicator) : presetDropdown;
        presetStagingBadge.setBounds(host.getRight() - badgeW - s(4), host.getY() + s(2), badgeW, badgeH);
    }
}
