#include "VoicePanel.h"
#include "Plugin/PluginProcessor.h"
#include "Audio/Scales.h"
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
        d.addItem("Sync", 3);
    }

    void populateFilterTypes(DropdownSelect& d)
    {
        // First-stab labels — same id span as the AudioParameterInt 0..15 range.
        const char* names[] = { "LP 12", "LP 24", "HP 12", "HP 24",
                                "BP 12", "BP 24", "Notch", "AP",
                                "Comb",  "1P LP", "1P HP", "Peak",
                                "Lo Shelf", "Hi Shelf", "—", "—" };
        for (int i = 0; i < 16; ++i)
            d.addItem(names[i], i + 1);
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
    for (auto* k : { &o1OctKnob, &o1SemiKnob, &o1FineKnob, &o1PosKnob,
                     &o2OctKnob, &o2SemiKnob, &o2FineKnob, &o2PosKnob,
                     &xmodKnob, &osc1LevelKnob, &osc2LevelKnob, &noiseLevelKnob,
                     &fltCutKnob, &fltResKnob, &levelKnob })
        addAndMakeVisible(k);

    // Placeholder wavetable selectors — one per oscillator, sitting to the left
    // of that oscillator's knobs. No engine wiring or APVTS binding yet; the
    // real wavetable bank lands later. Inert for now.
    for (auto* d : { &osc1WaveDropdown, &osc2WaveDropdown })
    {
        d->addItem("Basic", 1);   // single placeholder entry until the bank exists
        d->setSelectedId(1, false);
        addAndMakeVisible(d);
    }

    setupLabel(xmodLabel, "Mode");
    populateXmodModes(xmodModeDropdown);
    addAndMakeVisible(xmodModeDropdown);

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

    // ── Gating designer + Gap knob + Gater bypass ───────────────────────────
    addAndMakeVisible(gatingDesigner);
    addAndMakeVisible(gapKnob);
    // Gap is a 0..100 % integer param (the trailing fraction of each gate region
    // forced to silence). The designer renders in 0..1, so scale on the way in.
    gapKnob.onValueChanged = [this](double v) { gatingDesigner.setGap((float) (v / 100.0)); };

    // Gater bypass — when on, the gate stage is skipped so the raw drone passes
    // (audition / configuration). Per-voice APVTS toggle, rebound per voice.
    gateBypassButton.setClickingTogglesState(true);
    gateBypassButton.setTooltip("Bypass the gater so the drone passes through unmodulated");
    addAndMakeVisible(gateBypassButton);

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
    o1OctAttachment      = nullptr; o1SemiAttachment   = nullptr;
    o1FineAttachment     = nullptr; o1PosAttachment    = nullptr;
    o2OctAttachment      = nullptr; o2SemiAttachment   = nullptr;
    o2FineAttachment     = nullptr; o2PosAttachment    = nullptr;
    xmodAttachment       = nullptr; xmodModeAttachment = nullptr;
    osc1LevelAttachment  = nullptr; osc2LevelAttachment = nullptr;
    noiseLevelAttachment = nullptr; noiseTypeAttachment = nullptr;
    fltTypeAttachment    = nullptr; fltCutAttachment   = nullptr;
    fltResAttachment     = nullptr; levelAttachment    = nullptr;
    gapAttachment        = nullptr; gateBypassAttachment = nullptr;

    o1OctAttachment  = std::make_unique<APVTS::SliderAttachment>(apvts, id("o1_oct"),  o1OctKnob.getSlider());
    o1SemiAttachment = std::make_unique<APVTS::SliderAttachment>(apvts, id("o1_semi"), o1SemiKnob.getSlider());
    o1FineAttachment = std::make_unique<APVTS::SliderAttachment>(apvts, id("o1_fine"), o1FineKnob.getSlider());
    o1PosAttachment  = std::make_unique<APVTS::SliderAttachment>(apvts, id("o1_pos"),  o1PosKnob.getSlider());

    o2OctAttachment  = std::make_unique<APVTS::SliderAttachment>(apvts, id("o2_oct"),  o2OctKnob.getSlider());
    o2SemiAttachment = std::make_unique<APVTS::SliderAttachment>(apvts, id("o2_semi"), o2SemiKnob.getSlider());
    o2FineAttachment = std::make_unique<APVTS::SliderAttachment>(apvts, id("o2_fine"), o2FineKnob.getSlider());
    o2PosAttachment  = std::make_unique<APVTS::SliderAttachment>(apvts, id("o2_pos"),  o2PosKnob.getSlider());

    xmodAttachment     = std::make_unique<APVTS::SliderAttachment>  (apvts, id("xmod"),  xmodKnob.getSlider());
    xmodModeAttachment = std::make_unique<APVTS::ComboBoxAttachment>(apvts, id("xmode"), xmodModeDropdown.getComboBox());

    osc1LevelAttachment  = std::make_unique<APVTS::SliderAttachment>  (apvts, id("o1_lvl"),     osc1LevelKnob.getSlider());
    osc2LevelAttachment  = std::make_unique<APVTS::SliderAttachment>  (apvts, id("o2_lvl"),     osc2LevelKnob.getSlider());
    noiseLevelAttachment = std::make_unique<APVTS::SliderAttachment>  (apvts, id("noise_lvl"),  noiseLevelKnob.getSlider());
    noiseTypeAttachment  = std::make_unique<APVTS::ComboBoxAttachment>(apvts, id("noise_type"), noiseTypeDropdown.getComboBox());

    fltTypeAttachment = std::make_unique<APVTS::ComboBoxAttachment>(apvts, id("flt_type"), fltTypeDropdown.getComboBox());
    fltCutAttachment  = std::make_unique<APVTS::SliderAttachment>  (apvts, id("flt_cut"),  fltCutKnob.getSlider());
    fltResAttachment  = std::make_unique<APVTS::SliderAttachment>  (apvts, id("flt_res"),  fltResKnob.getSlider());

    levelAttachment = std::make_unique<APVTS::SliderAttachment>(apvts, id("level"), levelKnob.getSlider());
    gapAttachment   = std::make_unique<APVTS::SliderAttachment>(apvts, id("gate_gap"), gapKnob.getSlider());
    gateBypassAttachment = std::make_unique<APVTS::ButtonAttachment>(apvts, id("gate_bypass"), gateBypassButton);

    // Sync the designer's render-only Gap mirror to the freshly-bound value (0..100 % → 0..1).
    gatingDesigner.setGap((float) (gapKnob.getValue() / 100.0));
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

    // mu-clid panel styling: each sub-panel gets a 2px rounded border in the
    // per-voice palette colour, with a small uppercase muted title on the left.
    auto panel = [&](const juce::Rectangle<int>& r, const juce::String& title)
    {
        if (r.isEmpty()) return;
        g.setColour(voiceCol);
        g.drawRoundedRectangle(r.toFloat().reduced(1.0f), sf(6.0f), sf(2.0f));
        g.setColour(muted);
        g.drawText(title, r.getX() + s(8), r.getY(), s(40), r.getHeight(),
                   juce::Justification::centredLeft, false);
    };
    panel(osc1PanelR,     "OSC 1");
    panel(osc2PanelR,     "OSC 2");
    panel(modNoisePanelR, "X-MOD");
    panel(filterPanelR,   "FILTER");

    // Insert sub-panel: same rounded-border + centred title as the MIXER column.
    if (! insertPanelR.isEmpty())
    {
        g.setColour(voiceCol);
        g.drawRoundedRectangle(insertPanelR.toFloat().reduced(1.0f), sf(6.0f), sf(2.0f));
        g.setColour(muted);
        g.drawText("INSERT", insertPanelR.getX(), insertPanelR.getY() + s(3),
                   insertPanelR.getWidth(), s(12), juce::Justification::centred, false);
    }

    // NOISE + MIXER panels (right column): same centred-title styling.
    for (const auto& p : { std::make_pair(&noisePanelR, "NOISE"),
                           std::make_pair(&mixerPanelR, "MIXER") })
    {
        if (p.first->isEmpty()) continue;
        g.setColour(voiceCol);
        g.drawRoundedRectangle(p.first->toFloat().reduced(1.0f), sf(6.0f), sf(2.0f));
        g.setColour(muted);
        g.drawText(p.second, p.first->getX(), p.first->getY() + s(3),
                   p.first->getWidth(), s(12), juce::Justification::centred, false);
    }
}

