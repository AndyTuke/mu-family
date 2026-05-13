#include "MixerOverlay.h"
#include "../FX/FXAlgorithmDef.h"
#include "../FX/EffectSlot.h"

MixerOverlay::MixerOverlay(PluginProcessor& p, MixerEngine& m)
    : proc(p), mixer(m),
      effectRow("Effect", FXAlgorithmRegistry::effectAlgorithms(), MuClidLookAndFeel::knobFxSend),
      reverbRow("Reverb", FXAlgorithmRegistry::reverbAlgorithms(), MuClidLookAndFeel::knobReverb)
{
    buildRhythmChannels();
    wireReturns();
    wireFXRows();

    addAndMakeVisible(effectReturn);
    addAndMakeVisible(delayReturn);
    addAndMakeVisible(reverbReturn);
    addAndMakeVisible(masterChannel);
    addAndMakeVisible(effectRow);
    addChildComponent(echoRow);   // hidden by default; shown when effect algo == Echo
    addAndMakeVisible(delayRow);
    addAndMakeVisible(reverbRow);

    addAndMakeVisible(meterModeCtrl);
    meterModeCtrl.onChange = [this](int idx)
    {
        propagateMeterMode(static_cast<VUMeter::MeterMode>(idx));
    };

    // Register for every APVTS parameter so automation syncs the mixer UI.
    for (auto* param : proc.getParameters())
        if (auto* pid = dynamic_cast<juce::AudioProcessorParameterWithID*>(param))
            proc.apvts.addParameterListener(pid->getParameterID(), this);

    startTimerHz(30);
}

MixerOverlay::~MixerOverlay()
{
    stopTimer();
    for (auto* param : proc.getParameters())
        if (auto* pid = dynamic_cast<juce::AudioProcessorParameterWithID*>(param))
            proc.apvts.removeParameterListener(pid->getParameterID(), this);
}

void MixerOverlay::parameterChanged(const juce::String& /*parameterID*/, float /*newValue*/)
{
    apvtsDirty = true;
}

void MixerOverlay::visibilityChanged()
{
    if (isVisible() && apvtsDirty)
    {
        loadFromAPVTS();
        apvtsDirty = false;
    }
}

void MixerOverlay::timerCallback()
{
    if (apvtsDirty && isVisible())
    {
        loadFromAPVTS();
        apvtsDirty = false;
    }
}

void MixerOverlay::buildRhythmChannels()
{
    for (auto& ch : rhythmChannels)
        removeChildComponent(ch.get());
    rhythmChannels.clear();

    const auto& palette = MuClidLookAndFeel::rhythmPalette;
    const int numActive = proc.getNumRhythms();
    for (int r = 0; r < MixerEngine::MaxChannels; ++r)
    {
        bool hasRhythm = r < numActive;
        juce::Colour col = hasRhythm ? palette[proc.getRhythm(r).colourIndex % 30]
                                     : juce::Colour(0xff404040);
        juce::String name = hasRhythm ? juce::String(proc.getRhythm(r).name) : "-";
        auto ch = std::make_unique<MixerChannel>(MixerChannel::Type::Rhythm, name, col);
        const juce::String prefix = "ch" + juce::String(r) + "_";
        ch->bindRhythm(mixer.channels[r], mixer.channelPeaks[r], &proc, prefix,
                       &mixer.sidechainGR[r]);
        if (!hasRhythm) ch->setActive(false);
        addAndMakeVisible(*ch);
        rhythmChannels.push_back(std::move(ch));
    }
    refreshSidechainSources();
}

void MixerOverlay::refreshSidechainSources()
{
    juce::StringArray names;
    const int numActive = proc.getNumRhythms();
    for (int r = 0; r < MixerEngine::MaxChannels; ++r)
        names.add((r < numActive) ? juce::String(proc.getRhythm(r).name) : juce::String());
    for (int r = 0; r < (int)rhythmChannels.size(); ++r)
        rhythmChannels[r]->setSidechainSources(r, names);
}

void MixerOverlay::wireReturns()
{
    effectReturn.bindReturn(mixer.returns[0], mixer.returnPeaks[0], &proc, "ret_eff_");
    effectReturn.bindReturnSends(proc.apvts, "eff2dly", "eff2rev");

    delayReturn .bindReturn(mixer.returns[1], mixer.returnPeaks[1], &proc, "ret_dly_");
    delayReturn .bindReturnSends(proc.apvts, "", "dly2rev");

    reverbReturn.bindReturn(mixer.returns[2], mixer.returnPeaks[2], &proc, "ret_rev_");
    masterChannel.bindMaster(mixer, &proc);
}

