#include "VoicePanel.h"
#include "Plugin/PluginProcessor.h"
#include "Audio/Scales.h"
#include "Audio/AlgorithmNames.h"   // mu-core: kFilterTypeNames (shared canonical list)
#include "UI/Components/MuLookAndFeel.h"
#include "UI/ConfirmDialog.h"   // mu-core shared confirm dialogs
#include "Modulation/MuTantModSnap.h"

namespace mu_tant
{

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
        mu_ui::confirmAsync("Delete Voice", "Delete \"" + name + "\"?\nThis cannot be undone.",
                            "Delete", [this] { if (onDeleteVoice) onDeleteVoice(); });
    };
    headerBar.onReset  = [this]
    {
        const juce::String name = proc.getChannelName(currentVoice);
        mu_ui::confirmAsync("Reset Voice", "Reset \"" + name + "\" to defaults?\nThis cannot be undone.",
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
    headerBar.onSave = [this]
    {
        auto* w = new juce::AlertWindow("Save Voice Preset", "Preset name:",
                                        juce::MessageBoxIconType::NoIcon);
        w->addTextEditor("name", "Voice " + juce::String(currentVoice + 1));
        w->addButton("Save",   1, juce::KeyPress(juce::KeyPress::returnKey));
        w->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
        juce::Component::SafePointer<VoicePanel> safe(this);
        w->enterModalState(true, juce::ModalCallbackFunction::create(
            [safe, w](int r)
            {
                if (safe != nullptr && r == 1)
                {
                    safe->proc.saveVoicePreset(safe->currentVoice, w->getTextEditorContents("name"));
                    safe->refreshVoicePresetList();
                }
            }), true);
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
                     &xmodKnob, &osc1LevelKnob, &osc2LevelKnob, &noiseLevelKnob,
                     &fltCutKnob, &fltResKnob, &fltEnvDepthKnob })
        addAndMakeVisible(k);
    addAndMakeVisible(levelKnob);
    levelKnob.setVisible(false);   // per-voice level removed from the panel; APVTS param kept

    // Wavetable selectors: hidden until the wavetable bank API lands.
    // No APVTS binding or engine wiring yet — hidden to avoid a visible
    // control that accepts interaction but changes nothing.
    for (auto* d : { &osc1WaveDropdown, &osc2WaveDropdown })
    {
        d->addItem("Basic", 1);
        d->setSelectedId(1, false);
        addChildComponent(d);     // parented but not visible
    }

    setupLabel(xmodLabel, "Mode");
    xmodLabel.setJustificationType(juce::Justification::centred);
    populateXmodModes(xmodModeDropdown);
    addAndMakeVisible(xmodModeDropdown);

    syncButton.setClickingTogglesState(true);
    syncButton.setTooltip("Hard sync: osc1 wrap resets osc2 phase");
    addAndMakeVisible(syncButton);

    setupLabel(noiseTypeLabel, "Noise");
    populateNoiseTypes(noiseTypeDropdown);
    addAndMakeVisible(noiseTypeDropdown);

    setupLabel(fltTypeLabel, "Type");
    populateFilterTypes(fltTypeDropdown);
    addAndMakeVisible(fltTypeDropdown);

    // Cutoff / Resonance value formatting lives on the APVTS parameter (see
    // createParameterLayout) so the SliderAttachment can't clobber it. The
    // cutoff value reads as a bare number (Hz / kHz), so carry the unit in the
    // knob label, switching it as the value crosses 1 kHz — matching mu-clid.
    fltCutKnob.onValueChanged = [this](double v)
    {
        fltCutKnob.setLabel(v < 1000.0 ? "Cutoff (Hz)" : "Cutoff (kHz)");
    };

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

    // ── Modulator panel ─────────────────────────────────────────────────────
    addAndMakeVisible(modulatorPanel);
    modulatorPanel.setDestProvider(&modDestProvider);

    rebindAttachments();
    refreshHeader();

    startTimerHz(30);
}

VoicePanel::~VoicePanel() { stopTimer(); }

