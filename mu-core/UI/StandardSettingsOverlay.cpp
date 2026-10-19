#include "StandardSettingsOverlay.h"

namespace mu_ui
{

StandardSettingsOverlay::StandardSettingsOverlay(mu_core::ShellBackend& backend, Options options)
    : proc(backend),
      isStandalone(backend.isStandaloneApp()),
      opts(options)
{
    // ── Audio — master volume, only when the backend has a master level. The backend reads and
    //    writes the level itself: a SliderAttachment would replace the dB readout with the
    //    parameter's raw formatter. Midpoint skew at 0.5 (-6 dB) gives fader-like resolution.
    if (proc.hasMasterLevel())
    {
        masterVolKnob.setRange(0.0, 1.0, 0.001);
        masterVolKnob.getSlider().setSkewFactorFromMidPoint(0.5);
        masterVolKnob.getSlider().textFromValueFunction = [](double v) -> juce::String {
            if (v <= 0.0) return "-inf dB";
            return juce::String(20.0 * std::log10(v), 1) + " dB";
        };
        masterVolKnob.setValue(proc.getMasterLevel(), juce::dontSendNotification);
        masterVolKnob.onValueChanged = [this](double v) { proc.setMasterLevel((float) v); };
        addAndMakeVisible(masterVolKnob);
        general.push_back({ "Audio", 64, [this](const Rows& r) {
            masterVolKnob.setBounds(r.labelX, r.area.getY(),
                                    s(MuLookAndFeel::kKnobSize2W), s(MuLookAndFeel::kKnobSize2H)); } });
    }

    // ── Display — UI size.
    makeFieldLabel(uiSizeLabel, "Size");
    uiSizeCtrl.setSelectedIndex(proc.getUiScale() >= mu_core::ShellBackend::kUiScaleLarge ? 1 : 0, false);
    uiSizeCtrl.onChange = [this](int idx)
    { proc.setUiScale(idx == 1 ? mu_core::ShellBackend::kUiScaleLarge : mu_core::ShellBackend::kUiScaleMedium); };
    addAndMakeVisible(uiSizeCtrl);
    // The window resizes live, but text labels pick their font size when created — so a
    // hint below the picker says the app or plugin needs reopening for those to fully rescale.
    const juce::String rescaleHint = isStandalone ? "(reopen the app for label fonts to fully rescale)"
                                                  : "(reopen the plugin for label fonts to fully rescale)";
    general.push_back({ "Display", 2 * kRowH + kRowGap, [this](const Rows& r) {
        uiSizeLabel.setBounds(r.labelX, r.area.getY(), r.labelW, r.rowH);
        uiSizeCtrl .setBounds(r.ctrlX,  r.area.getY(), r.ctrlW,  r.rowH); },
        [this, rescaleHint](juce::Graphics& g, const Rows& r) {
        drawHint(g, r.area.getY() + r.rowH + s(kRowGap) / 2 + r.rowH / 2,
                 rescaleHint,
                 r.ctrlX, r.area.getRight() - r.ctrlX); } });

    // ── Transport — internal free-running BPM (where a product shows it here) and, in the
    //    standalone, the meter the bar.beat display counts in. Pattern lengths stay four beats a bar.
    const bool showMeter = isStandalone && options.showMeter;
    if (options.showTransport || showMeter)
    {
        if (options.showTransport)
        {
            makeFieldLabel(bpmLabel, "Tempo");
            bpmInput.setDecimals(MuLookAndFeel::kBpmDecimals);
            bpmInput.setStep(MuLookAndFeel::kBpmStep);
            bpmInput.setFineStep(MuLookAndFeel::kBpmFineStep);
            bpmInput.setValueD(proc.getInternalBpm());
            bpmInput.onChangeD = [this](double v) { proc.setInternalBpm(v); };
            addAndMakeVisible(bpmInput);
        }
        if (showMeter)
        {
            makeFieldLabel(timeSigLabel, "Time signature");
            static const std::pair<int, int> kMeters[] = { {2,4}, {3,4}, {4,4}, {5,4}, {6,4}, {7,4}, {5,8}, {6,8}, {7,8}, {9,8}, {12,8} };
            int current = 3, num = 4, den = 4;
            proc.getTimeSignature(num, den);
            for (int i = 0; i < (int) std::size(kMeters); ++i)
            {
                timeSigDropdown.addItem(juce::String(kMeters[i].first) + "/" + juce::String(kMeters[i].second), i + 1);
                if (kMeters[i].first == num && kMeters[i].second == den) current = i + 1;
            }
            timeSigDropdown.setSelectedId(current, false);
            timeSigDropdown.onChange = [this](int id)
            {
                if (id >= 1 && id <= (int) std::size(kMeters))
                    proc.setTimeSignature(kMeters[id - 1].first, kMeters[id - 1].second);
            };
            addAndMakeVisible(timeSigDropdown);
        }
        const bool tempo = options.showTransport;
        general.push_back({ "Transport", (tempo && showMeter) ? 2 * kRowH + kRowGap : kRowH, [this, tempo, showMeter](const Rows& r) {
            if (tempo)
            {
                bpmLabel.setBounds(r.labelX, r.area.getY(), r.labelW, r.rowH);
                bpmInput.setBounds(r.ctrlX,  r.area.getY(), s(90),    r.rowH);
            }
            if (showMeter)
            {
                const int y = r.area.getY() + (tempo ? r.rowH + s(kRowGap) : 0);
                timeSigLabel   .setBounds(r.labelX, y, r.labelW, r.rowH);
                timeSigDropdown.setBounds(r.ctrlX,  y, s(90),    r.rowH);
            } } });
    }

    // ── Update check — whether the app asks the website, once a day, for a newer version. A plain
    //    request carrying nothing about the user; Off sends nothing and hides the status-bar link.
    //    Shown only where the backend has a settings file and a product slug.
    if (proc.getAppSettingsFile() != nullptr && proc.getProductSlug().isNotEmpty())
    {
        makeFieldLabel(updateCheckLabel, "Update check");
        updateCheckCtrl.setSelectedIndex(mu_core::update_check::isEnabled(proc.getAppSettingsFile()) ? 0 : 1, false);
        updateCheckCtrl.onChange = [this](int idx)
        {
            mu_core::update_check::setEnabled(proc.getAppSettingsFile(), idx == 0);
            if (proc.onUpdateCheckToggled) proc.onUpdateCheckToggled();
        };
        addAndMakeVisible(updateCheckCtrl);
        general.push_back({ "Updates", 2 * kRowH + kRowGap, [this](const Rows& r) {
            updateCheckLabel.setBounds(r.labelX, r.area.getY(), r.labelW, r.rowH);
            updateCheckCtrl .setBounds(r.ctrlX,  r.area.getY(), r.ctrlW,  r.rowH); },
            [this](juce::Graphics& g, const Rows& r) {
            drawHint(g, r.area.getY() + r.rowH + s(kRowGap) / 2 + r.rowH / 2,
                     "Checks once a day for a newer version. Nothing about you is sent.",
                     r.ctrlX, r.area.getRight() - r.ctrlX); } });
    }

    // ── MIDI Clock (standalone only) — slave the beat / tempo to external MIDI clock. Without the
    //    Source and Messages rows (showClockSource off) only the Sync offset row remains.
    if (isStandalone && (opts.showClockSource || opts.syncOffsetAlways))
    {
        if (opts.showClockSource)
        {
            makeFieldLabel(clockSourceLabel, "Source");
            clockSourceDropdown.addItem("Internal", 1);
            clockSourceDropdown.addItem("MIDI In",  2);
            clockSourceDropdown.setSelectedId(proc.getMidiSyncEnabled() ? 2 : 1, false);
            clockSourceDropdown.onChange = [this](int id) { proc.setMidiSyncEnabled(id == 2); updateMidiSyncVisibility(); };
            addAndMakeVisible(clockSourceDropdown);

            makeFieldLabel(midiMessagesLabel, "Messages");
            midiMessagesDropdown.addItem("Clock only",        1);
            midiMessagesDropdown.addItem("Transport only",    2);
            midiMessagesDropdown.addItem("Clock + Transport", 3);
            midiMessagesDropdown.setSelectedId(proc.getMidiSyncMessages() + 1, false);
            midiMessagesDropdown.onChange = [this](int id) { proc.setMidiSyncMessages(id - 1); };
            addAndMakeVisible(midiMessagesDropdown);
        }

        // Sync offset: positive = the app plays earlier than the clock says (enter the audio output
        // latency, about 20-40 ms on WASAPI). Arrows step 1 ms, Shift+click 10.
        makeFieldLabel(syncOffsetLabel, "Sync offset");
        syncOffsetInput.setShowSign(true);
        syncOffsetInput.setStep(1.0);
        syncOffsetInput.setFineStep(10.0);
        syncOffsetInput.setValue(proc.getSyncOffsetMs());
        syncOffsetInput.onChange = [this](int ms) { proc.setSyncOffsetMs(ms); };
        addAndMakeVisible(syncOffsetInput);
        updateMidiSyncVisibility();

        const bool sourceRows = opts.showClockSource;
        midiClock.push_back({ "MIDI Clock", sourceRows ? 3 * kRowH + 2 * kRowGap : kRowH, [this, sourceRows](const Rows& r) {
            const int y2 = r.area.getY() + r.rowH + s(kRowGap);
            const int y3 = sourceRows ? y2 + r.rowH + s(kRowGap) : r.area.getY();
            if (sourceRows)
            {
                clockSourceLabel    .setBounds(r.labelX, r.area.getY(), r.labelW, r.rowH);
                clockSourceDropdown .setBounds(r.ctrlX,  r.area.getY(), r.ctrlW,  r.rowH);
                midiMessagesLabel   .setBounds(r.labelX, y2,            r.labelW, r.rowH);
                midiMessagesDropdown.setBounds(r.ctrlX,  y2,            r.ctrlW,  r.rowH);
            }
            syncOffsetLabel     .setBounds(r.labelX, y3,            r.labelW, r.rowH);
            syncOffsetInput     .setBounds(r.ctrlX,  y3,            s(90),    r.rowH); } });
    }

    // ── Controller — a control surface the standalone shell attached (appears once it does).
    if (isStandalone) setUpControllerGroup();
}

StandardSettingsOverlay::ControllerLook StandardSettingsOverlay::controllerLook(mu_core::ControlStatus status)
{
    using Id = MuLookAndFeel::ColourIds;
    using S  = mu_core::ControlStatus;
    const auto& L = MuLookAndFeel::lighting();
    const float bright = juce::jmin(1.0f, L.lampOn + L.lampPlayhead);
    const auto text = mu_core::controlStatusText(status);

    switch (status)
    {
        case S::Found:    return { MuLookAndFeel::colour(Id::segmentPositiveBorder), L.lampOn, text, "Release" };
        case S::InUse:    return { MuLookAndFeel::colour(Id::segmentPositiveBorder), bright,   text, "Release" };
        case S::Released: return { MuLookAndFeel::colour(Id::segmentPositiveBorder), L.lampOff, text, "Connect" };   // unlit: the bare lamp base
        case S::Lost:     return { MuLookAndFeel::colour(Id::indicatorFault),        bright,   text, "Connect" };
        case S::NotFound:
        default:          return { MuLookAndFeel::colour(Id::segmentWarningBorder),  L.lampDim, text, "Connect" };
    }
}

void StandardSettingsOverlay::setUpControllerGroup()
{
    makeFieldLabel(controllerLampLabel, "Launchpad");
    makeFieldLabel(controllerButtonLabel, "Controller");
    addChildComponent(controllerLamp);
    addChildComponent(controllerBtn);
    controllerLampLabel.setVisible(false);
    controllerButtonLabel.setVisible(false);

    controllerLamp.onStatusUpdate = [this](const juce::String& n, const juce::String& v) { if (onStatusUpdate) onStatusUpdate(n, v); };
    controllerBtn.onClick = [this]
    {
        auto* a = controllerWatch.get();
        if (a == nullptr) return;
        const auto st = a->getStatus();
        if (st == mu_core::ControlStatus::Found || st == mu_core::ControlStatus::InUse) a->release();
        else                                                                            a->connect();
    };
    controllerWatch.onChange = [this] { refreshController(); };

    addSection(Where::Group, { "", 2 * kRowH + kRowGap, [this](const Rows& r) {
        const int y2 = r.area.getY() + r.rowH + s(kRowGap);
        controllerLampLabel  .setBounds(r.labelX, r.area.getY(), r.labelW, r.rowH);
        controllerLamp       .setBounds(r.ctrlX,  r.area.getY(), r.ctrlW,  r.rowH);
        controllerButtonLabel.setBounds(r.labelX, y2,            r.labelW, r.rowH);
        controllerBtn        .setBounds(r.ctrlX,  y2,            juce::jmin(r.ctrlW, s(MuLookAndFeel::kDropdownW)), r.rowH); },
        nullptr, [this] { return controllerShown(); } }, "Controller");
    refreshController();
}

// Follows the shell's attachment and brings the lamp, the text and the button in step with its status.
void StandardSettingsOverlay::refreshController()
{
    const bool wasShown = controllerShown();
    controllerWatch.update(proc.getControlAttachment());
    const bool show = controllerShown();

    for (auto* c : std::initializer_list<juce::Component*> { &controllerLampLabel, &controllerButtonLabel, &controllerLamp, &controllerBtn })
        c->setVisible(show);

    if (show)
    {
        const auto look = controllerLook(controllerWatch.get()->getStatus());
        const auto device = controllerWatch.get()->getDeviceLabel();
        controllerLamp.setState(look.colour, look.lit);
        controllerLamp.setLabel(look.text);
        controllerLamp.setStatus("Launchpad", look.text == "Not found" ? juce::String("Not found: check the USB cable")
                                                                        : (device.isNotEmpty() ? device + ": " : juce::String()) + look.text);
        controllerBtn.setButtonText(look.button);
    }

    // The group appears or goes as a whole, so the page is laid out again.
    if (show != wasShown && getWidth() > 0 && getHeight() > 0) resized();
    repaint();
}

void StandardSettingsOverlay::visibilityChanged()
{
    if (isVisible()) refreshController();
}

void StandardSettingsOverlay::makeFieldLabel(juce::Label& lbl, const juce::String& text)
{
    lbl.setText(text, juce::dontSendNotification);
    lbl.setFont(juce::Font(juce::FontOptions{}.withHeight(sf(12.0f))));
    lbl.setColour(juce::Label::textColourId, MuLookAndFeel::colour(MuLookAndFeel::labelText));
    lbl.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(lbl);
}

void StandardSettingsOverlay::addProgramChangeSection(const juce::String& layerTableName,
                                                      const juce::String& fullTableName)
{
    midiPresetsBtn.setButtonText(layerTableName);
    fullPresetsBtn.setButtonText(fullTableName);
    midiPresetsBtn.onClick = [this] { if (onMidiPresetsClicked) onMidiPresetsClicked(); };
    fullPresetsBtn.onClick = [this] { if (onFullPresetsClicked) onFullPresetsClicked(); };
    addAndMakeVisible(midiPresetsBtn);
    addAndMakeVisible(fullPresetsBtn);
    addSection(Where::MidiAfterClock, { "MIDI Program Change", kRowH, [this](const Rows& r) {
        const int btnW = s(180), gap = s(MuLookAndFeel::kSpaceM);
        midiPresetsBtn.setBounds(r.labelX,              r.area.getY(), btnW, r.rowH);
        fullPresetsBtn.setBounds(r.labelX + btnW + gap, r.area.getY(), btnW, r.rowH); } });
}

void StandardSettingsOverlay::addSection(Where where, Section section, const juce::String& group)
{
    switch (where)
    {
        case Where::General:         general.push_back(std::move(section));    break;
        case Where::MidiBeforeClock: midiBefore.push_back(std::move(section)); break;
        case Where::MidiAfterClock:  midiAfter.push_back(std::move(section));  break;
        case Where::Group:           extraGroups.push_back({ group, std::move(section) }); break;
    }
}

void StandardSettingsOverlay::updateMidiSyncVisibility()
{
    // The Messages and Sync offset rows only matter when MIDI-clock input is selected; Sync offset
    // can be kept on always, for a backend whose clock is not chosen on this page.
    const bool on = proc.getMidiSyncEnabled();
    const bool messages = on && opts.showClockSource;
    const bool offset = on || opts.syncOffsetAlways;
    midiMessagesLabel   .setVisible(messages);
    midiMessagesDropdown.setVisible(messages);
    syncOffsetLabel     .setVisible(offset);
    syncOffsetInput     .setVisible(offset);
}

void StandardSettingsOverlay::layoutContent()
{
    headings.clear();
    placed.clear();

    // Per-column running y; each group goes in the column that is currently shorter.
    int colY[2] = { contentTop(), contentTop() };
    bool colUsed[2] = { false, false };
    const int nCols = columnCount();

    // Stack one group into a column: its heading (if named), then each section — heading + rows —
    // with the standard gaps between sections and between groups.
    auto emitGroup = [&](const juce::String& name, const std::vector<const Section*>& sections)
    {
        if (sections.empty()) return;
        const int c = (nCols == 2 && colY[1] < colY[0]) ? 1 : 0;
        const int cx = columnX(c), cw = columnW();
        int& y = colY[c];
        if (colUsed[c]) y += s(kGroupGap);
        colUsed[c] = true;
        if (name.isNotEmpty()) { headings.push_back({ cx, cw, y, name, true }); y += s(kGroupHeadH); }

        const Rows base { {}, cx + s(kRowIndent), cx + s(kRowIndent) + s(kLabelW) + s(kLabelCtrlGap),
                          s(kLabelW), juce::jmin(s(kControlW), cw - s(kRowIndent) - s(kLabelW) - s(kLabelCtrlGap)),
                          s(kRowH) };
        for (size_t i = 0; i < sections.size(); ++i)
        {
            const auto* sec = sections[i];
            if (i > 0) y += s(kSectionGap);
            if (sec->title.isNotEmpty()) { headings.push_back({ cx, cw, y, sec->title, false }); y += s(kSectionHeadH); }
            Rows rows = base;
            rows.area = { cx, y, cw, s(sec->heightPx) };
            if (sec->place) sec->place(rows);
            placed.push_back({ sec, rows });
            y += s(sec->heightPx);
        }
    };
    auto ptrs = [](const std::vector<Section>& v)
    {
        std::vector<const Section*> out;
        for (const auto& sct : v) if (! sct.shown || sct.shown()) out.push_back(&sct);
        return out;
    };

    emitGroup("General", ptrs(general));

    std::vector<const Section*> midi = ptrs(midiBefore);
    for (const auto* sct : ptrs(midiClock)) midi.push_back(sct);
    for (const auto* sct : ptrs(midiAfter)) midi.push_back(sct);
    emitGroup("MIDI", midi);

    // Extra groups, in the order each group name first appeared.
    juce::StringArray order;
    for (const auto& gs : extraGroups) if (! order.contains(gs.group)) order.add(gs.group);
    for (const auto& name : order)
    {
        std::vector<const Section*> secs;
        for (const auto& gs : extraGroups)
            if (gs.group == name && (! gs.section.shown || gs.section.shown())) secs.push_back(&gs.section);
        emitGroup(name, secs);
    }

    // Everything must fit the overlay without scrolling (design-ui-family §5).
    jassert(juce::jmax(colY[0], colY[1]) <= getHeight() - s(kPad) - s(20));
}

void StandardSettingsOverlay::paintContent(juce::Graphics& g)
{
    for (const auto& h : headings)
    {
        if (h.group) drawGroupHeader  (g, h.y, h.title, h.x, h.w);
        else         drawSectionHeader(g, h.y, h.title, h.x, h.w);
    }
    for (const auto& [sec, rows] : placed)
        if (sec->paint) sec->paint(g, rows);
}

} // namespace mu_ui