void MixerOverlay::wireFXRows()
{
    auto& eff = proc.fxChain.effectSlot();
    auto& dly = proc.fxChain.delaySlot();
    auto& rev = proc.fxChain.reverbSlot();

    // ── Effect row ───────────────────────────────────────────────────────────
    // Issue #44: Effect is a send/return path — the dry signal already lives in the
    // main bus, so the algorithm's wet/dry mix knob would double the dry component.
    // Hide the mix knob; EffectSlot forces the algorithm into sendMode (wet-only).
    effectRow.hideParameter("mix");
    effectRow.setEnabled(eff.isEnabled());
    effectRow.setSelectedAlgorithm(eff.getAlgorithmIndex());

    effectRow.onEnabledChanged = [this](bool e) {
        if (auto* p = proc.apvts.getParameter("eff_en"))
            p->setValueNotifyingHost(e ? 1.0f : 0.0f);
    };
    effectRow.onAlgorithmChanged = [this](int idx) {
        // #242b: force-sync the engine BEFORE writing APVTS. If APVTS already
        // holds the same value (e.g. state restore left it at idx while the
        // engine sat at default 0), setValueNotifyingHost skips the listener,
        // so syncFXParam never fires and the engine stays stale.
        proc.fxChain.effectSlot().setAlgorithm(idx);
        if (auto* p = proc.apvts.getParameter("eff_algo"))
            p->setValueNotifyingHost(p->convertTo0to1((float)idx));
        updateEffectSendLabels();
        const bool isEcho = (idx == EffectSlot::kEchoAlgoIndex);
        effectRow.setVisible(!isEcho);
        echoRow  .setVisible(isEcho);
        resized();
    };
    effectRow.onParamChanged = [this, &eff](const juce::String& id, float v) {
        const auto& algos = FXAlgorithmRegistry::effectAlgorithms();
        int ai = eff.getAlgorithmIndex();
        if (ai >= (int)algos.size()) return;
        const auto& params = algos[ai].params;
        for (int i = 0; i < (int)params.size() && i < 5; ++i)
        {
            if (params[i].id == id)
            {
                const auto& pd = params[i];
                float norm = (pd.maxVal > pd.minVal)
                             ? juce::jlimit(0.0f, 1.0f, (v - pd.minVal) / (pd.maxVal - pd.minVal))
                             : 0.0f;
                if (auto* p = proc.apvts.getParameter("eff_p" + juce::String(i)))
                    p->setValueNotifyingHost(norm);
                return;
            }
        }
    };

    // Load current algo param values into FX row knobs.
    {
        int ai = eff.getAlgorithmIndex();
        const auto& algos = FXAlgorithmRegistry::effectAlgorithms();
        if (ai < (int)algos.size())
        {
            const auto& params = algos[ai].params;
            for (int i = 0; i < (int)params.size() && i < 5; ++i)
            {
                float norm = *proc.apvts.getRawParameterValue("eff_p" + juce::String(i));
                float actual = params[i].minVal + norm * (params[i].maxVal - params[i].minVal);
                effectRow.setParamValue(params[i].id, actual);
            }
        }
    }

    // ── Delay row ────────────────────────────────────────────────────────────
    delayRow.setEnabled(dly.isEnabled());
    delayRow.setSyncMode(dly.getTimeMode() == DelaySlot::TimeMode::Sync);

    delayRow.onEnabledChanged = [this](bool e) {
        if (auto* p = proc.apvts.getParameter("dly_en"))
            p->setValueNotifyingHost(e ? 1.0f : 0.0f);
    };
    delayRow.onSyncChanged = [this](bool sync) {
        if (auto* p = proc.apvts.getParameter("dly_mode"))
            p->setValueNotifyingHost(sync ? 1.0f : 0.0f);
    };
    delayRow.onSyncParamChanged = [this](int denom, bool dotted, bool triplet, int count) {
        // Map denominator value to index: 32→0, 16→1, 8→2, 4→3
        static const int denoms[] = { 32, 16, 8, 4 };
        int idx = 3;
        for (int i = 0; i < 4; ++i)
            if (denoms[i] == denom) { idx = i; break; }
        if (auto* p = proc.apvts.getParameter("dly_syncDenom"))
            p->setValueNotifyingHost(p->convertTo0to1((float)idx));
        if (auto* p = proc.apvts.getParameter("dly_syncDot"))
            p->setValueNotifyingHost(dotted ? 1.0f : 0.0f);
        if (auto* p = proc.apvts.getParameter("dly_syncTrip"))
            p->setValueNotifyingHost(triplet ? 1.0f : 0.0f);
        if (auto* p = proc.apvts.getParameter("dly_count"))
            p->setValueNotifyingHost(p->convertTo0to1((float)count));
    };
    delayRow.onFreeMsChanged = [this](float ms) {
        if (auto* p = proc.apvts.getParameter("dly_ms"))
            p->setValueNotifyingHost(p->convertTo0to1(ms));
    };
    delayRow.onFeedbackChanged = [this](float v) {
        if (auto* p = proc.apvts.getParameter("dly_fb"))
            p->setValueNotifyingHost(p->convertTo0to1(v));
    };
    delayRow.onSpreadChanged = [this](float v) {
        if (auto* p = proc.apvts.getParameter("dly_spread"))
            p->setValueNotifyingHost(p->convertTo0to1(v));
    };
    delayRow.onDirtChanged = [this](float v) {
        if (auto* p = proc.apvts.getParameter("dly_dirt"))
            p->setValueNotifyingHost(p->convertTo0to1(v));
    };

    // ── Reverb row ───────────────────────────────────────────────────────────
    reverbRow.setEnabled(rev.isEnabled());
    reverbRow.setSelectedAlgorithm(rev.getAlgorithmIndex());

    reverbRow.onEnabledChanged = [this](bool e) {
        if (auto* p = proc.apvts.getParameter("rev_en"))
            p->setValueNotifyingHost(e ? 1.0f : 0.0f);
    };
    reverbRow.onAlgorithmChanged = [this](int idx) {
        if (auto* p = proc.apvts.getParameter("rev_algo"))
            p->setValueNotifyingHost(p->convertTo0to1((float)idx));
    };

    // Reverb params map directly to APVTS (matching ranges, no normalization needed).
    static const std::pair<const char*, const char*> revParamMap[] = {
        { "size",      "rev_size" },
        { "predelay",  "rev_pre"  },
        { "diffusion", "rev_diff" },
        { "damp",      "rev_damp" },
        { "mod",       "rev_mod"  },
        { "dirt",      "rev_dirt" },
    };
    reverbRow.onParamChanged = [this](const juce::String& id, float v) {
        for (auto& [pid, apid] : revParamMap)
        {
            if (id == pid)
            {
                if (auto* p = proc.apvts.getParameter(apid))
                    p->setValueNotifyingHost(p->convertTo0to1(v));
                return;
            }
        }
    };

    // ── Echo row (shown when Effect algo == Echo) ─────────────────────────────
    auto& echo = proc.fxChain.effectSlot().getEchoDelay();

    echoRow.setEnabled(echo.isEnabled());
    echoRow.setSyncMode(echo.getTimeMode() == DelaySlot::TimeMode::Sync);

    echoRow.onEnabledChanged = [this](bool e) {
        if (auto* p = proc.apvts.getParameter("echo_en"))
            p->setValueNotifyingHost(e ? 1.0f : 0.0f);
    };
    echoRow.onSyncChanged = [this](bool sync) {
        if (auto* p = proc.apvts.getParameter("echo_mode"))
            p->setValueNotifyingHost(sync ? 1.0f : 0.0f);
    };
    echoRow.onSyncParamChanged = [this](int denom, bool dotted, bool triplet, int count) {
        static const int denoms[] = { 32, 16, 8, 4 };
        int idx = 3;
        for (int i = 0; i < 4; ++i)
            if (denoms[i] == denom) { idx = i; break; }
        if (auto* p = proc.apvts.getParameter("echo_syncDenom"))
            p->setValueNotifyingHost(p->convertTo0to1((float)idx));
        if (auto* p = proc.apvts.getParameter("echo_syncDot"))
            p->setValueNotifyingHost(dotted ? 1.0f : 0.0f);
        if (auto* p = proc.apvts.getParameter("echo_syncTrip"))
            p->setValueNotifyingHost(triplet ? 1.0f : 0.0f);
        if (auto* p = proc.apvts.getParameter("echo_count"))
            p->setValueNotifyingHost(p->convertTo0to1((float)count));
    };
    echoRow.onFreeMsChanged   = [this](float ms) {
        if (auto* p = proc.apvts.getParameter("echo_ms"))
            p->setValueNotifyingHost(p->convertTo0to1(ms));
    };
    echoRow.onFeedbackChanged = [this](float v) {
        if (auto* p = proc.apvts.getParameter("echo_fb"))
            p->setValueNotifyingHost(p->convertTo0to1(v));
    };
    echoRow.onSpreadChanged   = [this](float v) {
        if (auto* p = proc.apvts.getParameter("echo_spread"))
            p->setValueNotifyingHost(p->convertTo0to1(v));
    };
    echoRow.onDirtChanged     = [this](float v) {
        if (auto* p = proc.apvts.getParameter("echo_dirt"))
            p->setValueNotifyingHost(p->convertTo0to1(v));
    };

    // Set initial row visibility based on current algorithm.
    {
        const bool isEcho = (eff.getAlgorithmIndex() == EffectSlot::kEchoAlgoIndex);
        effectRow.setVisible(!isEcho);
        echoRow  .setVisible(isEcho);
    }

    // Set initial send labels on rhythm channels to match the current algorithm.
    updateEffectSendLabels();

    // Load current reverb param values into row knobs.
    {
        int ai = rev.getAlgorithmIndex();
        const auto& algos = FXAlgorithmRegistry::reverbAlgorithms();
        if (ai < (int)algos.size())
        {
            for (auto& [pid, apid] : revParamMap)
            {
                if (auto* raw = proc.apvts.getRawParameterValue(apid))
                    reverbRow.setParamValue(pid, *raw);
            }
        }
    }
}

