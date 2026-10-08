#include "EnginePanel.h"
#include "UI/SlotPresetHeader.h"   // mu-core: shared per-slot preset header wiring

namespace mu_toni
{

EnginePanel::EnginePanel(PluginProcessor& processor)
    : proc(processor), insertSub(processor, "v")
{
    addSourceControls();
    addVoiceControls();
    addArpControls();
    buildVoiceBand();
    setupHeaderAndModulators();
}

// The source section: Osc 1 / Osc 2 wavetables, X-Mod and the Mix levels.
void EnginePanel::addSourceControls()
{
    using LF = MuLookAndFeel;

    // ── Oscillators, X-Mod + Mix (mu-toni source section, boxed) ─────────
    addCombo(G_OSC1, "o1_wt",  waveItems());
    addKnob (G_OSC1, "o1o",    "Oct",  LF::knobEuclidean);
    addKnob (G_OSC1, "o1f",    "Fine", LF::knobEuclidean);
    addKnob (G_OSC1, "o1_pos", "Pos",  LF::knobEuclidean);
    addCombo(G_OSC2, "o2_wt",  waveItems());
    addKnob (G_OSC2, "o2o",    "Oct",  LF::knobEuclidean);
    addKnob (G_OSC2, "o2s",    "Semi", LF::knobEuclidean);
    addKnob (G_OSC2, "o2f",    "Fine", LF::knobEuclidean);
    addKnob (G_OSC2, "o2_pos", "Pos",  LF::knobEuclidean);
    // Cross-mod (Osc 2 → Osc 1): Lane A mode + Index, Sync, Feedback; Lane B mode + Depth, SSB shift.
    addCombo (G_XMOD, "xmod_phaseMode", mu_ui::choiceNames(proc.apvts, "v0_xmod_phaseMode"));
    addCombo (G_XMOD, "xmod_ampMode",   mu_ui::choiceNames(proc.apvts, "v0_xmod_ampMode"));
    addToggle(G_XMOD, "sync",           "Sync");
    addToggle(G_XMOD, "xmod_fdbk",      "Fdbk");
    addKnob  (G_XMOD, "xmod_index", "Index", LF::knobEuclidean);
    addKnob  (G_XMOD, "xmod_depth", "Depth", LF::knobEuclidean);
    addKnob  (G_XMOD, "xmod_ssb",   "Shift", LF::knobEuclidean);
    addCombo(G_MIX, "ntype", { "White", "Pink" });
    addKnob (G_MIX, "o1l",   "Osc 1", LF::knobLevel);
    addKnob (G_MIX, "o2l",   "Osc 2", LF::knobLevel);
    addKnob (G_MIX, "noise", "Noise", LF::knobLevel);
}

// The voice controls: pitch, filter and amp (its sends bind to the ch{N}_ mixer params).
void EnginePanel::addVoiceControls()
{
    using LF = MuLookAndFeel;

    // ── Pitch (target + envelope) ────────────────────────────────────────
    addCombo(G_PITCH, "ptgt", { "Osc 1+2", "Osc 2" });
    addKnob (G_PITCH, "peA", "A", LF::knobModulation);
    addKnob (G_PITCH, "peD", "D", LF::knobModulation);
    addKnob (G_PITCH, "peS", "S",  LF::knobModulation);
    addKnob (G_PITCH, "peR", "R", LF::knobModulation);
    addKnob (G_PITCH, "peDep", "Depth", LF::knobModulation);

    // ── Filter (type + drive/cutoff/reso/lowcut, envelope) ───────────────
    addCombo(G_FILTER, "ft", filterItems());
    addKnob (G_FILTER, "drv",   "Drive",   LF::knobPostPad);
    addKnob (G_FILTER, "cut",   "Cutoff",  LF::knobPostPad);
    addKnob (G_FILTER, "res",   "Reso",    LF::knobPostPad);
    addKnob (G_FILTER, "locut", "Low Cut", LF::knobPostPad);
    addKnob (G_FILTER, "feA", "A", LF::knobPostPad);
    addKnob (G_FILTER, "feD", "D", LF::knobPostPad);
    addKnob (G_FILTER, "feS", "S",  LF::knobPostPad);
    addKnob (G_FILTER, "feR", "R", LF::knobPostPad);
    addKnob (G_FILTER, "feDep", "Depth", LF::knobPostPad);

    // ── Amp (level + FX sends, envelope). Sends bind to ch{N}_ params. ───
    addKnob(G_AMP, "aeL",     "Level", LF::knobLevel);
    addKnob(G_AMP, "sendEff", "Eff",   LF::knobFxSend, "ch");
    addKnob(G_AMP, "sendDly", "Dly",   LF::knobFxSend, "ch");
    addKnob(G_AMP, "sendRev", "Rev",   LF::knobFxSend, "ch");
    addKnob(G_AMP, "aeA", "A", LF::knobLevel);
    addKnob(G_AMP, "aeD", "D", LF::knobLevel);
    addKnob(G_AMP, "aeS", "S",  LF::knobLevel);
    addKnob(G_AMP, "aeR", "R", LF::knobLevel);
}

// The Appergater (arpeggiator) controls.
void EnginePanel::addArpControls()
{
    using LF = MuLookAndFeel;

    // ── Appergater (arpeggiator, boxed) ──────────────────────────────────
    addCombo (G_ARP, "scale", scaleItems());
    addCombo (G_ARP, "chord", chordItems());
    addCombo (G_ARP, "rate",  rateItems());
    addCombo (G_ARP, "trig",  { "Loop", "MIDI" });
    addToggle(G_ARP, "leg",   "Legato");
    addToggle(G_ARP, "snap",  "Snap");
    addKnob  (G_ARP, "root",  "Root",      LF::knobEuclidean);
    addKnob  (G_ARP, "roct",  "Octave",    LF::knobEuclidean);
    addKnob  (G_ARP, "inv",   "Inversion", LF::knobEuclidean);
    addKnob  (G_ARP, "octs",  "Octaves",   LF::knobEuclidean);
    addKnob  (G_ARP, "dir",   "Direction", LF::knobEuclidean);
    addKnob  (G_ARP, "gate",  "Gate",      LF::knobEuclidean);
    addKnob  (G_ARP, "porta", "Glide",     LF::knobEuclidean);
    addKnob  (G_ARP, "acc",    "Accent",   LF::knobEuclidean);
    addKnob  (G_ARP, "accLen", "Acc Steps", LF::knobEuclidean);

    // The accent pattern: the shared step editor as on/off cells (two levels), one per step,
    // written into the layer's accPat bits.
    accentSteps.setUnipolar(true);
    accentSteps.setQuantization(2);
    accentSteps.onStepChanged = [this](int step, float value) { setAccentStep(step, value > 50.0f); };
    addAndMakeVisible(accentSteps);
}

// Turn accent step `step` on / off in the current layer's pattern.
void EnginePanel::setAccentStep(int step, bool on)
{
    if (step < 0 || step >= ArpAccent::kMaxSteps) return;
    auto* p = proc.apvts.getParameter("v" + juce::String(currentLayer) + "_accPat");
    if (p == nullptr) return;
    const int was = (int) p->convertFrom0to1(p->getValue());
    const int now = on ? (was | (1 << step)) : (was & ~(1 << step));
    if (now == was) return;
    p->beginChangeGesture();
    p->setValueNotifyingHost(p->convertTo0to1((float) now));
    p->endChangeGesture();
}

// Show the current layer's accent pattern (only when it changed) and where the arp is in it.
void EnginePanel::refreshAccentSteps()
{
    // Look the layer's parameters up once per layer, not every tick.
    if (accentParamsLayer != currentLayer)
    {
        const juce::String pre = "v" + juce::String(currentLayer) + "_";
        accLenParam  = proc.apvts.getRawParameterValue(pre + "accLen");
        accPatParam  = proc.apvts.getRawParameterValue(pre + "accPat");
        arpRateParam = proc.apvts.getRawParameterValue(pre + "rate");
        accentParamsLayer = currentLayer;
    }
    auto value = [](const std::atomic<float>* a) { return a != nullptr ? a->load() : 0.0f; };
    const int len = juce::jlimit(1, ArpAccent::kMaxSteps, (int) value(accLenParam));
    const int pat = (int) value(accPatParam);
    if (len != shownAccentLen || pat != shownAccentPat)
    {
        shownAccentLen = len;
        shownAccentPat = pat;
        std::vector<float> cells((size_t) len);
        for (int i = 0; i < len; ++i) cells[(size_t) i] = ((pat >> i) & 1) != 0 ? 100.0f : 0.0f;
        accentSteps.setSteps(cells);
    }
    // Loop mode: the arp's step follows the beat, so its place in the pattern does too.
    const long long step = (long long) std::floor(proc.getInternalBeatPos() / rateBeats((int) value(arpRateParam)));
    accentSteps.setPlayheadPhase((float) (((step % len) + len) % len) / (float) len);
    accentSteps.setBarColour(layerColour());
}

// The shared voice band (mu-Clid's layout), its sections built from the controls above.
void EnginePanel::buildVoiceBand()
{
    // ── Voice band: mu-Clid's layout, the sections built from the controls above ──
    // Pitch: target over two columns, Depth above R; A / D / S / R below.
    pitchBox.place(*findCombo("ptgt"), 0, 0, 2, true);
    pitchBox.place(*findKnob("peDep"), 0, 3);
    // Filter: type over two columns, then Drive / Cutoff / Reso / Low Cut; A / D / S / R / Depth below.
    filterBox.place(*findCombo("ft"), 0, 0, 2, true);
    // Amp: Level over A / D / S / R; the FX sends sit in the Effects box.
    ampBox.place(*findKnob("aeL"), 0, 0);
    {
        const char* const pitchEnv[]  = { "peA", "peD", "peS", "peR" };
        const char* const filterRow[] = { "drv", "cut", "res", "locut" };
        const char* const filterEnv[] = { "feA", "feD", "feS", "feR", "feDep" };
        const char* const ampEnv[]    = { "aeA", "aeD", "aeS", "aeR" };
        for (int i = 0; i < 4; ++i) pitchBox .place(*findKnob(pitchEnv[i]),  1, i);
        for (int i = 0; i < 4; ++i) filterBox.place(*findKnob(filterRow[i]), 0, i + 2);
        for (int i = 0; i < 5; ++i) filterBox.place(*findKnob(filterEnv[i]), 1, i);
        for (int i = 0; i < 4; ++i) ampBox   .place(*findKnob(ampEnv[i]),    1, i);
    }
    voiceBand.setSections(pitchBox, filterBox, ampBox, insertSub,
                          { findKnob("sendEff"), findKnob("sendDly"), findKnob("sendRev") });
    addAndMakeVisible(voiceBand);
    insertSub.onStatusUpdate = [this](const juce::String& n, const juce::String& val)
    { if (onStatusUpdate) onStatusUpdate(n, val); };

    addAndMakeVisible(modulatorPanel);
    modulatorPanel.setDestProvider(&modDestProvider);
}

// The shared per-layer header (fixed layers: reset + layer presets), the modulators and the first bind.
void EnginePanel::setupHeaderAndModulators()
{
    // ── Shared per-layer header: fixed layers (no rename / delete), reset + layer presets ──
    header.setShowDelete(false);
    header.setNameEditable(false);
    // Reset / preset load / save: the shared per-slot wiring (mu-core SlotPresetHeader).
    mu_ui::wireSlotPresetHeader(header, *this, proc,
        { "Layer",
          [this] { return currentLayer; },
          [this] { setLayer(currentLayer); },   // rebind so the modulators show the loaded state
          {} });
    addAndMakeVisible(header);
    refreshPresetList();

    setLayer(0);
    startTimerHz(mu_ui::kUiRefreshHz);
}

void EnginePanel::setLayer(int idx)
{
    currentLayer = juce::jmax(0, idx);
    const juce::String n = juce::String(currentLayer);

    // Drop every old binding first: a new attachment sets its control to the new layer's value,
    // and a still-live old one would write that value back into the previous layer.
    for (auto& k : knobs)   k.att.reset();
    for (auto& c : combos)  c.att.reset();
    for (auto& t : toggles) t.att.reset();

    for (auto& k : knobs)
        k.att = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
            proc.apvts, k.prefix + n + "_" + k.suffix, k.comp->getSlider());
    for (auto& c : combos)
        c.att = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
            proc.apvts, "v" + n + "_" + c.suffix, c.comp->getComboBox());
    for (auto& t : toggles)
        t.att = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
            proc.apvts, "v" + n + "_" + t.suffix, *t.comp);

    insertSub.setChannel(currentLayer);
    shownAccentLen = shownAccentPat = -1;   // redraw the new layer's accent pattern
    refreshAccentSteps();
    modulatorPanel.setVoiceSlot(&proc.voiceSlots[(size_t) currentLayer]);

    header.setLayerName(proc.getChannelName(currentLayer));
    header.setColour(layerColour());
    header.setSelectedPresetId(0);
    repaint();
}

