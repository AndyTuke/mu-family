#include "MixerOverlay.h"
#include "Audio/FX/Slots/FXAlgorithmDef.h"
#include "Audio/FX/Slots/EffectSlot.h"

// Precomputed param IDs for the 5 effect-slot params. Avoids the per-iteration
// `kEffParamIds[i]` String allocation in the snapshot/restore loops
// that fire whenever the effect algorithm is switched.
static const juce::String kEffParamIds[5] = {
    "eff_p0", "eff_p1", "eff_p2", "eff_p3", "eff_p4"
};

// only subscribe to APVTS IDs that actually affect the mixer view.
// Previously this class registered as a listener for *every* parameter in the
// tree (hundreds across all rhythms / voices / mods) and set apvtsDirty on any
// change — each loadFromAPVTS then rebuilt every channel strip. Risk: feedback
// storm via the universal listener every time a rhythm knob moved.
static bool isMixerRelevantParam(const juce::String& id) noexcept
{
    // Rhythm channel strips: ch0_*, ch1_*, … ch7_*
    if (id.startsWith("ch") && id.length() > 2 && juce::CharacterFunctions::isDigit(id[2]))
        return true;
    // Return channels: ret_eff_*, ret_dly_*, ret_rev_*
    if (id.startsWith("ret_eff_") || id.startsWith("ret_dly_") || id.startsWith("ret_rev_"))
        return true;
    // Master strip + inserts: mstr_*, mst_ins*
    if (id.startsWith("mstr_") || id.startsWith("mst_ins"))
        return true;
    // FX algorithm + per-algo params (effectRow, delayRow, reverbRow) + Echo mode.
    // Also intra-FX routing params (eff2dly, eff2rev, dly2rev) — these live outside
    // the ret_*_ prefix scheme but affect the return strip send knob display.
    if (id.startsWith("eff_") || id.startsWith("eff2")
     || id.startsWith("dly_") || id.startsWith("dly2")
     || id.startsWith("rev_") || id.startsWith("echo_"))
        return true;
    return false;
}

MixerOverlay::MixerOverlay(ProcessorBase& p, MixerEngine& m)
    : proc(p), mixer(m),
      effectRow("Effect", FXAlgorithmRegistry::effectAlgorithms(), MuLookAndFeel::knobFxSend),
      reverbRow("Reverb", FXAlgorithmRegistry::reverbAlgorithms(), MuLookAndFeel::knobReverb)
{
    buildChannels();
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

    // subscribe only to mixer-relevant IDs (see isMixerRelevantParam above).
    // Rhythm/euclid/voice/mod params no longer trigger spurious mixer reloads.
    for (auto* param : proc.getParameters())
        if (auto* pid = dynamic_cast<juce::AudioProcessorParameterWithID*>(param))
            if (isMixerRelevantParam(pid->getParameterID()))
                proc.apvts.addParameterListener(pid->getParameterID(), this);

    startTimerHz(mu_ui::kUiRefreshHz);
}

MixerOverlay::~MixerOverlay()
{
    stopTimer();
    for (auto* param : proc.getParameters())
        if (auto* pid = dynamic_cast<juce::AudioProcessorParameterWithID*>(param))
            if (isMixerRelevantParam(pid->getParameterID()))
                proc.apvts.removeParameterListener(pid->getParameterID(), this);
}

void MixerOverlay::parameterChanged(const juce::String& /*parameterID*/, float /*newValue*/)
{
    apvtsDirty.store(true, std::memory_order_release);
}

void MixerOverlay::visibilityChanged()
{
    // exchange so a parameterChanged firing between the read and the clear
    // is preserved for the next tick instead of being silently dropped.
    if (isVisible() && apvtsDirty.exchange(false, std::memory_order_acq_rel))
        loadFromAPVTS();
}

void MixerOverlay::timerCallback()
{
    if (isVisible() && apvtsDirty.exchange(false, std::memory_order_acq_rel))
        loadFromAPVTS();
}