void VoicePanel::timerCallback()
{
    // 2 bars = 8 beats in 4/4. Normalise the transport beat to 0..1 across the
    // gating grid + feed the modulator playhead.
    const bool   playing = proc.isInternalPlaying();
    const double beat    = proc.getInternalBeatPos();
    constexpr double patBeats = (double) GatePattern::kTotalBars * 4.0;
    const double beat01  = std::fmod(beat, patBeats) / patBeats;
    gatingDesigner.setPlayhead(beat01, playing);
    modulatorPanel.setPlayheadBeat(beat);
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
    xmodAttachment = nullptr; xmodModeAttachment = nullptr; syncAttachment = nullptr;
    osc1LevelAttachment  = nullptr; osc2LevelAttachment = nullptr;
    noiseLevelAttachment = nullptr; noiseTypeAttachment = nullptr;
    fltCutAttachment       = nullptr;
    fltResAttachment      = nullptr; fltEnvDepthAttachment = nullptr;
    levelAttachment       = nullptr;
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

    xmodAttachment     = std::make_unique<APVTS::SliderAttachment>  (apvts, id("xmod"),  xmodKnob.getSlider());
    xmodModeAttachment = std::make_unique<APVTS::ComboBoxAttachment>(apvts, id("xmode"), xmodModeDropdown.getComboBox());
    syncAttachment     = std::make_unique<APVTS::ButtonAttachment>  (apvts, id("sync"),  syncButton);

    osc1LevelAttachment  = std::make_unique<APVTS::SliderAttachment>  (apvts, id("o1_lvl"),     osc1LevelKnob.getSlider());
    osc2LevelAttachment  = std::make_unique<APVTS::SliderAttachment>  (apvts, id("o2_lvl"),     osc2LevelKnob.getSlider());
    noiseLevelAttachment = std::make_unique<APVTS::SliderAttachment>  (apvts, id("noise_lvl"),  noiseLevelKnob.getSlider());
    noiseTypeAttachment  = std::make_unique<APVTS::ComboBoxAttachment>(apvts, id("noise_type"), noiseTypeDropdown.getComboBox());

    // Filter type — manual wiring (item IDs ≠ sequential, so no ComboBoxAttachment).
    // Item ID = algorithm index + 1, matching the AudioParameterInt(0..15) stored value.
    {
        auto* fltParam = apvts.getParameter(id("flt_type"));
        const int algo = juce::jlimit(0, 15, (int) apvts.getRawParameterValue(id("flt_type"))->load());
        fltTypeDropdown.setSelectedId(algo + 1, juce::dontSendNotification);
        fltTypeDropdown.onChange = [fltParam](int itemId) {
            if (fltParam)
                fltParam->setValueNotifyingHost(fltParam->convertTo0to1((float)(itemId - 1)));
        };
    }
    fltCutAttachment      = std::make_unique<APVTS::SliderAttachment>  (apvts, id("flt_cut"),       fltCutKnob.getSlider());
    fltResAttachment      = std::make_unique<APVTS::SliderAttachment>  (apvts, id("flt_res"),       fltResKnob.getSlider());
    fltEnvDepthAttachment = std::make_unique<APVTS::SliderAttachment>  (apvts, id("flt_env_depth"), fltEnvDepthKnob.getSlider());

    levelAttachment = std::make_unique<APVTS::SliderAttachment>(apvts, id("level"), levelKnob.getSlider());
    gapAttachment        = std::make_unique<APVTS::SliderAttachment>(apvts, id("gate_gap"),     gatingDesigner.gapSlider);
    gateBypassAttachment = std::make_unique<APVTS::ButtonAttachment>(apvts, id("gate_bypass"), gatingDesigner.bypassButton);

    // Mirror the Gap value into the designer's render cache.
    gatingDesigner.setGap((float)(gatingDesigner.gapSlider.getValue() / 100.0));
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
    bind(xmodKnob,       "xmod",              mu_tant::kTantSnapXMod);
    bind(osc1LevelKnob,  "osc1.level",        mu_tant::kTantSnapOsc1Level);
    bind(osc2LevelKnob,  "osc2.level",        mu_tant::kTantSnapOsc2Level);
    bind(noiseLevelKnob, "noise.level",       mu_tant::kTantSnapNoiseLevel);
    bind(fltCutKnob,     "filter.cutoff",     mu_tant::kTantSnapFilterCutoff);
    bind(fltResKnob,     "filter.resonance",  mu_tant::kTantSnapFilterRes);
    bind(levelKnob,      "level",             mu_tant::kTantSnapLevel);
}