// Rescan the layer-preset folder into the header's preset list (after a save).
void EnginePanel::refreshPresetList()
{
    mu_ui::refreshSlotPresetList(header, proc, currentLayer);
}

void EnginePanel::paintOverChildren(juce::Graphics& g)
{
    if (! MuLookAndFeel::hasScrews(*this)) return;
    for (auto r : { srcR, voicePanelR, arpPanelR, modR })
        MuLookAndFeel::drawPanelScrews(g, r.reduced(2).toFloat());
    MuLookAndFeel::drawStripScrews(g, headerR.reduced(2).toFloat());   // the thin preset strip
}

void EnginePanel::paint(juce::Graphics& g)
{
    using Id = MuLookAndFeel::ColourIds;
    using LF = MuLookAndFeel;
    using mu_ui::s;
    g.fillAll(MuLookAndFeel::colour(Id::panelBackground));

    // Metal style: the preset strip, source, voice band, Appergater and modulators each on
    // a metal panel (the header bar lights its displays in the layer colour); every section
    // in a raised box with its name plate above it.
    if (MuLookAndFeel::isMetal(*this))
    {
        const auto accent = MuLookAndFeel::appAccent(*this);
        for (auto r : { headerR, srcR, voicePanelR, arpPanelR, modR })
            MuLookAndFeel::drawAccentPanel(g, r.reduced(2).toFloat(), accent);
        // (the voice band draws its own section boxes)
        MuLookAndFeel::drawSections(g, *this, { { oscR[0], "OSCILLATOR 1" }, { oscR[1], "OSCILLATOR 2" },
                                                { oscR[2], "X-MOD" }, { oscR[3], "MIX" },
                                                { arpR, "APPERGATER" } }, accent);
        return;
    }

    // Boxed sections (Osc 1/2, X-Mod, Mix + Appergater).
    static const char* const boxTitles[] = { "Oscillator 1", "Oscillator 2", "X-Mod", "Mix" };
    g.setColour(MuLookAndFeel::colour(Id::segmentInactiveBorder));
    for (int i = 0; i < 4; ++i) g.drawRoundedRectangle(oscR[(size_t) i].toFloat().reduced(1.0f), 5.0f, 1.0f);
    g.drawRoundedRectangle(arpR.toFloat().reduced(1.0f), 5.0f, 1.0f);
    g.setColour(MuLookAndFeel::colour(Id::labelText));
    g.setFont(juce::Font(juce::FontOptions(12.0f, juce::Font::bold)));
    for (int i = 0; i < 4; ++i)
        g.drawText(boxTitles[i], oscR[(size_t) i].reduced(8, 4).removeFromTop(15), juce::Justification::topLeft, false);
    g.drawText("Appergater", arpR.reduced(8, 4).removeFromTop(15), juce::Justification::topLeft, false);

}