void MixerOverlay::buildChannels()
{
    for (auto& ch : rhythmChannels)
        removeChildComponent(ch.get());
    rhythmChannels.clear();

    const auto& palette = MuLookAndFeel::channelPalette;
    const int numActive = proc.getNumChannels();
    for (int r = 0; r < MixerEngine::MaxChannels; ++r)
    {
        bool hasRhythm = r < numActive;
        juce::Colour col = hasRhythm ? palette[proc.getChannelColourIndex(r) % MuLookAndFeel::kChannelPaletteSize]
                                     : MuLookAndFeel::colour(MuLookAndFeel::mixerInactiveNameBg);
        juce::String name = hasRhythm ? juce::String(proc.getChannelName(r)) : "-";
        auto ch = std::make_unique<MixerChannel>(MixerChannel::Type::Channel, name, col);
        const juce::String prefix = "ch" + juce::String(r) + "_";
        ch->bindChannel(mixer.channels[r], mixer.channelPeaks[r], &proc, prefix,
                       &mixer.sidechainGR[r]);
        if (!hasRhythm) ch->setActive(false);
        // forward status updates to the global StatusBar via PluginEditor.
        ch->onStatusUpdate = [this](const juce::String& n, const juce::String& v, juce::Colour c) {
            if (onStatusUpdate) onStatusUpdate(n, v, c);
        };
        addAndMakeVisible(*ch);
        rhythmChannels.push_back(std::move(ch));
    }
    refreshSidechainSources();
}

void MixerOverlay::refreshSidechainSources()
{
    juce::StringArray names;
    const int numActive = proc.getNumChannels();
    for (int r = 0; r < MixerEngine::MaxChannels; ++r)
        names.add((r < numActive) ? juce::String(proc.getChannelName(r)) : juce::String());
    for (int r = 0; r < (int)rhythmChannels.size(); ++r)
        rhythmChannels[r]->setSidechainSources(r, names);
    // Return channels can sidechain from any rhythm channel (no self-exclusion, pass -1).
    effectReturn .setSidechainSources(-1, names);
    delayReturn  .setSidechainSources(-1, names);
    reverbReturn .setSidechainSources(-1, names);
}

void MixerOverlay::wireReturns()
{
    effectReturn.bindReturn(mixer.returns[0], mixer.returnPeaks[0], &proc, "ret_eff_",
                            &mixer.returnSidechainGR[0]);
    effectReturn.bindReturnSends(proc.apvts, "eff2dly", "eff2rev");

    delayReturn .bindReturn(mixer.returns[1], mixer.returnPeaks[1], &proc, "ret_dly_",
                            &mixer.returnSidechainGR[1]);
    delayReturn .bindReturnSends(proc.apvts, "", "dly2rev");

    reverbReturn.bindReturn(mixer.returns[2], mixer.returnPeaks[2], &proc, "ret_rev_",
                            &mixer.returnSidechainGR[2]);
    masterChannel.bindMaster(mixer, &proc);

    // forward status updates from return channels to the StatusBar via PluginEditor.
    auto forwardStatus = [this](const juce::String& n, const juce::String& v, juce::Colour c) {
        if (onStatusUpdate) onStatusUpdate(n, v, c);
    };
    effectReturn .onStatusUpdate = forwardStatus;
    delayReturn  .onStatusUpdate = forwardStatus;
    reverbReturn .onStatusUpdate = forwardStatus;
    masterChannel.onStatusUpdate = forwardStatus;
}

