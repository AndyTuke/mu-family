#include "VoicePanel.h"
#include "Plugin/PluginProcessor.h"
#include "Audio/Scales.h"
#include "Audio/AlgorithmNames.h"   // mu-core: kFilterTypeNames (shared canonical list)
#include "UI/Components/MuLookAndFeel.h"
#include "UI/ConfirmDialog.h"   // mu-core shared confirm dialogs
#include "Modulation/MuTantModSnap.h"

namespace mu_tant
{

// ── FilterRoutingButton ───────────────────────────────────────────────────────
FilterRoutingButton::FilterRoutingButton() : juce::Button({})
{
    setClickingTogglesState(true);
    setToggleState(true, juce::dontSendNotification);
    setTooltip("Series: F1 into F2.  Parallel: F1 + F2 mixed.");
    targetAngle = animAngle = 90.0f;   // default Series = vertical (‖)
    getToggleStateValue().addListener(this);
}

FilterRoutingButton::~FilterRoutingButton()
{
    stopTimer();   // a rotation animation may be mid-flight at teardown
    getToggleStateValue().removeListener(this);
}

void FilterRoutingButton::valueChanged(juce::Value&)
{
    targetAngle = getToggleState() ? 90.0f : 0.0f;   // Series=90° (‖), Parallel=0° (═ two side-by-side paths)
    if (! angleInit) { animAngle = targetAngle; angleInit = true; repaint(); }  // no spin on first load
    else             { startTimerHz(60); }
}

void FilterRoutingButton::timerCallback()
{
    constexpr float kStep = 6.0f;
    const float diff = targetAngle - animAngle;
    if (std::abs(diff) <= kStep) { animAngle = targetAngle; stopTimer(); }
    else { animAngle += diff > 0.0f ? kStep : -kStep; }
    repaint();
}

void FilterRoutingButton::paintButton(juce::Graphics& g, bool highlighted, bool)
{
    const auto r = getLocalBounds().toFloat().reduced(1.5f);
    // Family purple — rendered at all times regardless of Series/Parallel state.
    const auto accent = MuLookAndFeel::colour(MuLookAndFeel::knobEuclidean);
    g.setColour(highlighted ? accent.brighter(0.25f) : accent.withAlpha(0.9f));
    g.fillEllipse(r);
    g.setColour(accent.brighter(0.4f));
    g.drawEllipse(r, 1.2f);

    const auto  b     = r.reduced(r.getWidth() * 0.20f);
    const float thick = juce::jmax(1.8f, b.getHeight() * 0.13f);
    const float sep   = b.getHeight() * 0.22f;
    const float hw    = b.getWidth() * 0.5f;
    const float cx    = b.getCentreX(), cy = b.getCentreY();
    const float rad   = animAngle * juce::MathConstants<float>::pi / 180.0f;
    const float cosA  = std::cos(rad), sinA = std::sin(rad);
    g.setColour(juce::Colours::white.withAlpha(0.95f));
    for (int sign : { -1, 1 })
    {
        const float ox = -sinA * sep * (float) sign;
        const float oy =  cosA * sep * (float) sign;
        juce::Path line;
        line.startNewSubPath(cx + ox - cosA * hw, cy + oy - sinA * hw);
        line.lineTo        (cx + ox + cosA * hw, cy + oy + sinA * hw);
        g.strokePath(line, juce::PathStrokeType(thick, juce::PathStrokeType::mitered, juce::PathStrokeType::square));
    }
}
namespace
{
    juce::StringArray rootNames()
    {
        return { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    }

    void populateRoots(DropdownSelect& d)
    {
        const auto names = rootNames();
        for (int i = 0; i < names.size(); ++i)
            d.addItem(names[i], i + 1);
    }

    void populateScales(DropdownSelect& d)
    {
        for (int i = 0; i < (int) kScales.size(); ++i)
            d.addItem(kScales[(size_t) i].name, i + 1);
    }

    void populateXmodModes(DropdownSelect& d)
    {
        d.addItem("Off",  1);
        d.addItem("FM",   2);
        d.addItem("AM",   3);
        d.addItem("Ring", 4);
    }

    void populateFilterTypes(DropdownSelect& d)
    {
        // Use the family-canonical display order from mu-core — same order as
        // mu-clid's FilterSubsection. Item ID = algorithm index + 1.
        mu_audio::populateFilterTypeDropdown([&d](const char* n, int id) { d.addItem(n, id); });
    }

    void populateNoiseTypes(DropdownSelect& d)
    {
        d.addItem("White", 1);
        d.addItem("Pink",  2);
    }
}

VoicePanel::VoicePanel(PluginProcessor& p)
    : proc(p),
      insertSub(p, "v"),
      modDestProvider(makeModDestProvider())
{
    auto& apvts = proc.apvts;

    auto setupLabel = [this](juce::Label& l, const juce::String& text)
    {
        l.setText(text, juce::dontSendNotification);
        l.setJustificationType(juce::Justification::centredRight);
        l.setFont(juce::Font(juce::FontOptions{}.withHeight(11.0f)));
        addAndMakeVisible(l);
    };

    // ── Shared per-layer header bar (name / reset / delete / preset / save) ──
    // Reset + delete confirm first (shared mu-core dialog), matching mu-clid.
    headerBar.onDelete = [this]
    {
        if (proc.getNumVoices() <= 1) return;   // can't delete the last voice
        const juce::String name = proc.getChannelName(currentVoice);
        mu_ui::confirmAsync(this, "Delete Voice", "Delete \"" + name + "\"?\nThis cannot be undone.",
                            "Delete", [this] { if (onDeleteVoice) onDeleteVoice(); });
    };
    headerBar.onReset  = [this]
    {
        const juce::String name = proc.getChannelName(currentVoice);
        mu_ui::confirmAsync(this, "Reset Voice", "Reset \"" + name + "\" to defaults?\nThis cannot be undone.",
                            "Reset", [this]
        {
            proc.resetVoice(currentVoice);
            setVoice(currentVoice);           // re-sync knobs / gate / modulators
        });
    };
    headerBar.onPresetSelected = [this](int id)
    {
        const int i = id - 1;
        if (i >= 0 && i < (int) voicePresetFiles.size())
        {
            proc.loadVoicePreset(currentVoice, voicePresetFiles[(size_t) i]);
            setVoice(currentVoice);
        }
    };
    headerBar.setSaveEnabled(proc.canSaveLayerPreset());   // demo: per-layer save disabled
    headerBar.onSave = [this]
    {
        if (! proc.canSaveLayerPreset()) return;
        juce::Component::SafePointer<VoicePanel> safe(this);
        mu_ui::promptTextAsync(this, "Save Voice Preset", "Preset name:",
                               "Voice " + juce::String(currentVoice + 1), "Save",
            [safe](const juce::String& name)
            {
                if (safe != nullptr && name.isNotEmpty())
                {
                    safe->proc.saveVoicePreset(safe->currentVoice, name);
                    safe->refreshVoicePresetList();
                }
            });
    };
    addAndMakeVisible(headerBar);

    // ── Tonal centre (shared — bound once) ──────────────────────────────────
    setupLabel(rootLabel,  "Root");
    setupLabel(scaleLabel, "Scale");
    populateRoots(rootDropdown);
    populateScales(scaleDropdown);
    addAndMakeVisible(rootDropdown);
    addAndMakeVisible(scaleDropdown);
    rootAttachment  = std::make_unique<APVTS::ComboBoxAttachment>(apvts, "root",  rootDropdown.getComboBox());
    scaleAttachment = std::make_unique<APVTS::ComboBoxAttachment>(apvts, "scale", scaleDropdown.getComboBox());

    // ── Per-voice controls (added once, rebound per-voice) ──────────────────
    for (auto* k : { &o1OctKnob, &o1SemiKnob, &o1FineKnob, &o1PosKnob, &o1PenvDepthKnob,
                     &o2OctKnob, &o2SemiKnob, &o2FineKnob, &o2PosKnob, &o2PenvDepthKnob,
                     &xmodIndexKnob, &xmodDepthKnob,
                     &osc1LevelKnob, &osc2LevelKnob, &noiseLevelKnob,
                     &fltDrvKnob, &fltCutKnob, &fltResKnob, &fltEnvDepthKnob, &fltLoCutKnob,
                     &flt2DrvKnob, &flt2CutKnob, &flt2ResKnob, &flt2EnvDepthKnob, &flt2LoCutKnob })
        addAndMakeVisible(k);
    addAndMakeVisible(levelKnob);
    // Level knob is intentionally hidden: the per-voice output level is driven by
    // readConfig() directly from the APVTS param (cfg.levelDb) every block, so the
    // param and engine are always in sync even without a visible control. Keeping the
    // SliderAttachment ensures the APVTS param still saves/loads and can be modulated.
    levelKnob.setVisible(false);

    // Wavetable selectors — items are rebuilt per voice by refreshWavetableDropdowns
    // (factory names + optional user import + "Load .wav…"); onChange routes here.
    addAndMakeVisible(osc1WaveDropdown);
    addAndMakeVisible(osc2WaveDropdown);
    osc1WaveDropdown.onChange = [this](int itemId) { handleWtSelection(0, itemId); };
    osc2WaveDropdown.onChange = [this](int itemId) { handleWtSelection(1, itemId); };


    syncButton.setClickingTogglesState(true);
    syncButton.setTooltip("Hard sync: osc1 wrap resets osc2 phase");
    addAndMakeVisible(syncButton);

    fdbkButton.setClickingTogglesState(true);
    fdbkButton.setTooltip("Feedback FM: osc1's output also modulates osc2");
    addAndMakeVisible(fdbkButton);

    // X-Mod mode switches — manually wired to the choice params (item index = choice
    // index) in setVoice(); the amp-mode change also rebinds the Depth knob's target.
    xmodDepthKnob.getSlider().setDoubleClickReturnValue(true, 0.0);
    addAndMakeVisible(phaseModeCtrl);
    addAndMakeVisible(ampModeCtrl);
    phaseModeCtrl.onChange = [this](int idx) {
        if (auto* p = proc.apvts.getParameter(PluginProcessor::voiceParamId(currentVoice, "xmod_phaseMode")))
            p->setValueNotifyingHost(p->convertTo0to1((float) idx));
    };
    ampModeCtrl.onChange = [this](int idx) {
        if (auto* p = proc.apvts.getParameter(PluginProcessor::voiceParamId(currentVoice, "xmod_ampMode")))
            p->setValueNotifyingHost(p->convertTo0to1((float) idx));
        bindAmpDepthKnob(idx == 2);   // SSB → bind Depth knob to the shift param
    };

    setupLabel(noiseTypeLabel, "Noise");
    populateNoiseTypes(noiseTypeDropdown);
    addAndMakeVisible(noiseTypeDropdown);

    setupLabel(fltTypeLabel,  "Type");
    setupLabel(flt2TypeLabel, "Type");
    populateFilterTypes(fltTypeDropdown);
    populateFilterTypes(flt2TypeDropdown);
    addAndMakeVisible(fltTypeDropdown);
    addAndMakeVisible(flt2TypeDropdown);

    // Cutoff / Resonance value formatting lives on the APVTS parameter (see
    // createParameterLayout) so the SliderAttachment can't clobber it. The
    // cutoff value reads as a bare number (Hz / kHz), so carry the unit in the
    // knob label, switching it as the value crosses 1 kHz — matching mu-clid.
    fltDrvKnob.getSlider().textFromValueFunction = [](double v) -> juce::String {
        return juce::String((int)std::round(v * 100.0));
    };
    fltLoCutKnob.getSlider().textFromValueFunction = [](double v) -> juce::String {
        if (v <= 0.0) return "Off";
        if (v < 1000.0) return juce::String((int)std::round(v));
        return juce::String(v / 1000.0, 2);
    };
    fltLoCutKnob.onValueChanged = [this](double v) {
        fltLoCutKnob.setLabel(v <= 0.0 ? "Low Cut" : (v < 1000.0 ? "Low Cut (Hz)" : "Low Cut (kHz)"));
    };
    fltCutKnob.onValueChanged = [this](double v)
    {
        fltCutKnob.setLabel(v < 1000.0 ? "Cutoff (Hz)" : "Cutoff (kHz)");
    };

    // ── Filter 2 ────────────────────────────────────────────────────────────
    setupLabel(flt2TypeLabel, "Type");
    populateFilterTypes(flt2TypeDropdown);
    addAndMakeVisible(flt2TypeDropdown);

    flt2DrvKnob.getSlider().textFromValueFunction = [](double v) -> juce::String {
        return juce::String((int)std::round(v * 100.0));
    };
    flt2LoCutKnob.getSlider().textFromValueFunction = [](double v) -> juce::String {
        if (v <= 0.0) return "Off";
        if (v < 1000.0) return juce::String((int)std::round(v));
        return juce::String(v / 1000.0, 2);
    };
    flt2LoCutKnob.onValueChanged = [this](double v) {
        flt2LoCutKnob.setLabel(v <= 0.0 ? "Low Cut" : (v < 1000.0 ? "Low Cut (Hz)" : "Low Cut (kHz)"));
    };
    flt2CutKnob.onValueChanged = [this](double v) {
        flt2CutKnob.setLabel(v < 1000.0 ? "Cutoff (Hz)" : "Cutoff (kHz)");
    };

    addAndMakeVisible(fltSeriesBtn);

    // ── Gating designer (Gap slider + Bypass button are inside it) ──────────
    // The GatingDesigner owns the Gap slider and Bypass button as children;
    // VoicePanel just creates the APVTS attachments per-voice in rebindAttachments.
    addAndMakeVisible(gatingDesigner);

    // ── Insert effect (shared mu-core panel) ────────────────────────────────
    // mu-tant reads its insert params fresh from APVTS each block (no listener),
    // so the 5-write algo switch needs no bulk-change guard — leave runBulkChange
    // null. Insert slots aren't modulation destinations in mu-tant yet, so the
    // mod-arc hooks stay null too. Only the GR meter + transport state are wired.
    addAndMakeVisible(insertSub);
    insertSub.isPlaying   = [this] { return proc.isInternalPlaying(); };
    insertSub.getInsertGR = [this]() -> const std::atomic<float>* {
        return proc.getInsertGRPtr(currentVoice);
    };
    // Re-label the insert modulation destinations to the active algo's slot names
    // (mirrors mu-clid) so the dropdown shows e.g. "Drive"/"Tone" instead of P1..P4.
    insertSub.onInsertAlgorithmChanged = [this](int charId)
    {
        modulatorPanel.setInsertAlgorithm(charId);
    };

    // ── Modulator panel ─────────────────────────────────────────────────────
    addAndMakeVisible(modulatorPanel);
    modulatorPanel.setDestProvider(&modDestProvider);
    // A modulator edit (assignment/mode/dest change) may flip whether a Stepped CS drives
    // pitch — refresh the processor's cached flags for the active voice (message thread).
    modulatorPanel.onChange = [this] { proc.refreshPitchQuantFlags(currentVoice); };

    rebindAttachments();
    refreshVoicePresetList();   // initial scan; onSave will re-scan after any save
    refreshHeader();

    startTimerHz(30);
}

VoicePanel::~VoicePanel() { stopTimer(); }

void VoicePanel::timerCallback()
{
    // Normalise the transport beat to 0..1 across the current pattern length
    // for the gating-grid playhead. Pattern length may be 1..16 bars.
    const bool   playing = proc.isInternalPlaying();
    const double beat    = proc.getInternalBeatPos();
    const auto* gPat = &proc.gatePatterns[(size_t) currentVoice];
    const double patBeats = (double) gPat->patternLengthBars * 4.0;
    const double beat01  = (patBeats > 0.0) ? std::fmod(beat, patBeats) / patBeats : 0.0;
    gatingDesigner.setPlayhead(beat01, playing);
    modulatorPanel.setPlayheadBeat(beat);

    // Sync filter-type dropdown from APVTS so DAW automation is reflected in the UI.
    // The dropdown uses manual wiring (no ComboBoxAttachment) so there is no automatic
    // reverse path from APVTS changes to the displayed value.
    if (auto* raw = proc.apvts.getRawParameterValue(
            PluginProcessor::voiceParamId(currentVoice, "flt_type")))
    {
        const int apvtsAlgo = juce::jlimit(0, 15, (int) raw->load());
        if (fltTypeDropdown.getSelectedId() != apvtsAlgo + 1)
            fltTypeDropdown.setSelectedId(apvtsAlgo + 1, false);
    }
    if (auto* raw = proc.apvts.getRawParameterValue(
            PluginProcessor::voiceParamId(currentVoice, "flt2_type")))
    {
        const int apvtsAlgo = juce::jlimit(0, 15, (int) raw->load());
        if (flt2TypeDropdown.getSelectedId() != apvtsAlgo + 1)
            flt2TypeDropdown.setSelectedId(apvtsAlgo + 1, false);
    }

    // Sync the wavetable dropdowns' factory selection from APVTS (preset / automation).
    // A loaded user table overrides the factory index, so leave its item selected.
    const int maxWt = juce::jmax(0, WavetableBank::factoryTableNames().size() - 1);
    auto syncWt = [&](DropdownSelect& dd, int osc, const char* pid)
    {
        if (proc.userWavetablePath(currentVoice, osc).isNotEmpty()) return;
        if (auto* raw = proc.apvts.getRawParameterValue(PluginProcessor::voiceParamId(currentVoice, pid)))
        {
            const int idx = juce::jlimit(0, maxWt, (int) raw->load());
            if (dd.getSelectedId() != idx + 1) dd.setSelectedId(idx + 1, false);
        }
    };
    syncWt(osc1WaveDropdown, 0, "o1_wt");
    syncWt(osc2WaveDropdown, 1, "o2_wt");
}

void VoicePanel::setVoice(int voiceIndex)
{
    voiceIndex = juce::jlimit(0, PluginProcessor::kMaxVoices - 1, voiceIndex);

    // Rebuild the APVTS attachments only when the voice actually changes (or on
    // the very first bind). The pattern + modulator-slot pointers are (re)bound
    // unconditionally below — otherwise the initial setVoice(0) call would skip
    // them (currentVoice already 0, attachments already built in the ctor),
    // leaving the gate editor unbound so the pencil draws nothing.
    if (voiceIndex != currentVoice || o1OctAttachment == nullptr)
    {
        currentVoice = voiceIndex;
        rebindAttachments();
    }

    // Re-bind unconditionally — same reason as modulatorPanel / gatingDesigner below:
    // the ctor calls rebindAttachments() directly, so o1OctAttachment != nullptr when
    // setVoice(0) first fires and the if-guard above is false, which would leave the
    // knob arc bindings unset for voice 0 unless we re-bind here every time.
    bindModulationIndicators();

    modulatorPanel.setVoiceSlot(&proc.voiceSlots[(size_t) currentVoice]);
    gatingDesigner.setPattern(&proc.gatePatterns[(size_t) currentVoice]);
    gatingDesigner.setFilterPattern(&proc.filterPatterns[(size_t) currentVoice]);
    gatingDesigner.setPitchPattern(&proc.pitchPatterns[(size_t) currentVoice]);
    insertSub.setChannel(currentVoice);   // reloads algo + slot knobs from APVTS
    // setChannel → configureInsertAlgorithm fires insertSub.onInsertAlgorithmChanged,
    // which re-labels the modulator insert destinations for this voice's algo.
    // Unconditional — a preset load calls setVoice(0) while already on voice 0, so
    // the rebindAttachments() guard above is skipped; the wavetable dropdowns are
    // not APVTS attachments, so they must be rebuilt here to reflect the loaded table.
    refreshWavetableDropdowns();
    refreshHeader();
    repaint();   // sub-panel borders + section titles paint in the active voice's palette colour
}

void VoicePanel::rebindAttachments()
{
    auto& apvts = proc.apvts;
    auto id = [this](const char* base) {
        return PluginProcessor::voiceParamId(currentVoice, base);
    };

    // Destroy previous attachments first — the JUCE attachment dtor unhooks
    // the listener before the new one binds, so the slider doesn't briefly
    // double-fire from two attachments while we swap.
    o1OctAttachment  = nullptr; o1SemiAttachment      = nullptr;
    o1FineAttachment = nullptr; o1PosAttachment       = nullptr; o1PenvDepthAttachment = nullptr;
    o2OctAttachment  = nullptr; o2SemiAttachment      = nullptr;
    o2FineAttachment = nullptr; o2PosAttachment       = nullptr; o2PenvDepthAttachment = nullptr;
    xmodIndexAttachment = nullptr; xmodDepthAttachment = nullptr; syncAttachment = nullptr; fdbkAttachment = nullptr;
    osc1LevelAttachment  = nullptr; osc2LevelAttachment = nullptr;
    noiseLevelAttachment = nullptr; noiseTypeAttachment = nullptr;
    fltDrvAttachment = nullptr; fltCutAttachment = nullptr; fltResAttachment = nullptr;
    fltEnvDepthAttachment = nullptr; fltLoCutAttachment = nullptr;
    flt2DrvAttachment = nullptr; flt2CutAttachment = nullptr; flt2ResAttachment = nullptr;
    flt2EnvDepthAttachment = nullptr; flt2LoCutAttachment = nullptr; fltSeriesAttachment = nullptr;
    levelAttachment = nullptr;
    gapAttachment = nullptr; gateBypassAttachment = nullptr;

    o1OctAttachment       = std::make_unique<APVTS::SliderAttachment>(apvts, id("o1_oct"),         o1OctKnob.getSlider());
    o1SemiAttachment      = std::make_unique<APVTS::SliderAttachment>(apvts, id("o1_semi"),        o1SemiKnob.getSlider());
    o1FineAttachment      = std::make_unique<APVTS::SliderAttachment>(apvts, id("o1_fine"),        o1FineKnob.getSlider());
    o1PosAttachment       = std::make_unique<APVTS::SliderAttachment>(apvts, id("o1_pos"),         o1PosKnob.getSlider());
    o1PenvDepthAttachment = std::make_unique<APVTS::SliderAttachment>(apvts, id("o1_penv_depth"),  o1PenvDepthKnob.getSlider());

    o2OctAttachment  = std::make_unique<APVTS::SliderAttachment>(apvts, id("o2_oct"),  o2OctKnob.getSlider());
    o2SemiAttachment      = std::make_unique<APVTS::SliderAttachment>(apvts, id("o2_semi"),        o2SemiKnob.getSlider());
    o2FineAttachment      = std::make_unique<APVTS::SliderAttachment>(apvts, id("o2_fine"),        o2FineKnob.getSlider());
    o2PosAttachment       = std::make_unique<APVTS::SliderAttachment>(apvts, id("o2_pos"),         o2PosKnob.getSlider());
    o2PenvDepthAttachment = std::make_unique<APVTS::SliderAttachment>(apvts, id("o2_penv_depth"),  o2PenvDepthKnob.getSlider());

    // Wavetable selectors are rebuilt for the active voice (factory selection or a
    // loaded user table); onChange was wired once in the constructor.
    refreshWavetableDropdowns();

    xmodIndexAttachment = std::make_unique<APVTS::SliderAttachment>(apvts, id("xmod_index"), xmodIndexKnob.getSlider());
    syncAttachment      = std::make_unique<APVTS::ButtonAttachment>(apvts, id("sync"),       syncButton);
    fdbkAttachment      = std::make_unique<APVTS::ButtonAttachment>(apvts, id("xmod_fdbk"),  fdbkButton);

    // Mode switches — manual (choice item index = stored choice index), matching the
    // filter-type dropdown pattern; the amp-mode selection also drives which param the
    // Depth knob binds to.
    {
        int pm = 1;
        if (auto* raw = apvts.getRawParameterValue(id("xmod_phaseMode"))) pm = juce::jlimit(0, 2, (int) raw->load());
        phaseModeCtrl.setSelectedIndex(pm, false);
        int am = 0;
        if (auto* raw = apvts.getRawParameterValue(id("xmod_ampMode")))   am = juce::jlimit(0, 2, (int) raw->load());
        ampModeCtrl.setSelectedIndex(am, false);
        currentAmpMode = am;
        bindAmpDepthKnob(am == 2);   // binds xmodDepthAttachment to depth/ssb for the active voice
    }

    osc1LevelAttachment  = std::make_unique<APVTS::SliderAttachment>  (apvts, id("o1_lvl"),     osc1LevelKnob.getSlider());
    osc2LevelAttachment  = std::make_unique<APVTS::SliderAttachment>  (apvts, id("o2_lvl"),     osc2LevelKnob.getSlider());
    noiseLevelAttachment = std::make_unique<APVTS::SliderAttachment>  (apvts, id("noise_lvl"),  noiseLevelKnob.getSlider());
    noiseTypeAttachment  = std::make_unique<APVTS::ComboBoxAttachment>(apvts, id("noise_type"), noiseTypeDropdown.getComboBox());

    // Filter type — manual wiring (item IDs ≠ sequential, so no ComboBoxAttachment).
    // Item ID = algorithm index + 1, matching the AudioParameterInt(0..15) stored value.
    {
        auto* fltParam = apvts.getParameter(id("flt_type"));
        int algo = 0;
        if (auto* raw = apvts.getRawParameterValue(id("flt_type")))
            algo = juce::jlimit(0, 15, (int) raw->load());
        fltTypeDropdown.setSelectedId(algo + 1, juce::dontSendNotification);
        fltTypeDropdown.onChange = [fltParam](int itemId) {
            if (fltParam)
                fltParam->setValueNotifyingHost(fltParam->convertTo0to1((float)(itemId - 1)));
        };
    }
    fltDrvAttachment      = std::make_unique<APVTS::SliderAttachment>(apvts, id("flt_drv"),       fltDrvKnob.getSlider());
    fltCutAttachment      = std::make_unique<APVTS::SliderAttachment>(apvts, id("flt_cut"),       fltCutKnob.getSlider());
    fltResAttachment      = std::make_unique<APVTS::SliderAttachment>(apvts, id("flt_res"),       fltResKnob.getSlider());
    fltEnvDepthAttachment = std::make_unique<APVTS::SliderAttachment>(apvts, id("flt_env_depth"), fltEnvDepthKnob.getSlider());
    fltLoCutAttachment    = std::make_unique<APVTS::SliderAttachment>(apvts, id("flt_lo_cut"),    fltLoCutKnob.getSlider());

    // Filter 2 — same manual-wiring approach for the type dropdown.
    {
        auto* flt2Param = apvts.getParameter(id("flt2_type"));
        int algo2 = 0;
        if (auto* raw = apvts.getRawParameterValue(id("flt2_type")))
            algo2 = juce::jlimit(0, 15, (int) raw->load());
        flt2TypeDropdown.setSelectedId(algo2 + 1, juce::dontSendNotification);
        flt2TypeDropdown.onChange = [flt2Param](int itemId) {
            if (flt2Param)
                flt2Param->setValueNotifyingHost(flt2Param->convertTo0to1((float)(itemId - 1)));
        };
    }
    flt2DrvAttachment      = std::make_unique<APVTS::SliderAttachment>(apvts, id("flt2_drv"),       flt2DrvKnob.getSlider());
    flt2CutAttachment      = std::make_unique<APVTS::SliderAttachment>(apvts, id("flt2_cut"),       flt2CutKnob.getSlider());
    flt2ResAttachment      = std::make_unique<APVTS::SliderAttachment>(apvts, id("flt2_res"),       flt2ResKnob.getSlider());
    flt2EnvDepthAttachment = std::make_unique<APVTS::SliderAttachment>(apvts, id("flt2_env_depth"), flt2EnvDepthKnob.getSlider());
    flt2LoCutAttachment    = std::make_unique<APVTS::SliderAttachment>(apvts, id("flt2_lo_cut"),    flt2LoCutKnob.getSlider());
    fltSeriesAttachment    = std::make_unique<APVTS::ButtonAttachment>(apvts, id("flt_series"),     fltSeriesBtn);

    levelAttachment = std::make_unique<APVTS::SliderAttachment>(apvts, id("level"), levelKnob.getSlider());
    gapAttachment        = std::make_unique<APVTS::SliderAttachment>(apvts, id("gate_gap"),     gatingDesigner.gapSlider);
    gateBypassAttachment = std::make_unique<APVTS::ButtonAttachment>(apvts, id("gate_bypass"), gatingDesigner.bypassButton);

    // Mirror the Gap value into the designer's render cache.
    gatingDesigner.setGap((float)(gatingDesigner.gapSlider.getValue() / 100.0));
}

void VoicePanel::bindAmpDepthKnob(bool ssbMode)
{
    // The single Depth knob drives xmod_depth (Mult) or xmod_ssb (SSB) — swap the
    // attachment (and thus the slider's range) to whichever the active mode uses.
    currentAmpMode = ssbMode ? 1 : 0;
    xmodDepthAttachment = nullptr;
    xmodDepthAttachment = std::make_unique<APVTS::SliderAttachment>(
        proc.apvts, PluginProcessor::voiceParamId(currentVoice, ssbMode ? "xmod_ssb" : "xmod_depth"),
        xmodDepthKnob.getSlider());
}

void VoicePanel::bindModulationIndicators()
{
    const int vi = currentVoice;
    const auto* mx = &proc.voiceSlots[(size_t) vi].modulationMatrix;
    static const float kNaN = std::numeric_limits<float>::quiet_NaN();

    auto bind = [&](KnobWithLabel& k, const char* destId, mu_tant::ModSnapIdx snap)
    {
        k.bindModulation(destId, mx,
            [&proc = proc, vi, snap]() -> float {
                return proc.isInternalPlaying() ? proc.getTantSnap(vi, (int) snap) : kNaN; });
    };

    bind(o1OctKnob,      "osc1.octave",       mu_tant::kTantSnapOsc1Octave);
    bind(o1SemiKnob,     "osc1.semi",         mu_tant::kTantSnapOsc1Semi);
    bind(o1FineKnob,     "osc1.fine",         mu_tant::kTantSnapOsc1Fine);
    bind(o1PosKnob,      "osc1.pos",          mu_tant::kTantSnapOsc1Pos);
    bind(o2OctKnob,      "osc2.octave",       mu_tant::kTantSnapOsc2Octave);
    bind(o2SemiKnob,     "osc2.semi",         mu_tant::kTantSnapOsc2Semi);
    bind(o2FineKnob,     "osc2.fine",         mu_tant::kTantSnapOsc2Fine);
    bind(o2PosKnob,      "osc2.pos",          mu_tant::kTantSnapOsc2Pos);
    bind(xmodIndexKnob,  "xmod.index",        mu_tant::kTantSnapXModIndex);
    bind(xmodDepthKnob,  "xmod.depth",        mu_tant::kTantSnapXModDepth);
    bind(osc1LevelKnob,  "osc1.level",        mu_tant::kTantSnapOsc1Level);
    bind(osc2LevelKnob,  "osc2.level",        mu_tant::kTantSnapOsc2Level);
    bind(noiseLevelKnob, "noise.level",       mu_tant::kTantSnapNoiseLevel);
    bind(fltCutKnob,     "filter.cutoff",     mu_tant::kTantSnapFilterCutoff);
    bind(fltResKnob,     "filter.resonance",  mu_tant::kTantSnapFilterRes);
    bind(levelKnob,      "level",             mu_tant::kTantSnapLevel);
}

void VoicePanel::clearAllModBindings()
{
    o1OctKnob.clearModBinding();
    o1SemiKnob.clearModBinding();
    o1FineKnob.clearModBinding();
    o1PosKnob.clearModBinding();
    o2OctKnob.clearModBinding();
    o2SemiKnob.clearModBinding();
    o2FineKnob.clearModBinding();
    o2PosKnob.clearModBinding();
    xmodIndexKnob.clearModBinding();
    xmodDepthKnob.clearModBinding();
    osc1LevelKnob.clearModBinding();
    osc2LevelKnob.clearModBinding();
    noiseLevelKnob.clearModBinding();
    fltCutKnob.clearModBinding();
    fltResKnob.clearModBinding();
    flt2CutKnob.clearModBinding();
    flt2ResKnob.clearModBinding();
    levelKnob.clearModBinding();
}

void VoicePanel::refreshHeader()
{
    headerBar.setLayerName(proc.getChannelName(currentVoice));
    headerBar.setColour(MuLookAndFeel::channelPalette[
        (size_t)(proc.getChannelColourIndex(currentVoice) % MuLookAndFeel::kChannelPaletteSize)]);
    // Preset list is NOT rescanned here — it doesn't change on voice switch,
    // only after a save. refreshVoicePresetList() is called from the ctor and
    // from the onSave callback instead.
}

void VoicePanel::refreshVoicePresetList()
{
    voicePresetFiles.clear();
    juce::StringArray names;
    const auto dir = proc.getPerSlotPresetDir();
    if (dir.isDirectory())
        for (const auto& f : dir.findChildFiles(juce::File::findFiles, false,
                                                "*." + proc.getPerSlotPresetExtension()))
        {
            voicePresetFiles.push_back(f);
            names.add(f.getFileNameWithoutExtension());
        }
    headerBar.setPresetItems(names);
}

void VoicePanel::paint(juce::Graphics& g)
{
    using Id = MuLookAndFeel::ColourIds;
    using mu_ui::s;
    using mu_ui::sf;
    g.fillAll(MuLookAndFeel::colour(Id::panelBackground));

    const auto voiceCol = MuLookAndFeel::channelPalette[
        (size_t)(proc.getChannelColourIndex(currentVoice) % MuLookAndFeel::kChannelPaletteSize)];
    const auto muted = MuLookAndFeel::colour(Id::mutedText);

    g.setFont(juce::Font(juce::FontOptions{}.withHeight(sf(10.0f))));

    // Panel styling: 2px rounded border in the per-voice palette colour, small
    // uppercase muted title in the top-left corner of every sub-panel.
    auto panel = [&](const juce::Rectangle<int>& r, const juce::String& title)
    {
        if (r.isEmpty()) return;
        g.setColour(voiceCol);
        g.drawRoundedRectangle(r.toFloat().reduced(1.0f), sf(6.0f), sf(2.0f));
        g.setColour(muted);
        g.drawText(title, r.getX() + s(8), r.getY() + s(4), s(50), s(12),
                   juce::Justification::centredLeft, false);
    };
    panel(osc1PanelR,     "OSC 1");
    panel(osc2PanelR,     "OSC 2");
    panel(noisePanelR,    "NOISE");
    panel(modNoisePanelR, "X-MOD");
    panel(filterPanelR,   "FILTER");
    panel(insertPanelR,   "INSERT");
    panel(mixerPanelR,    "MIXER");
}

void VoicePanel::resized()
{
    using mu_ui::s;
    const int w = getWidth();
    const int h = getHeight();

    const int pad = s(12);
    const int gap = s(8);
    const int ddH = s(24);
    const int s2W = s(MuLookAndFeel::kKnobSize2W);   // 54
    const int s2H = s(MuLookAndFeel::kKnobSize2H);   // 56

    // ── Shared header bar + Root / Scale row ────────────────────────────────
    const int barH = s(ChannelHeaderBar::kHeight);   // 28
    headerBar.setBounds(0, 0, w, barH);
    const int tonalY = barH + s(2);
    {
        const int labelW = s(44);
        int x = pad;
        rootLabel    .setBounds(x, tonalY, labelW, ddH);   x += labelW + s(4);
        rootDropdown .setBounds(x, tonalY, s(56),   ddH);  x += s(56)  + s(12);
        scaleLabel   .setBounds(x, tonalY, labelW, ddH);   x += labelW + s(4);
        scaleDropdown.setBounds(x, tonalY, s(100),  ddH);
    }
    const int contentTop = tonalY + ddH + gap;   // top of all sub-panels

    // ── Column geometry ─────────────────────────────────────────────────────
    // Mixer column (far right, spans Row A + Row B): one knob wide, three knobs tall.
    const int mixerW  = s(MuLookAndFeel::kKnobSize2W + 20);   // ~74
    const int mixerX  = w - pad - mixerW;
    // Left region (up to Mixer):
    const int leftRight = mixerX - gap;
    const int leftW     = leftRight - pad;    // 870 at default window width
    // Insert column (right of rowsW, left of Mixer, spans Row B only):
    const int insSubW  = s(4 * MuLookAndFeel::kKnobSize2W);        // 216
    const int insSubH  = s(2 * MuLookAndFeel::kKnobSize2H + MuLookAndFeel::kVoiceGap);  // 116
    const int insTitleH = s(14);
    const int insColW  = insSubW + s(20);     // 236
    const int insColX  = leftRight - insColW; // 646
    const int rowsW    = insColX - gap - pad; // 626 — shared by Row A osc zone and Row B X-Mod/Filter

    // ── Row heights ──────────────────────────────────────────────────────────
    // Row A: Osc 1 | Osc 2 | Noise side-by-side — natural knob height.
    const int rowAH = s2H + s(8);   // 64
    // Row B: height for two filter rows + vertical padding. The Series/Parallel toggle
    // sits at the left edge between the rows, so it doesn't add to the height requirement.
    const int rowBH = 2 * s2H + s(32);              // 2×56 + 32 = 144px

    // ── Row A: Osc 1 | Osc 2 | Noise (side by side, using full leftW) ───────
    // Osc panels extend across the Insert column zone since Insert is Row B only.
    {
        const int noiseW    = s(170);
        const int oscW      = (leftW - noiseW - 2 * gap) / 2;   // ≈ 342
        const int rowY      = contentTop + s(4);
        const int waveY     = rowY + (s2H - ddH) / 2;

        // 5 pitch / scan knobs are right-aligned in each panel; the wavetable
        // dropdown fills the space to their left and grows with the panel width.
        const int knobsW  = 5 * s2W + 4 * s(2);
        const int wtInset = s(8);   // keep the dropdown clear of the panel border + knobs

        // Osc 1 (the "OSC 1" panel title sits in the top-left corner above this row).
        osc1PanelR = { pad, contentTop, oscW, rowAH };
        {
            const int knobsX = pad + oscW - knobsW - s(6);
            const int wtX    = pad + wtInset;
            osc1WaveDropdown.setBounds(wtX, waveY, juce::jmax(s(40), knobsX - wtX - wtInset), ddH);
            int x = knobsX;
            for (auto* k : { &o1OctKnob, &o1SemiKnob, &o1FineKnob, &o1PenvDepthKnob, &o1PosKnob })
            { k->setBounds(x, rowY, s2W, s2H);  x += s2W + s(2); }
        }

        // Osc 2
        const int osc2X = pad + oscW + gap;
        osc2PanelR = { osc2X, contentTop, oscW, rowAH };
        {
            const int knobsX = osc2X + oscW - knobsW - s(6);
            const int wtX    = osc2X + wtInset;
            osc2WaveDropdown.setBounds(wtX, waveY, juce::jmax(s(40), knobsX - wtX - wtInset), ddH);
            int x = knobsX;
            for (auto* k : { &o2OctKnob, &o2SemiKnob, &o2FineKnob, &o2PenvDepthKnob, &o2PosKnob })
            { k->setBounds(x, rowY, s2W, s2H);  x += s2W + s(2); }
        }

        // Noise section (type dropdown; level is in the Mixer column)
        const int noiseX      = pad + oscW + gap + oscW + gap;
        const int noiseTitleW = s(42);   // clear space for "NOISE" panel label in paint()
        noisePanelR = { noiseX, contentTop, noiseW, rowAH };
        noiseTypeLabel.setBounds(0, 0, 0, 0);   // title drawn by paint(), label hidden
        noiseTypeDropdown.setBounds(noiseX + noiseTitleW, waveY, noiseW - noiseTitleW - s(8), ddH);
    }

    // ── Row B: X-Mod | Filter (no Level) | Insert ───────────────────────────
    const int rowBY = contentTop + rowAH + gap;   // top of Row B
    {

        // X-Mod panel — two horizontal lanes (one per row). Each lane: knob, then a mode
        // SegmentControl, then (Lane A only) the Sync + Feedback toggles. Widened so the
        // lanes read left-to-right; the Filter panel's type dropdowns shrink to suit.
        const int xmodW = s(248);
        modNoisePanelR = { pad, rowBY, xmodW, rowBH };
        {
            const int titleH = s(14);                       // room for the "X-MOD" panel title
            const int rowH   = (rowBH - titleH) / 2;        // two equal lane rows
            const int row1Y  = rowBY + titleH;
            const int row2Y  = row1Y + rowH;
            const int innerX = pad + s(6);
            const int segW   = s(96);                       // 3-segment mode switch
            const int togW   = s(32);

            // Place a lane's knob + mode segment; returns the segment's right edge.
            auto layoutLane = [&](int rowY, KnobWithLabel& knob, SegmentControl& seg) -> int
            {
                knob.setBounds(innerX, rowY + (rowH - s2H) / 2, s2W, s2H);
                const int segX = innerX + s2W + s(8);
                seg.setBounds(segX, rowY + (rowH - ddH) / 2, segW, ddH);
                return segX + segW;
            };

            // Lane A — Index knob, Phase mode (FM/PM/TZFM), Sync + Feedback toggles.
            const int seg1R = layoutLane(row1Y, xmodIndexKnob, phaseModeCtrl);
            const int togY  = row1Y + (rowH - ddH) / 2;
            syncButton.setBounds(seg1R + s(8),                togY, togW, ddH);
            fdbkButton.setBounds(seg1R + s(8) + togW + s(3),  togY, togW, ddH);

            // Lane B — Depth knob, Amp mode (AM/RM/SSB).
            layoutLane(row2Y, xmodDepthKnob, ampModeCtrl);
        }

        // Filter panel — two rows (Filter 1 + Filter 2) with Series/Parallel toggle.
        const int filterX      = pad + xmodW + gap;
        const int filterTitleW = s(30);
        const int filterW      = rowsW - xmodW - gap;
        filterPanelR = { filterX, rowBY, filterW, rowBH };
        {
            // Each row: type_dd + 5×s2W knobs, the dropdown taking the remaining width.
            const int rowH = s2H;
            // Two filter rows, vertically centred in rowBH.
            const int twoRowsH   = 2 * rowH + s(8);
            const int rowsStartY = rowBY + (rowBH - twoRowsH) / 2;
            const int row1Y = rowsStartY;
            const int row2Y = row1Y + rowH + s(8);

            // Series/Parallel round button — at the left edge of the panel (just inside
            // the border, below the "FILTER" title), vertically centred between the rows.
            juce::ignoreUnused(filterTitleW);
            const int serBtnD = s(30);
            const int serBtnX = filterX + s(8);
            const int serBtnY = rowBY + (rowBH - serBtnD) / 2;
            fltSeriesBtn.setBounds(serBtnX, serBtnY, serBtnD, serBtnD);

            // Controls start after the button.
            const int xStart     = serBtnX + serBtnD + s(8);
            const int knobGap    = s(2);
            // Dropdown width fills remaining space: filterX+filterW - xStart - 5×s2W - 4×knobGap
            const int fullTypeDdW = (filterX + filterW) - xStart - 5 * s2W - 4 * knobGap;

            // Type labels hidden — the dropdown content is self-explanatory.
            fltTypeLabel .setBounds(0, 0, 0, 0);
            flt2TypeLabel.setBounds(0, 0, 0, 0);

            // Filter 1 row
            {
                int x = xStart;
                fltTypeDropdown.setBounds(x, row1Y + (rowH - ddH) / 2, fullTypeDdW, ddH); x += fullTypeDdW + knobGap;
                for (auto* k : { &fltDrvKnob, &fltCutKnob, &fltResKnob, &fltEnvDepthKnob, &fltLoCutKnob })
                    { k->setBounds(x, row1Y, s2W, s2H); x += s2W + knobGap; }
            }
            // Filter 2 row
            {
                int x = xStart;
                flt2TypeDropdown.setBounds(x, row2Y + (rowH - ddH) / 2, fullTypeDdW, ddH); x += fullTypeDdW + knobGap;
                for (auto* k : { &flt2DrvKnob, &flt2CutKnob, &flt2ResKnob, &flt2EnvDepthKnob, &flt2LoCutKnob })
                    { k->setBounds(x, row2Y, s2W, s2H); x += s2W + knobGap; }
            }
        }

        // Insert panel (Row B only, same height as Row B)
        insertPanelR = { insColX, rowBY, insColW, rowBH };
        {
            const int subX = insColX + (insColW - insSubW) / 2;
            const int subY = rowBY + insTitleH + (rowBH - insTitleH - insSubH) / 2;
            insertSub.setBounds(subX, subY, insSubW, insSubH);
        }
    }

    // ── Mixer column — spans Row A + Row B, knobs stacked vertically ──────────
    {
        const int spanH   = rowAH + gap + rowBH;
        mixerPanelR = { mixerX, contentTop, mixerW, spanH };
        const int knobX   = mixerX + (mixerW - s2W) / 2;
        const int stackH  = 3 * s2H + 2 * gap;
        int ky = contentTop + (spanH - stackH) / 2;
        for (auto* k : { &osc1LevelKnob, &osc2LevelKnob, &noiseLevelKnob })
        {
            k->setBounds(knobX, ky, s2W, s2H);
            ky += s2H + gap;
        }
    }

    // ── Gating designer — full width ─────────────────────────────────────────
    int y = rowBY + rowBH + gap;
    {
        const int gateH = s(38 + 134 + 10 + 4);   // 186 — kHdr1H + kGridH + kScrollH + border
        gatingDesigner.setBounds(pad, y, w - 2 * pad, gateH);
        y += gateH + gap;
    }

    // ── Modulator panel — fills remaining height (minimum matches mu-clid) ───
    modulatorPanel.setBounds(pad, y, w - 2 * pad,
                             juce::jmax(s(332), h - y - pad));
}

// Item-ID ranges. Factory tables use 1..N (= APVTS index + 1). The Wavetables/
// folder files use kWtFolderBase + i (path-based, via the disk-import machinery).
static constexpr int kWtUserItemId = 901;   // active table loaded from OUTSIDE the folder
static constexpr int kWtLoadItemId = 902;   // "Load .wav…" browse action
static constexpr int kWtFolderBase = 1000;  // first Wavetables/ folder file

void VoicePanel::refreshWavetableDropdowns()
{
    const auto& names = WavetableBank::factoryTableNames();
    const auto  root  = proc.getWavetablesDir();

    // Scan the Wavetables/ content folder recursively (shared by both oscillators).
    // Each containing sub-folder becomes a category heading; files in the root sit
    // under "Wavetables". Category = the parent's path relative to the root.
    auto categoryOf = [&root](const juce::File& f) -> juce::String
    {
        const auto parent = f.getParentDirectory();
        if (parent == root) return {};                       // root → "Wavetables" heading
        return parent.getRelativePathFrom(root)              // nested → "Parent / Child"
                     .replaceCharacter('\\', '/').replace("/", " / ");
    };

    wtFolderFiles.clear();
    for (const auto& f : root.findChildFiles(juce::File::findFiles, true, "*.wav"))
        wtFolderFiles.push_back(f);
    // Group by category (root files first), then natural filename within a category.
    std::sort(wtFolderFiles.begin(), wtFolderFiles.end(),
              [&](const juce::File& a, const juce::File& b)
              {
                  const auto ca = categoryOf(a), cb = categoryOf(b);
                  if (ca != cb) return ca.isEmpty() != cb.isEmpty() ? ca.isEmpty()
                                                                    : ca.compareNatural(cb) < 0;
                  return a.getFileName().compareNatural(b.getFileName()) < 0;
              });

    auto rebuild = [&](DropdownSelect& dd, int osc, const char* pid)
    {
        dd.clear();
        for (int i = 0; i < names.size(); ++i) dd.addItem(names[i], i + 1);

        juce::String lastCat = "\x01";   // sentinel so the first file always emits a heading
        for (int i = 0; i < (int) wtFolderFiles.size(); ++i)
        {
            const auto cat = categoryOf(wtFolderFiles[(size_t) i]);
            if (cat != lastCat)
            {
                dd.addSectionHeading(cat.isEmpty() ? "Wavetables" : cat);
                lastCat = cat;
            }
            dd.addItem(wtFolderFiles[(size_t) i].getFileNameWithoutExtension(), kWtFolderBase + i);
        }

        const auto path = proc.userWavetablePath(currentVoice, osc);

        // Find the scanned-folder item for the loaded table (anywhere under the root,
        // sub-folders included). A table loaded from OUTSIDE the tree gets its own item.
        int folderId = -1;
        if (path.isNotEmpty())
            for (int i = 0; i < (int) wtFolderFiles.size(); ++i)
                if (wtFolderFiles[(size_t) i].getFullPathName() == path) { folderId = kWtFolderBase + i; break; }

        if (path.isNotEmpty() && folderId < 0)
        {
            const juce::String fn = juce::File(path).getFileNameWithoutExtension();
            dd.addItem(proc.userWavetableMissing(currentVoice, osc) ? ("! " + fn) : fn, kWtUserItemId);
        }
        dd.addItem(juce::String::fromUTF8("Load .wav\xe2\x80\xa6"), kWtLoadItemId);

        // Reflect the current selection.
        if (path.isNotEmpty())
        {
            dd.setSelectedId(folderId >= 0 ? folderId : kWtUserItemId, false);
        }
        else
        {
            int idx = 0;
            if (auto* raw = proc.apvts.getRawParameterValue(PluginProcessor::voiceParamId(currentVoice, pid)))
                idx = juce::jlimit(0, names.size() - 1, (int) raw->load());
            dd.setSelectedId(idx + 1, false);
        }
    };
    rebuild(osc1WaveDropdown, 0, "o1_wt");
    rebuild(osc2WaveDropdown, 1, "o2_wt");
}

void VoicePanel::handleWtSelection(int oscIdx, int itemId)
{
    if (itemId == kWtUserItemId) return;   // already on the loaded user table

    if (itemId == kWtLoadItemId)
    {
        const int v = currentVoice;        // freeze target voice for the async callback
        wtChooser = std::make_unique<juce::FileChooser>("Load wavetable (.wav)", proc.getWavetablesDir(), "*.wav");
        auto safe = juce::Component::SafePointer<VoicePanel>(this);
        wtChooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
            [safe, oscIdx, v](const juce::FileChooser& fc)
            {
                if (safe == nullptr) return;
                const auto f = fc.getResult();
                if (f.existsAsFile()) safe->proc.loadUserWavetable(v, oscIdx, f);
                safe->refreshWavetableDropdowns();   // show new table, or revert on cancel
            });
        return;
    }

    if (itemId >= kWtFolderBase)           // a Wavetables/ folder file → load by path
    {
        const int fi = itemId - kWtFolderBase;
        if (fi >= 0 && fi < (int) wtFolderFiles.size())
            proc.loadUserWavetable(currentVoice, oscIdx, wtFolderFiles[(size_t) fi]);
        refreshWavetableDropdowns();
        return;
    }

    // Factory item (1..N): clear any user table for this osc, set the APVTS index.
    proc.clearUserWavetable(currentVoice, oscIdx);
    if (auto* p = proc.apvts.getParameter(PluginProcessor::voiceParamId(currentVoice, oscIdx == 0 ? "o1_wt" : "o2_wt")))
        p->setValueNotifyingHost(p->convertTo0to1((float) (itemId - 1)));
    refreshWavetableDropdowns();
}

} // namespace mu_tant