void EnginePanel::resized()
{
    if (MuLookAndFeel::isMetal(*this)) { layoutMetal(); return; }

    using LF = MuLookAndFeel;
    using mu_ui::s;
    const int pad = s(8);
    const int gap = MuLookAndFeel::isMetal(*this) ? s(10) : s(6);   // metal: room for the painted borders
    auto area = getLocalBounds().reduced(pad);
    area.removeFromBottom(pad);

    // Shared per-layer header bar on top (inside its own metal panel in the metal style).
    const bool metal = MuLookAndFeel::isMetal(*this);
    headerR = area.removeFromTop(s(ChannelHeaderBar::kHeight) + (metal ? s(4) : 0));
    header.setBounds(metal ? headerR.reduced(s(4), s(2)) : headerR);
    area.removeFromTop(gap);

    // Osc row (boxed): box pad + title + dropdown + gap + one Size-2 knob row +
    // a label gap so the knob label never touches the panel border, + box pad.
    const int oscRowH = s(kBoxPad + LF::kVoiceLabelH + kDropdownH + LF::kVoiceGap
                        + LF::kKnobSize2H + kLabelGap + kBoxPad);
    auto oscRow = area.removeFromTop(oscRowH);
    layoutSourceRow(oscRow, gap);
    area.removeFromTop(gap);

    // Voice band (the shared mu-Clid geometry).
    voiceR = area.removeFromTop(s(VoiceBand::kHeight));
    voiceBand.setBounds(voiceR);
    area.removeFromTop(gap);

    // Modulator at the bottom; Appergater fills the middle.
    const int modH = juce::jmax(s(190), area.getHeight() * 42 / 100);
    modulatorPanel.setBounds(area.removeFromBottom(modH));
    area.removeFromBottom(gap);
    arpR = area;

    for (int i = 0; i < 4; ++i) layoutBox(kSourceGroups[i], oscR[(size_t) i]);
    layoutBox(G_ARP,  arpR);
}