void MixerOverlay::loadFromAPVTS()
{
    auto& apvts = proc.apvts;

    // Reload rhythm channel UI (only active channels have APVTS params).
    const int numActive = proc.getNumRhythms();
    for (int r = 0; r < numActive && r < (int)rhythmChannels.size(); ++r)
    {
        const juce::String prefix = "ch" + juce::String(r) + "_";
        rhythmChannels[r]->loadFromAPVTS(apvts, prefix);
    }
    effectReturn .loadFromAPVTS(apvts, "ret_eff_");
    delayReturn  .loadFromAPVTS(apvts, "ret_dly_");
    reverbReturn .loadFromAPVTS(apvts, "ret_rev_");
    masterChannel.loadFromAPVTS(apvts, "mstr_");

    // FX rows: reload enable states, algo selection, and param values.
    auto& eff = proc.fxChain.effectSlot();
    auto& rev = proc.fxChain.reverbSlot();

    // Effect row
    effectRow.setEnabled(*apvts.getRawParameterValue("eff_en") > 0.5f,
                         juce::dontSendNotification);
    // #242b: read algo from APVTS, not engine — APVTS is the source of truth.
    // If the engine somehow lags (e.g. state-restore listener skip), we still
    // show the user the value the host actually has stored.
    const int effAlgoFromApvts = juce::jlimit(0,
        (int)FXAlgorithmRegistry::effectAlgorithms().size() - 1,
        (int)*apvts.getRawParameterValue("eff_algo"));
    if (eff.getAlgorithmIndex() != effAlgoFromApvts)
        eff.setAlgorithm(effAlgoFromApvts);   // defensive resync
    effectRow.setSelectedAlgorithm(effAlgoFromApvts, juce::dontSendNotification);
    {
        const auto& algos = FXAlgorithmRegistry::effectAlgorithms();
        int ai = eff.getAlgorithmIndex();
        if (ai < (int)algos.size())
        {
            const auto& params = algos[ai].params;
            for (int i = 0; i < (int)params.size() && i < 5; ++i)
            {
                float norm = *apvts.getRawParameterValue("eff_p" + juce::String(i));
                effectRow.setParamValue(params[i].id,
                    params[i].minVal + norm * (params[i].maxVal - params[i].minVal));
            }
        }
    }

    // Echo row visibility and state
    {
        const bool isEcho = (eff.getAlgorithmIndex() == EffectSlot::kEchoAlgoIndex);
        effectRow.setVisible(!isEcho);
        echoRow  .setVisible(isEcho);
        if (isEcho)
        {
            echoRow.setEnabled(*apvts.getRawParameterValue("echo_en") > 0.5f,
                               juce::dontSendNotification);
            const bool syncMode = *apvts.getRawParameterValue("echo_mode") > 0.5f;
            echoRow.setSyncMode(syncMode);
            if (syncMode)
            {
                static const int denoms[] = { 32, 16, 8, 4 };
                int  idx  = juce::jlimit(0, 3, (int)*apvts.getRawParameterValue("echo_syncDenom"));
                bool dot  = *apvts.getRawParameterValue("echo_syncDot")  > 0.5f;
                bool trip = *apvts.getRawParameterValue("echo_syncTrip") > 0.5f;
                int  cnt  = (int)*apvts.getRawParameterValue("echo_count");
                echoRow.setSyncParams(denoms[idx], dot, trip, cnt);
            }
            else
            {
                echoRow.setFreeMs(*apvts.getRawParameterValue("echo_ms"));
            }
            echoRow.setFeedback(*apvts.getRawParameterValue("echo_fb"));
            echoRow.setSpread  (*apvts.getRawParameterValue("echo_spread"));
            echoRow.setDirt    (*apvts.getRawParameterValue("echo_dirt"));
        }
        resized();
    }

    // Delay row
    delayRow.setEnabled(*apvts.getRawParameterValue("dly_en") > 0.5f,
                        juce::dontSendNotification);
    {
        const bool syncMode = *apvts.getRawParameterValue("dly_mode") > 0.5f;
        delayRow.setSyncMode(syncMode);
        if (syncMode)
        {
            static const int denoms[] = { 32, 16, 8, 4 };
            int  idx  = juce::jlimit(0, 3, (int)*apvts.getRawParameterValue("dly_syncDenom"));
            bool dot  = *apvts.getRawParameterValue("dly_syncDot")  > 0.5f;
            bool trip = *apvts.getRawParameterValue("dly_syncTrip") > 0.5f;
            int  cnt  = (int)*apvts.getRawParameterValue("dly_count");
            delayRow.setSyncParams(denoms[idx], dot, trip, cnt);
        }
        else
        {
            delayRow.setFreeMs(*apvts.getRawParameterValue("dly_ms"));
        }
        delayRow.setFeedback(*apvts.getRawParameterValue("dly_fb"));
        delayRow.setSpread  (*apvts.getRawParameterValue("dly_spread"));
        delayRow.setDirt    (*apvts.getRawParameterValue("dly_dirt"));
    }

    // Reverb row
    reverbRow.setEnabled(*apvts.getRawParameterValue("rev_en") > 0.5f,
                         juce::dontSendNotification);
    reverbRow.setSelectedAlgorithm(rev.getAlgorithmIndex(), juce::dontSendNotification);
    {
        static const std::pair<const char*, const char*> revParamMap[] = {
            { "size",      "rev_size" },
            { "predelay",  "rev_pre"  },
            { "diffusion", "rev_diff" },
            { "damp",      "rev_damp" },
            { "mod",       "rev_mod"  },
            { "dirt",      "rev_dirt" },
        };
        for (auto& [pid, apid] : revParamMap)
            if (auto* raw = apvts.getRawParameterValue(apid))
                reverbRow.setParamValue(pid, *raw);
    }

    updateEffectSendLabels();
}