void MixerOverlay::wireFXRows()
{
    auto& eff = proc.fxChain.effectSlot();
    auto& dly = proc.fxChain.delaySlot();
    auto& rev = proc.fxChain.reverbSlot();

    // ── Effect row ───────────────────────────────────────────────────────────
    // Effect is a send/return path — the dry signal already lives in the
    // main bus, so the algorithm's wet/dry mix knob would double the dry component.
    // Hide the mix knob; EffectSlot forces the algorithm into sendMode (wet-only).
    effectRow.hideParameter("mix");
    effectRow.setEnabled(eff.isEnabled());
    effectRow.setSelectedAlgorithm(eff.getAlgorithmIndex());

#if JUCE_DEBUG
    // Debug visual — Size 1..4 knob demo at the right end of the Effect row
    // so the user can compare the four canonical knob size buckets at a
    // glance. Each knob is outlined at its actual rendered bounds. Only
    // compiled into Debug builds; Release ships without the demo cluster.
    effectRow.setShowSizeDemo(true);
#endif

    effectRow.onEnabledChanged = [this](bool e) {
        if (auto* p = proc.apvts.getParameter("eff_en"))
            p->setValueNotifyingHost(e ? 1.0f : 0.0f);
        // When Echo is the active algo, the effectRow On button is the sole visible
        // enable control — keep echo_en in sync so the echo path follows it.
        if (proc.fxChain.effectSlot().getAlgorithmIndex() == EffectSlot::kEchoAlgoIndex)
            if (auto* p = proc.apvts.getParameter("echo_en"))
                p->setValueNotifyingHost(e ? 1.0f : 0.0f);
    };
    effectRow.onAlgorithmChanged = [this](int idx) {
        const auto& algos = FXAlgorithmRegistry::effectAlgorithms();

        // Snapshot current algo params before switching so we can restore them if
        // the user cycles back.
        const int oldIndex = proc.fxChain.effectSlot().getAlgorithmIndex();
        if (oldIndex >= 0 && oldIndex < (int)algos.size())
        {
            const auto& oldParams = algos[oldIndex].params;
            auto& snap = effSnaps[oldIndex];
            float* pv[5] = { &snap.p0, &snap.p1, &snap.p2, &snap.p3, &snap.p4 };
            for (int i = 0; i < (int)oldParams.size() && i < 5; ++i)
            {
                float norm = *proc.apvts.getRawParameterValue(kEffParamIds[i]);
                *pv[i] = oldParams[i].minVal + norm * (oldParams[i].maxVal - oldParams[i].minVal);
            }
            effSnapValid[oldIndex] = true;
        }

        // force-sync the engine BEFORE writing APVTS. If APVTS already
        // holds the same value (e.g. state restore left it at idx while the
        // engine sat at default 0), setValueNotifyingHost skips the listener,
        // so syncFXParam never fires and the engine stays stale.
        proc.fxChain.effectSlot().setAlgorithm(idx);
        if (auto* p = proc.apvts.getParameter("eff_algo"))
            p->setValueNotifyingHost(p->convertTo0to1((float)idx));

        // Apply saved snapshot for this algo, or first-visit defaults.
        if (idx >= 0 && idx < (int)algos.size())
        {
            const auto& newParams = algos[idx].params;
            const EffectAlgoDefaults& src = (idx < 4 && effSnapValid[idx])
                ? effSnaps[idx]
                : mu_ui::kEffectAlgoDefaults[idx < 4 ? idx : 0];
            const float vals[5] = { src.p0, src.p1, src.p2, src.p3, src.p4 };
            for (int i = 0; i < (int)newParams.size() && i < 5; ++i)
            {
                const auto& pd = newParams[i];
                float norm = (pd.maxVal > pd.minVal)
                    ? juce::jlimit(0.0f, 1.0f, (vals[i] - pd.minVal) / (pd.maxVal - pd.minVal))
                    : 0.0f;
                if (auto* p = proc.apvts.getParameter(kEffParamIds[i]))
                    p->setValueNotifyingHost(norm);
                effectRow.setParamValue(pd.id, vals[i]);
            }
        }

        updateEffectSendLabels();
        const bool isEcho = (idx == EffectSlot::kEchoAlgoIndex);
        // Keep effectRow visible so the algo dropdown is always accessible.
        // When Echo is active, hide its knobs (irrelevant) and show echoRow in the
        // knobs area. When switching TO echo, sync echo_en to eff_en so the single
        // visible On button (effectRow's) controls the echo path.
        effectRow.setKnobsVisible(!isEcho);
        if (isEcho)
        {
            float effEn = *proc.apvts.getRawParameterValue("eff_en");
            if (auto* p = proc.apvts.getParameter("echo_en"))
                p->setValueNotifyingHost(effEn);
        }
        echoRow.setVisible(isEcho);
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
                if (auto* p = proc.apvts.getParameter(kEffParamIds[i]))
                    p->setValueNotifyingHost(norm);
                return;
            }
        }
    };

    // Load current algo param values into FX row knobs. Skip when the product
    // doesn't define eff_pN — the row renders with its defaults.
    if (proc.apvts.getParameter(kEffParamIds[0]) != nullptr)
    {
        int ai = eff.getAlgorithmIndex();
        const auto& algos = FXAlgorithmRegistry::effectAlgorithms();
        if (ai < (int)algos.size())
        {
            const auto& params = algos[ai].params;
            for (int i = 0; i < (int)params.size() && i < 5; ++i)
            {
                if (auto* raw = proc.apvts.getRawParameterValue(kEffParamIds[i]))
                {
                    float norm   = *raw;
                    float actual = params[i].minVal + norm * (params[i].maxVal - params[i].minVal);
                    effectRow.setParamValue(params[i].id, actual);
                }
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

        // setAlgorithm runs via the APVTS listener above and overwrites the
        // slot's internal size / predelay / diffusion / damp / mod with the
        // new algorithm's defaults (see ReverbSlot::applyAlgorithmPreset).
        // Push those defaults back into APVTS so the visible knobs match the
        // slot's new state — otherwise the user sees stale knob positions
        // while Hall (or whichever algo) plays at its internal defaults
        // until they touch each knob. Also updates the row's own knob
        // displays via the rev_size etc. listeners that fire from these
        // setValueNotifyingHost calls.
        auto& rev = proc.fxChain.reverbSlot();
        auto pushP = [this](const char* aid, float v)
        {
            if (auto* pp = proc.apvts.getParameter(aid))
                pp->setValueNotifyingHost(pp->convertTo0to1(v));
        };
        pushP("rev_size", rev.getSize());
        pushP("rev_pre",  rev.getPreDelay());
        pushP("rev_diff", rev.getDiffusion());
        pushP("rev_damp", rev.getDamp());
        pushP("rev_mod",  rev.getMod());
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
        effectRow.setKnobsVisible(!isEcho);
        echoRow  .setVisible(isEcho);
        echoRow  .setShowHeader(false);  // echoRow has no header; effectRow's header is always visible
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
    const int numActive = proc.getNumChannels();

    // Keep each strip's active/name/colour in sync with the live channel count.
    // buildChannels() (which sets these) only runs on refresh(), gated on mixer
    // visibility — so a preset / host-state load that changes the channel count
    // while the mixer is hidden would leave stale dim overlays + names when it's
    // next shown. loadFromAPVTS runs on visibilityChanged + preset reload, so
    // refreshing here makes the colouring track the count regardless of when it changed.
    const auto& palette = MuLookAndFeel::channelPalette;
    for (int r = 0; r < (int)rhythmChannels.size(); ++r)
    {
        const bool hasRhythm = r < numActive;
        rhythmChannels[r]->setActive(hasRhythm);
        rhythmChannels[r]->setChannelName(hasRhythm ? juce::String(proc.getChannelName(r)) : "-");
        rhythmChannels[r]->setChannelColour(
            hasRhythm ? palette[proc.getChannelColourIndex(r) % MuLookAndFeel::kChannelPaletteSize]
                      : MuLookAndFeel::colour(MuLookAndFeel::mixerInactiveNameBg));
    }

    for (int r = 0; r < numActive && r < (int)rhythmChannels.size(); ++r)
    {
        const juce::String prefix = "ch" + juce::String(r) + "_";
        rhythmChannels[r]->loadFromAPVTS(apvts, prefix);
    }
    effectReturn .loadFromAPVTS(apvts, "ret_eff_");
    delayReturn  .loadFromAPVTS(apvts, "ret_dly_");
    reverbReturn .loadFromAPVTS(apvts, "ret_rev_");
    masterChannel.loadFromAPVTS(apvts, "mstr_");

    // Intra-FX routing sends (eff2dly, eff2rev, dly2rev) are stored outside the
    // ret_*_ prefix scheme so loadFromAPVTS can't find them via the prefix. Refresh
    // the return strip send knobs explicitly so they track preset loads and automation.
    if (auto* p = apvts.getRawParameterValue("eff2dly")) effectReturn.setDelaySendValue(*p);
    if (auto* p = apvts.getRawParameterValue("eff2rev")) effectReturn.setRevSendValue(*p);
    if (auto* p = apvts.getRawParameterValue("dly2rev")) delayReturn .setRevSendValue(*p);

    refreshSidechainSources();

    // FX rows: reload enable states, algo selection, and param values.
    // Products without the FX rack (e.g. mu-tant first-stab) don't define
    // the eff_/dly_/rev_/echo_ APVTS schema; skip the entire FX reload so the
    // raw-value dereferences below don't null-crash. The FX UI rows still
    // render — they're inert until the product wires the rack.
    const bool hasFXRack = apvts.getParameter("eff_en") != nullptr;
    if (!hasFXRack)
    {
        updateEffectSendLabels();
        return;
    }

    auto& eff = proc.fxChain.effectSlot();
    auto& rev = proc.fxChain.reverbSlot();

    // Effect row
    effectRow.setEnabled(*apvts.getRawParameterValue("eff_en") > 0.5f,
                         juce::dontSendNotification);
    // read algo from APVTS, not engine — APVTS is the source of truth.
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
                float norm = *apvts.getRawParameterValue(kEffParamIds[i]);
                effectRow.setParamValue(params[i].id,
                    params[i].minVal + norm * (params[i].maxVal - params[i].minVal));
            }
        }
    }

    // Echo row visibility and state
    {
        const bool isEcho = (eff.getAlgorithmIndex() == EffectSlot::kEchoAlgoIndex);
        effectRow.setKnobsVisible(!isEcho);
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
    buildChannels();
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

    // Voice-section Amp "Effect" send label tracks the same name. PluginEditor
    // wires this to RhythmPanel → VoiceSection → AmpSubsection.
    if (onEffectAlgorithmNameChanged)
        onEffectAlgorithmNameChanged(name);
}