// Unscaled width a boxed section's controls need: its top row (dropdowns + toggles) or its
// knob row, whichever is wider (the widths layoutBox uses).
int EnginePanel::sourceContentW(int group) const
{
    int top = 0, row = 0;
    for (auto& c : combos)  if (c.group == group) top += kBoxComboW  + kBoxGap;
    for (auto& t : toggles) if (t.group == group) top += kBoxToggleW + kBoxGap;
    for (auto& k : knobs)   if (k.group == group) row += MuLookAndFeel::kKnobSize2W + MuLookAndFeel::kVoiceGap;
    return juce::jmax(top, row);
}

// The source row: Osc 1 | Osc 2 | X-Mod | Mix, each as wide as its controls need plus an
// equal share of what's left.
void EnginePanel::layoutSourceRow(juce::Rectangle<int> row, int gap)
{
    using mu_ui::s;
    const int pad = MuLookAndFeel::isMetal(*this) ? MuLookAndFeel::kSubPanelScrewClear : kBoxPad;
    int need[4], total = 0;
    for (int i = 0; i < 4; ++i) { need[i] = s(sourceContentW(kSourceGroups[i]) + 2 * pad); total += need[i]; }
    const int spare = juce::jmax(0, row.getWidth() - total - 3 * gap) / 4;
    for (int i = 0; i < 4; ++i)
    {
        oscR[(size_t) i] = (i == 3) ? row : row.removeFromLeft(need[i] + spare);
        if (i < 3) row.removeFromLeft(gap);
    }
}