void MixerOverlay::refresh()
{
    buildRhythmChannels();
    updateEffectSendLabels();
    resized();
    repaint();
}

void MixerOverlay::updateEffectSendLabels()
{
    const auto& algos = FXAlgorithmRegistry::effectAlgorithms();
    int ai = proc.fxChain.effectSlot().getAlgorithmIndex();
    juce::String name = (ai < (int)algos.size()) ? algos[ai].name : "Effect";
    for (auto& ch : rhythmChannels)
        ch->setEffectSendLabel(name);
    effectReturn.setChannelName(name);
}

void MixerOverlay::resized()
{
    const int w = getWidth();
    const int h = getHeight();

    // #257: FX area height proportional to window height; row height derived from it.
    const int fxAreaH = juce::jmax(220, juce::roundToInt(h * 0.32f));
    const int fxRowH  = (fxAreaH - kFXGap * 2 - kFXPad * 2) / 3;
    const int stripH  = juce::jmax(200, h - fxAreaH - kHeaderH);

    // #255: channel widths proportional to window width.
    // 8 rhythm + 3 returns share available space; master fills any remainder.
    const int masterTotalW = kMasterW + MixerChannel::kInsertPanelW;
    const int nChans = MixerEngine::MaxChannels + 3;
    const int chanW  = juce::jmax(44, (w - 2 * kDivW - masterTotalW
                                       - (MixerEngine::MaxChannels - 1) * kChanGap) / nChans);

    lastFXAreaH = fxAreaH;
    lastFXRowH  = fxRowH;
    lastStripH  = stripH;
    lastChanW   = chanW;

    // Meter mode selector — right-aligned in the header strip
    constexpr int kModeW = 176;
    meterModeCtrl.setBounds(w - kModeW - 4, (kHeaderH - 18) / 2, kModeW, 18);

    // Channel strips start below the header
    int x = 0;
    for (int i = 0; i < (int)rhythmChannels.size(); ++i)
    {
        rhythmChannels[i]->setBounds(x, kHeaderH, chanW, stripH);
        x += chanW + (i + 1 < (int)rhythmChannels.size() ? kChanGap : 0);
    }

    lastDivX1 = x;
    x += kDivW;
    effectReturn .setBounds(x, kHeaderH, chanW, stripH);  x += chanW;
    delayReturn  .setBounds(x, kHeaderH, chanW, stripH);  x += chanW;
    reverbReturn .setBounds(x, kHeaderH, chanW, stripH);  x += chanW;

    lastDivX2 = x;
    x += kDivW;
    // Master strip fills any remaining horizontal space.
    const int masterActualW = juce::jmax(masterTotalW, w - x);
    masterChannel.setBounds(x, kHeaderH, masterActualW, stripH);

    // FX rows below the channel strips
    int fy = kHeaderH + stripH + kFXPad;
    effectRow.setBounds(kFXPad, fy, w - kFXPad * 2, fxRowH);
    echoRow  .setBounds(kFXPad, fy, w - kFXPad * 2, fxRowH);  fy += fxRowH + kFXGap;
    delayRow .setBounds(kFXPad, fy, w - kFXPad * 2, fxRowH);  fy += fxRowH + kFXGap;
    reverbRow.setBounds(kFXPad, fy, w - kFXPad * 2, fxRowH);
}