void MixerOverlay::resized()
{
    // Medium baseline + s() wrap. The container width (getWidth()) is the
    // already-scaled allocation from PluginEditor; everything else here
    // multiplies through s() so toggling scale propagates uniformly.
    using mu_ui::s;
    const int w        = getWidth();
    const int fxAreaH  = s(MuLookAndFeel::kMixerFXAreaH);
    const int fxRowH   = s(MuLookAndFeel::kMixerFXRowH);
    const int stripH   = s(MuLookAndFeel::kMixerStripH);
    const int chanW    = s(MuLookAndFeel::kMixerChanW);
    const int masterTotalW = s(MuLookAndFeel::kMixerMasterTotalW);
    const int hdrH     = s(kHeaderH);
    const int fxPad    = s(kFXPad);
    const int fxGap    = s(kFXGap);
    const int divW     = s(kDivW);
    const int chanGap  = s(kChanGap);
    const int labelPanelW = s(kLabelPanelW);

    lastFXAreaH = fxAreaH;
    lastFXRowH  = fxRowH;
    lastStripH  = stripH;
    lastChanW   = chanW;

    // Meter mode selector — right-aligned in the header strip
    const int modeW = s(176);
    const int modeBtnH = s(18);
    meterModeCtrl.setBounds(w - modeW - s(4), (hdrH - modeBtnH) / 2, modeW, modeBtnH);

    int x = labelPanelW;
    for (int i = 0; i < (int)rhythmChannels.size(); ++i)
    {
        rhythmChannels[i]->setBounds(x, hdrH, chanW, stripH);
        x += chanW + (i + 1 < (int)rhythmChannels.size() ? chanGap : 0);
    }

    lastDivX1 = x;
    x += divW;
    effectReturn .setBounds(x, hdrH, chanW, stripH);  x += chanW;
    delayReturn  .setBounds(x, hdrH, chanW, stripH);  x += chanW;
    reverbReturn .setBounds(x, hdrH, chanW, stripH);  x += chanW;

    lastDivX2 = x;
    x += divW;
    const int masterActualW = juce::jmax(masterTotalW, w - x);
    masterChannel.setBounds(x, hdrH, masterActualW, stripH);

    // FX rows below the channel strips
    int fy = hdrH + stripH + fxPad;
    const int fxHeaderW = s(FXRow::kHeaderWidth);
    effectRow.setBounds(fxPad, fy, w - fxPad * 2, fxRowH);
    echoRow.setBounds(fxPad + fxHeaderW, fy,
                      juce::jmax(0, w - fxPad * 2 - fxHeaderW), fxRowH);
    fy += fxRowH + fxGap;
    delayRow .setBounds(fxPad, fy, w - fxPad * 2, fxRowH);  fy += fxRowH + fxGap;
    reverbRow.setBounds(fxPad, fy, w - fxPad * 2, fxRowH);
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
    using mu_ui::sf;
    g.setColour(MuLookAndFeel::colour(MuLookAndFeel::panelBackground));
    g.fillAll();

    // Header separator line
    g.setColour(MuLookAndFeel::colour(MuLookAndFeel::segmentInactiveBorder));
    g.fillRect(0, kHeaderH - 1, getWidth(), 1);

    // ── Row label panel (left of channel strips, labels aligned with strip sections) ─
    if (!rhythmChannels.empty())
    {
        auto* refCh = rhythmChannels[0].get();
        const int lx  = 0;
        const int lw  = kLabelPanelW;
        const int chY = kHeaderH;  // channels start at kHeaderH in MixerOverlay space

        // Slightly darker background for the label panel
        g.setColour(MuLookAndFeel::colour(MuLookAndFeel::panelBackground).darker(0.25f));
        g.fillRect(lx, chY, lw, lastStripH);

        // Right-edge separator
        g.setColour(MuLookAndFeel::colour(MuLookAndFeel::segmentInactiveBorder));
        g.fillRect(lw - 1, chY, 1, lastStripH);

        g.setColour(MuLookAndFeel::colour(MuLookAndFeel::mutedText));
        g.setFont(juce::Font(juce::FontOptions{}.withHeight(11.0f)));

        // Draw a label rotated 90° CCW, centred in 'bounds' (component-space rect).
        auto drawVLabel = [&](const juce::String& text, juce::Rectangle<int> bounds)
        {
            if (bounds.isEmpty() || bounds.getHeight() < 14) return;
            const juce::Rectangle<int> r { lx, chY + bounds.getY(), lw, bounds.getHeight() };
            const float cx = (float)r.getCentreX();
            const float cy = (float)r.getCentreY();
            g.saveState();
            g.addTransform(juce::AffineTransform::rotation(
                -juce::MathConstants<float>::halfPi, cx, cy));
            g.drawFittedText(text,
                (int)(cx - r.getHeight() * 0.5f), (int)(cy - r.getWidth() * 0.5f),
                r.getHeight(), r.getWidth(),
                juce::Justification::centred, 1, 0.75f);
            g.restoreState();
        };

        drawVLabel("Side Chain", refCh->getSidechainPaneBounds());
        drawVLabel("Sends",      refCh->getSendsPaneBounds());
        drawVLabel("Levels",     refCh->getFaderPaneBounds());
        if (!refCh->getOutBusBounds().isEmpty())
            drawVLabel("Output", refCh->getOutBusBounds());

        // Horizontal section-separator lines spanning the full channel area.
        const juce::Colour sepCol = MuLookAndFeel::colour(
            MuLookAndFeel::segmentInactiveBorder).withAlpha(0.35f);
        g.setColour(sepCol);
        const int lineX1 = lw;
        const int lineX2 = getWidth();
        auto drawHLine = [&](juce::Rectangle<int> bounds)
        {
            if (bounds.isEmpty()) return;
            const int lineY = chY + bounds.getY();
            g.drawLine((float)lineX1, (float)lineY, (float)lineX2, (float)lineY, 1.0f);
        };
        drawHLine(refCh->getSidechainPaneBounds());
        drawHLine(refCh->getSendsPaneBounds());
        drawHLine(refCh->getFaderPaneBounds());
        if (!refCh->getOutBusBounds().isEmpty())
            drawHLine(refCh->getOutBusBounds());
    }

    // Dividers between rhythm channels and returns, and before master
    g.setColour(MuLookAndFeel::colour(MuLookAndFeel::segmentInactiveBorder));
    g.fillRect(lastDivX1, kHeaderH, kDivW, lastStripH);
    g.fillRect(lastDivX2, kHeaderH, kDivW, lastStripH);

    // Mixer is a global view (not tied to a specific rhythm) so panel borders
    // use globalAccent (purple) — the rhythm palette's purple was retired so
    // this can't collide with any rhythm border. Each border is inset by
    // kBorderInset on all sides so the visible purple line has clean breathing
    // room — matches the .reduced(2) pattern in RhythmPanel::paint.
    using mu_ui::s;
    const int borderInset = s(2);
    const juce::Colour globalCol = MuLookAndFeel::colour(MuLookAndFeel::globalAccent);
    const juce::Colour outerFill = MuLookAndFeel::colour(MuLookAndFeel::panelBackground)
                                       .darker(0.15f);
    const juce::Colour rowFill   = MuLookAndFeel::colour(MuLookAndFeel::panelBackground)
                                       .brighter(0.06f);

    // ── Channel strips outline ─────────────────────────────────────────────
    // One rounded rect framing the whole strips area (label panel → master),
    // inset so the border doesn't sit on the overlay edge or against the FX
    // container below it.
    {
        const juce::Rectangle<int> stripsArea {
            0, kHeaderH, getWidth(), lastStripH };
        g.setColour(globalCol);
        g.drawRoundedRectangle(stripsArea.reduced(borderInset).toFloat(),
                               sf(6.0f), sf(2.0f));
    }

    // ── FX section: outer container + three inner row sub-panels ──────────
    // Outer container has its own .reduced(borderInset) so the gap between
    // the strips outline and the FX outer matches the gap between the FX
    // outer and the overlay edge.
    {
        const juce::Rectangle<int> outerArea {
            0, kHeaderH + lastStripH, getWidth(), lastFXAreaH };
        const auto outerF = outerArea.reduced(borderInset).toFloat();
        g.setColour(outerFill);
        g.fillRoundedRectangle(outerF, sf(6.0f));
        g.setColour(globalCol);
        g.drawRoundedRectangle(outerF, sf(6.0f), sf(2.0f));
    }

    // Inner FX rows — inset from the outer container's interior by kFXPad,
    // and each row sub-panel reduced by 1 so its border doesn't touch its
    // neighbour above/below.
    int fy = kHeaderH + lastStripH + kFXPad;
    for (int i = 0; i < 3; ++i)
    {
        const juce::Rectangle<int> rowArea {
            kFXPad, fy, getWidth() - kFXPad * 2, lastFXRowH };
        const auto rowF = rowArea.reduced(1).toFloat();
        g.setColour(rowFill);
        g.fillRoundedRectangle(rowF, sf(4.0f));
        g.setColour(globalCol);
        g.drawRoundedRectangle(rowF, sf(4.0f), sf(1.5f));
        fy += lastFXRowH + kFXGap;
    }
}