// Metal style, as mu-Clid: panels edge to edge — preset strip, source (Osc 1 / Osc 2 / Mix),
// voice band, Appergater, modulators — content inset clear of the panels' corner screws,
// each section a raised box with its name plate in the band above it.
void EnginePanel::layoutMetal()
{
    using LF = MuLookAndFeel;
    using mu_ui::s;
    const int w = getWidth(), h = getHeight();
    const int padX   = s(LF::kScrewedPanelInset);   // content in from a panel's sides
    const int padY   = s(LF::kChannelInset);        //   … and from its top / bottom
    const int plateH = s(LF::kSectionPlateH);       // name plate band above each box
    const int boxGap = s(LF::kVoiceDivW);

    // Preset strip — with screws the header is narrower, leaving a screw at each end.
    headerR = { 0, 0, w, s(ChannelHeaderBar::kHeight) + s(4) };
    header.setBounds(headerR.reduced(LF::hasScrews(*this) ? padX : s(4), s(2)));

    // Source boxes and the Appergater box: a dropdown row over a Size-2 knob row.
    const int boxH = s(2 * kBoxPad + kDropdownH + LF::kVoiceGap + LF::kKnobSize2H + kLabelGap);

    // Source: Osc 1 | Osc 2 | X-Mod | Mix, each sized to its controls.
    srcR = { 0, headerR.getBottom(), w, padY + plateH + boxH + padY };
    layoutSourceRow({ padX, srcR.getY() + padY + plateH, w - 2 * padX, boxH }, boxGap);

    // Voice band (the shared mu-Clid band, inside the standard channel inset as in mu-Clid).
    voicePanelR = { 0, srcR.getBottom(), w, padY + s(VoiceBand::kHeight) + padY };
    voiceR      = voicePanelR.reduced(s(LF::kChannelInset));
    voiceBand.setBounds(voiceR);

    // Appergater, one box.
    arpPanelR = { 0, voicePanelR.getBottom(), w, padY + plateH + boxH + padY };
    arpR      = { padX, arpPanelR.getY() + padY + plateH, w - 2 * padX, boxH };

    // Modulators take the rest.
    modR = { 0, arpPanelR.getBottom(), w, juce::jmax(s(190) + 2 * padY, h - arpPanelR.getBottom()) };
    modulatorPanel.setBounds(modR.reduced(padX, padY));

    for (int i = 0; i < 4; ++i) layoutBox(kSourceGroups[i], oscR[(size_t) i]);
    layoutBox(G_ARP,  arpR);
}