void MixerOverlay::propagateMeterMode(VUMeter::MeterMode m)
{
    for (auto& ch : rhythmChannels) ch->setMeterMode(m);
    effectReturn .setMeterMode(m);
    delayReturn  .setMeterMode(m);
    reverbReturn .setMeterMode(m);
    masterChannel.setMeterMode(m);
}

void MixerOverlay::paint(juce::Graphics& g)
{
    g.setColour(MuClidLookAndFeel::colour(MuClidLookAndFeel::panelBackground));
    g.fillAll();

    // Header separator line
    g.setColour(MuClidLookAndFeel::colour(MuClidLookAndFeel::segmentInactiveBorder));
    g.fillRect(0, kHeaderH - 1, getWidth(), 1);

    // Dividers between rhythm channels and returns, and before master
    g.setColour(MuClidLookAndFeel::colour(MuClidLookAndFeel::segmentInactiveBorder));
    g.fillRect(lastDivX1, kHeaderH, kDivW, lastStripH);
    g.fillRect(lastDivX2, kHeaderH, kDivW, lastStripH);

    // FX section: outer container panel + three inner row sub-panels
    const juce::Colour borderCol = MuClidLookAndFeel::colour(MuClidLookAndFeel::segmentInactiveBorder);
    const juce::Colour outerFill = MuClidLookAndFeel::colour(MuClidLookAndFeel::panelBackground)
                                       .darker(0.15f);
    const juce::Colour rowFill   = MuClidLookAndFeel::colour(MuClidLookAndFeel::panelBackground)
                                       .brighter(0.06f);

    // Outer container
    {
        juce::Rectangle<float> outer { 0.5f, (float)(kHeaderH + lastStripH) + 0.5f,
                                       (float)getWidth() - 1.0f, (float)lastFXAreaH - 1.0f };
        g.setColour(outerFill);
        g.fillRoundedRectangle(outer, 6.0f);
        g.setColour(borderCol);
        g.drawRoundedRectangle(outer, 6.0f, 1.5f);
    }

    // Inner row sub-panels (inset by kFXPad on all sides)
    int fy = kHeaderH + lastStripH + kFXPad;
    for (int i = 0; i < 3; ++i)
    {
        juce::Rectangle<float> panel { (float)kFXPad + 0.5f, (float)fy + 0.5f,
                                       (float)(getWidth() - kFXPad * 2) - 1.0f,
                                       (float)lastFXRowH - 1.0f };
        g.setColour(rowFill);
        g.fillRoundedRectangle(panel, 4.0f);
        g.setColour(borderCol);
        g.drawRoundedRectangle(panel, 4.0f, 1.0f);
        fy += lastFXRowH + kFXGap;
    }
}