void VoicePanel::resized()
{
    using mu_ui::s;
    const int w = getWidth();
    const int h = getHeight();

    const int pad      = s(12);
    const int gap      = s(8);
    const int ddH      = s(24);
    const int s2W      = s(MuLookAndFeel::kKnobSize2W);   // 54 — voice/filter/level knobs
    const int s2H      = s(MuLookAndFeel::kKnobSize2H);   // 56
    const int oscTitleW = s(46);                          // left title column inside each sub-panel
    const int modPanelH = s(260);

    // ── Shared header bar (full width) + shared Root / Scale row beneath ────
    const int barH = s(ChannelHeaderBar::kHeight);
    headerBar.setBounds(0, 0, w, barH);
    const int tonalY = barH + s(2);
    {
        const int labelW = s(44);
        int x = pad;
        rootLabel    .setBounds(x, tonalY, labelW, ddH);   x += labelW + s(4);
        rootDropdown .setBounds(x, tonalY, s(56), ddH);    x += s(56) + s(12);
        scaleLabel   .setBounds(x, tonalY, labelW, ddH);   x += labelW + s(4);
        scaleDropdown.setBounds(x, tonalY, s(100), ddH);
    }

    // ── Right-hand column (NOISE panel + horizontal MIXER row beneath) ───────
    // Wide enough for 3 Size-2 level knobs side by side in the MIXER row.
    const int mixerW     = s(3 * MuLookAndFeel::kKnobSize2W + 24);
    const int mixerX     = w - pad - mixerW;
    const int contentTop = tonalY + ddH + gap;

    // Left content region (everything except the mixer column).
    const int leftX     = pad;
    const int leftRight = mixerX - gap;
    const int leftW     = leftRight - leftX;

    int y = contentTop;

    // ── Osc 1 / Osc 2 horizontal sub-panels: [title | Wave | Oct Semi Fine Pos] ──
    const int oscSubH = s2H + s(8);
    const int waveW   = s(108);

    auto layoutOsc = [&](juce::Rectangle<int>& panelR, DropdownSelect& wave,
                         KnobWithLabel& kOct, KnobWithLabel& kSemi,
                         KnobWithLabel& kFine, KnobWithLabel& kPos)
    {
        panelR = { leftX, y, leftW, oscSubH };
        const int rowY = y + s(4);
        int x = leftX + oscTitleW;
        wave.setBounds(x, rowY + (s2H - ddH) / 2, waveW, ddH);
        x += waveW + gap;
        for (auto* k : { &kOct, &kSemi, &kFine, &kPos })
        {
            k->setBounds(x, rowY, s2W, s2H);
            x += s2W + s(2);
        }
        y += oscSubH + gap;
    };
    layoutOsc(osc1PanelR, osc1WaveDropdown, o1OctKnob, o1SemiKnob, o1FineKnob, o1PosKnob);
    layoutOsc(osc2PanelR, osc2WaveDropdown, o2OctKnob, o2SemiKnob, o2FineKnob, o2PosKnob);
    const int oscRowsBottom = y - gap;   // bottom of the Osc 2 sub-panel

    // ── Insert column — reserved on the right, spanning the X-Mod + Filter rows.
    //    Family signal flow: synth engine → insert → mixer. The shared
    //    InsertSubsection lays out an algo dropdown over a 4-knob row, so it needs
    //    a 2-row-tall column; placing it beside the two single-height rows keeps
    //    parity with mu-clid's Filter | Insert strip and adds no panel height. ──
    const int insSubW   = s(4 * MuLookAndFeel::kKnobSize2W);
    const int insSubH   = s(2 * MuLookAndFeel::kKnobSize2H + MuLookAndFeel::kVoiceGap);
    const int insTitleH = s(14);
    const int insColW   = insSubW + s(20);
    const int insColX   = leftRight - insColW;
    const int rowsRight = insColX - gap;     // X-Mod / Filter rows stop before the insert
    const int rowsW     = rowsRight - leftX;
    const int xmodFilterTop = y;

    // ── X-Mod / Noise sub-panel (mode + noise dropdowns kept apart) ─────────
    {
        const int rowH = s2H + s(8);
        modNoisePanelR = { leftX, y, rowsW, rowH };
        const int rowY  = y + s(4);
        const int ctrlY = rowY + (s2H - ddH) / 2;
        int x = leftX + oscTitleW;
        xmodKnob.setBounds(x, rowY, s2W, s2H);                 x += s2W + gap;
        xmodLabel.setBounds(x, ctrlY, s(40), ddH);             x += s(40) + s(2);
        xmodModeDropdown.setBounds(x, ctrlY, s(78), ddH);
        // Noise type moved to its own NOISE panel (top-right); see below.
        y += rowH + gap;
    }

    // ── Filter sub-panel (Type + Cutoff + Resonance) + per-voice Level ──────
    {
        const int rowH = s2H + s(8);
        filterPanelR = { leftX, y, rowsW, rowH };
        const int rowY  = y + s(4);
        const int ctrlY = rowY + (s2H - ddH) / 2;
        int x = leftX + oscTitleW;
        fltTypeLabel.setBounds(x, ctrlY, s(36), ddH);          x += s(36) + s(2);
        fltTypeDropdown.setBounds(x, ctrlY, s(96), ddH);       x += s(96) + gap;
        fltCutKnob.setBounds(x, rowY, s2W, s2H);               x += s2W + s(2);
        fltResKnob.setBounds(x, rowY, s2W, s2H);
        levelKnob.setBounds(rowsRight - s2W - s(6), rowY, s2W, s2H);
        y += rowH + gap;
    }

    // ── Insert sub-panel: title band + centred InsertSubsection, spanning both rows.
    {
        const int insBottom = y - gap;       // bottom of the Filter row above
        insertPanelR = { insColX, xmodFilterTop, insColW, insBottom - xmodFilterTop };
        const int subX = insColX + (insColW - insSubW) / 2;
        const int subY = xmodFilterTop + insTitleH
                       + (insertPanelR.getHeight() - insTitleH - insSubH) / 2;
        insertSub.setBounds(subX, subY, insSubW, insSubH);
    }

    // ── Gating designer + Gap (Size 2) + Gater bypass ───────────────────────
    {
        const int gateH     = s(112);
        const int rightColW = s2W + s(8);
        const int rightColX = leftRight - rightColW;
        gateBypassButton.setBounds(rightColX, y, rightColW, ddH);
        gapKnob.setBounds(rightColX + (rightColW - s2W) / 2, y + ddH + s(4), s2W, s2H);
        gatingDesigner.setBounds(leftX, y, rightColX - gap - leftX, gateH);
        y += gateH + gap;
    }
    // ── Right column: NOISE panel (top, room to grow) + MIXER row beneath ───
    // NOISE holds the noise-source config (type now; more later). MIXER is the
    // 3 source levels (Osc 1 / Osc 2 / Noise) as a horizontal Size-2 knob row.
    const int titleBand   = s(14);
    const int noisePanelH = titleBand + ddH + s(14);     // title + dropdown row + grow room
    noisePanelR = { mixerX, contentTop, mixerW, noisePanelH };
    {
        const int innerX = mixerX + s(8);
        const int innerW = mixerW - s(16);
        noiseTypeLabel.setBounds(0, 0, 0, 0);            // hidden — the panel title says NOISE
        noiseTypeDropdown.setBounds(innerX, contentTop + titleBand + s(2), innerW, ddH);
    }

    const int mixerY = contentTop + noisePanelH + gap;
    mixerPanelR = { mixerX, mixerY, mixerW, oscRowsBottom - mixerY };
    {
        const int knobsY = mixerY + titleBand + s(2);
        const int cellW  = mixerW / 3;
        int cx = mixerX;
        for (auto* k : { &osc1LevelKnob, &osc2LevelKnob, &noiseLevelKnob })
        {
            k->setBounds(cx + (cellW - s2W) / 2, knobsY, s2W, s2H);
            cx += cellW;
        }
    }

    // ── Modulator panel — full width, fills the remaining bottom space ──────
    const int modY = y;
    modulatorPanel.setBounds(pad, modY, w - 2 * pad, juce::jmax(modPanelH, h - modY - pad));
}

} // namespace mu_tant