juce::Colour EnginePanel::layerColour() const
{
    return MuLookAndFeel::channelPalette[(size_t) (proc.getChannelColourIndex(currentLayer)
                                                   % MuLookAndFeel::kChannelPaletteSize)];
}

// Boxed section (Osc 1/2/Mix, Appergater): dropdowns/toggles on top, Size-2 knobs flow.
void EnginePanel::layoutBox(int group, juce::Rectangle<int> rect)
{
    using LF = MuLookAndFeel;
    using mu_ui::s;
    // Metal style: the title sits on a plate above the box and content keeps clear of the
    // box's corner screws; flat: the title is drawn inside.
    const bool metal = MuLookAndFeel::isMetal(*this);
    auto inner = metal ? rect.reduced(s(LF::kSubPanelScrewClear), s(kBoxPad)) : rect.reduced(s(kBoxPad));
    inner.removeFromBottom(s(kLabelGap));   // keep control labels off the panel border
    if (! metal) inner.removeFromTop(s(LF::kVoiceLabelH));

    int nCombos = 0, nToggles = 0, nKnobs = 0;
    for (auto& c : combos)  if (c.group == group) ++nCombos;
    for (auto& t : toggles) if (t.group == group) ++nToggles;
    for (auto& k : knobs)   if (k.group == group) ++nKnobs;
    if (nCombos + nToggles > 0)
    {
        // A lone selector (e.g. the wavetable) stretches to the knob row so its name fits.
        const int comboW = (nCombos == 1 && nToggles == 0)
                         ? juce::jmax(kBoxComboW, nKnobs * (LF::kKnobSize2W + LF::kVoiceGap) - LF::kVoiceGap)
                         : kBoxComboW;
        auto top = inner.removeFromTop(s(kDropdownH));
        for (auto& c : combos)
            if (c.group == group) { c.comp->setBounds(top.removeFromLeft(s(comboW)).reduced(1)); top.removeFromLeft(s(kBoxGap)); }
        for (auto& t : toggles)
            if (t.group == group) { t.comp->setBounds(top.removeFromLeft(s(kBoxToggleW)).reduced(1)); top.removeFromLeft(s(kBoxGap)); }
        inner.removeFromTop(s(4));
    }

    const int kW = s(LF::kKnobSize2W), kH = s(LF::kKnobSize2H), gap = s(LF::kVoiceGap);
    int x = inner.getX(), y = inner.getY();
    for (auto& k : knobs)
        if (k.group == group)
        {
            if (x + kW > inner.getRight()) { x = inner.getX(); y += kH + gap; }
            k.comp->setBounds(x, y, kW, kH);
            x += kW + gap;
        }

    // The Appergater's accent pattern takes the rest of its knob row.
    if (group == G_ARP)
        accentSteps.setBounds(x + gap, y, juce::jmax(0, inner.getRight() - x - gap), kH - s(kLabelGap));
}

void EnginePanel::addCombo(int group, const char* suffix, const juce::StringArray& items)
{
    ComboDef d;
    d.comp = std::make_unique<DropdownSelect>();
    d.suffix = suffix; d.group = group;
    for (int i = 0; i < items.size(); ++i) d.comp->addItem(items[i], i + 1);
    addAndMakeVisible(*d.comp);
    combos.push_back(std::move(d));
}

void EnginePanel::addToggle(int group, const char* suffix, const juce::String& label)
{
    ToggleDef d;
    d.comp = std::make_unique<juce::ToggleButton>(label);
    d.suffix = suffix; d.group = group;
    addAndMakeVisible(*d.comp);
    toggles.push_back(std::move(d));
}

void EnginePanel::timerCallback()
{
    modulatorPanel.setPlayheadBeat(proc.getInternalBeatPos());
    refreshAccentSteps();
    header.setStagingBadge(proc.hasPendingSwap(currentLayer));   // "SWP" while a layer preset waits for the bar line
}

} // namespace mu_toni