void VoicePanel::refreshHeader()
{
    headerBar.setLayerName(proc.getChannelName(currentVoice));
    headerBar.setColour(MuLookAndFeel::channelPalette[
        (size_t)(proc.getChannelColourIndex(currentVoice) % MuLookAndFeel::kChannelPaletteSize)]);
    refreshVoicePresetList();
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
    // Row B: X-Mod | Filter | Insert — height set by Insert subsection height so
    // Insert fits without modification. X-Mod and Filter controls are centred within it.
    const int rowBH = insTitleH + insSubH + s(6);   // 14+116+6 = 136

    // ── Row A: Osc 1 | Osc 2 | Noise (side by side, using full leftW) ───────
    // Osc panels extend across the Insert column zone since Insert is Row B only.
    {
        const int oscTitleW = s(36);
        const int waveW     = s(70);
        const int noiseW    = s(170);
        const int oscW      = (leftW - noiseW - 2 * gap) / 2;   // ≈ 342
        const int rowY      = contentTop + s(4);
        const int waveY     = rowY + (s2H - ddH) / 2;

        // Osc 1
        osc1PanelR = { pad, contentTop, oscW, rowAH };
        {
            int x = pad + oscTitleW;
            // Wavetable dropdown is hidden (addChildComponent, not addAndMakeVisible)
            // until the bank API lands. When made visible, setVisible(true) must be
            // followed by resized() so the knobs shift right and oscW must be verified
            // wide enough for 5 knobs + waveW + oscTitleW.
            osc1WaveDropdown.setBounds(x, waveY, waveW, ddH);
            if (osc1WaveDropdown.isVisible()) x += waveW + s(4);
            for (auto* k : { &o1OctKnob, &o1SemiKnob, &o1FineKnob, &o1PosKnob, &o1PenvDepthKnob })
            { k->setBounds(x, rowY, s2W, s2H);  x += s2W + s(2); }
        }

        // Osc 2
        const int osc2X = pad + oscW + gap;
        osc2PanelR = { osc2X, contentTop, oscW, rowAH };
        {
            int x = osc2X + oscTitleW;
            osc2WaveDropdown.setBounds(x, waveY, waveW, ddH);  // hidden until bank lands (see Osc 1 comment)
            if (osc2WaveDropdown.isVisible()) x += waveW + s(4);
            for (auto* k : { &o2OctKnob, &o2SemiKnob, &o2FineKnob, &o2PosKnob, &o2PenvDepthKnob })
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
        // Vertical centering helpers — knob and dropdown centres align to row midline.
        const int knobY = rowBY + (rowBH - s2H) / 2;
        const int ctrlY = rowBY + (rowBH - ddH)  / 2;

        // X-Mod panel
        const int xmodTitleW = s(30);
        const int xmodW      = s(200);
        modNoisePanelR = { pad, rowBY, xmodW, rowBH };
        {
            int x = pad + xmodTitleW;
            // Knob / Mode label / dropdown stacked vertically in one column,
            // Sync button centred to their right.
            const int colW    = s(72);   // dropdown sets column width; knob centred within it
            const int labelH  = s(14);
            const int stackH  = s2H + s(4) + labelH + ddH;   // 56+4+14+24 = 98
            const int stackY  = rowBY + (rowBH - stackH) / 2;
            xmodKnob.setBounds(x + (colW - s2W) / 2, stackY, s2W, s2H);
            xmodLabel.setBounds(x, stackY + s2H + s(4), colW, labelH);
            xmodModeDropdown.setBounds(x, stackY + s2H + s(4) + labelH, colW, ddH);
            x += colW + gap;
            syncButton.setBounds(x, rowBY + (rowBH - ddH) / 2, s(44), ddH);
        }

        // Filter panel (no Level knob)
        const int filterX     = pad + xmodW + gap;
        const int filterTitleW = s(30);
        const int filterW     = rowsW - xmodW - gap;   // remaining rowsW
        filterPanelR = { filterX, rowBY, filterW, rowBH };
        {
            int x = filterX + filterTitleW;
            fltTypeLabel.setBounds(x, ctrlY, s(28), ddH);          x += s(28) + s(2);
            fltTypeDropdown.setBounds(x, ctrlY, s(88), ddH);       x += s(88) + gap;
            fltCutKnob.setBounds(x, knobY, s2W, s2H);              x += s2W + s(2);
            fltResKnob.setBounds(x, knobY, s2W, s2H);              x += s2W + s(2);
            fltEnvDepthKnob.setBounds(x, knobY, s2W, s2H);
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
        const int gateH = s(24 + 22 + 80 + 56 + 4);   // 186
        gatingDesigner.setBounds(pad, y, w - 2 * pad, gateH);
        y += gateH + gap;
    }

    // ── Modulator panel — fills remaining height (minimum matches mu-clid) ───
    modulatorPanel.setBounds(pad, y, w - 2 * pad,
                             juce::jmax(s(332), h - y - pad));
}

} // namespace mu_tant
