#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "Plugin/PluginProcessor.h"
#include "UI/Components/MuLookAndFeel.h"
#include "UI/Components/KnobWithLabel.h"
#include "UI/Components/DropdownSelect.h"
#include "UI/ModulatorPanel.h"
#include "UI/ChannelHeaderBar.h"          // mu-core: shared per-layer header (name / reset / presets / save)
#include "UI/ConfirmDialog.h"             // mu-core: shared confirm / name dialogs
#include "Persistence/PresetFiles.h"      // mu-core: listPresetFiles
#include "UI/Voice/InsertSubsection.h"
#include "UI/Voice/VoiceBand.h"            // mu-core: shared voice band (mu-Clid layout)
#include "Modulation/MuToniModDest.h"
#include "Audio/Scales.h"
#include "Audio/Chords.h"
#include "Audio/Wavetable/WavetableBank.h"   // mu-core: the wavetable names
#include <array>
#include <memory>
#include <vector>

namespace mu_toni
{

// μ-Toni engine panel. The shared per-layer header bar sits on top (layer presets + reset).
// The Pitch·Filter·Amp·Effects voice band is the shared mu-core VoiceBand (mu-Clid's layout),
// its sections built from this panel's controls. Oscillator 1/2/Mix (mu-toni's two-osc
// source) sits above; the Appergater band + shared modulator section below. All sizes come
// from MuLookAndFeel / VoiceBand — no arbitrary values.
class EnginePanel : public juce::Component,
                    private juce::Timer
{
public:
    enum Group { G_OSC1, G_OSC2, G_XMOD, G_MIX, G_PITCH, G_FILTER, G_AMP, G_INSERT, G_ARP, G_COUNT };

    explicit EnginePanel(PluginProcessor& processor)
        : proc(processor), insertSub(processor, "v")
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
        addCombo (G_XMOD, "xmod_phaseMode", { "FM", "PM", "TZFM" });
        addCombo (G_XMOD, "xmod_ampMode",   { "AM", "RM", "SSB" });
        addToggle(G_XMOD, "sync",           "Sync");
        addToggle(G_XMOD, "xmod_fdbk",      "Fdbk");
        addKnob  (G_XMOD, "xmod_index", "Index", LF::knobEuclidean);
        addKnob  (G_XMOD, "xmod_depth", "Depth", LF::knobEuclidean);
        addKnob  (G_XMOD, "xmod_ssb",   "Shift", LF::knobEuclidean);
        addCombo(G_MIX, "ntype", { "White", "Pink" });
        addKnob (G_MIX, "o1l",   "Osc 1", LF::knobLevel);
        addKnob (G_MIX, "o2l",   "Osc 2", LF::knobLevel);
        addKnob (G_MIX, "noise", "Noise", LF::knobLevel);

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

        // ── Shared per-layer header: fixed layers (no rename / delete), reset + layer presets ──
        header.setShowDelete(false);
        header.setNameEditable(false);
        header.onReset = [this]
        {
            mu_ui::confirmAsync(this, "Reset Layer",
                                "Reset \"" + proc.getChannelName(currentLayer) + "\" to defaults?\nThis cannot be undone.",
                                "Reset", [this] { proc.resetLayer(currentLayer); setLayer(currentLayer); });
        };
        header.onPresetFileChosen = [this](const juce::File& f)
        {
            proc.loadLayerPreset(currentLayer, f);
            setLayer(currentLayer);   // rebind so the modulators show the loaded state
            header.showPresetFile(f);
        };
        header.setSaveEnabled(proc.canSaveLayerPreset());   // demo: per-layer save disabled
        header.onSave = [this]
        {
            if (! proc.canSaveLayerPreset()) return;
            juce::Component::SafePointer<EnginePanel> safe(this);
            mu_ui::promptTextAsync(this, "Save Layer Preset", "Preset name:",
                                   proc.getChannelName(currentLayer), "Save",
                [safe](const juce::String& name)
                {
                    if (safe == nullptr || name.isEmpty()) return;
                    safe->proc.saveLayerPreset(safe->currentLayer, name);
                    safe->refreshPresetList();
                });
        };
        addAndMakeVisible(header);
        refreshPresetList();

        setLayer(0);
        startTimerHz(30);
    }

    ~EnginePanel() override
    {
        stopTimer();
        modulatorPanel.setVoiceSlot(nullptr);
    }

    void setLayer(int idx)
    {
        currentLayer = juce::jmax(0, idx);
        const juce::String n = juce::String(currentLayer);

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
        modulatorPanel.setVoiceSlot(&proc.voiceSlots[(size_t) currentLayer]);

        header.setLayerName(proc.getChannelName(currentLayer));
        header.setColour(layerColour());
        header.setSelectedPresetId(0);
        repaint();
    }

    int getLayer() const noexcept { return currentLayer; }

    // Rescan the layer-preset folder into the header's preset list (after a save).
    void refreshPresetList()
    {
        header.setPresetFiles(mu_pp::listPresetFiles(proc.getPerSlotPresetDir(), proc.getPerSlotPresetExtension()));
    }

    void lookAndFeelChanged() override { resized(); repaint(); }   // metal style / screws change the layout

    void paintOverChildren(juce::Graphics& g) override
    {
        if (! MuLookAndFeel::hasScrews(*this)) return;
        for (auto r : { srcR, voicePanelR, arpPanelR, modR })
            MuLookAndFeel::drawPanelScrews(g, r.reduced(2).toFloat());
        MuLookAndFeel::drawStripScrews(g, headerR.reduced(2).toFloat());   // the thin preset strip
    }

    void paint(juce::Graphics& g) override
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

    void resized() override
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

private:
    static constexpr Group kSourceGroups[4] = { G_OSC1, G_OSC2, G_XMOD, G_MIX };

    // Unscaled width a boxed section's controls need: its top row (dropdowns + toggles) or its
    // knob row, whichever is wider (the widths layoutBox uses).
    int sourceContentW(int group) const
    {
        int top = 0, row = 0;
        for (auto& c : combos)  if (c.group == group) top += kBoxComboW  + kBoxGap;
        for (auto& t : toggles) if (t.group == group) top += kBoxToggleW + kBoxGap;
        for (auto& k : knobs)   if (k.group == group) row += MuLookAndFeel::kKnobSize2W + MuLookAndFeel::kVoiceGap;
        return juce::jmax(top, row);
    }

    // The source row: Osc 1 | Osc 2 | X-Mod | Mix, each as wide as its controls need plus an
    // equal share of what's left.
    void layoutSourceRow(juce::Rectangle<int> row, int gap)
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
    void layoutMetal()
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

    // Layout constants (unscaled; wrap in mu_ui::s at use). kDropdownH is the
    // family-standard dropdown height; kLabelGap keeps a control label off the
    // panel border (design-ui-family §"Control label gap").
    static constexpr int kBoxPad    = MuLookAndFeel::kSpaceS;
    static constexpr int kDropdownH = 24;
    static constexpr int kLabelGap  = MuLookAndFeel::kSpaceS;
    // A boxed section's top row: dropdown / toggle widths and the gap after each.
    static constexpr int kBoxComboW  = 108;
    static constexpr int kBoxToggleW = 64;
    static constexpr int kBoxGap     = MuLookAndFeel::kSpaceXS;

    struct KnobDef   { std::unique_ptr<KnobWithLabel>  comp; juce::String suffix, prefix; int group;
                       std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>   att; };
    struct ComboDef  { std::unique_ptr<DropdownSelect> comp; juce::String suffix; int group;
                       std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> att; };
    struct ToggleDef { std::unique_ptr<juce::ToggleButton> comp; juce::String suffix; int group;
                       std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>   att; };

    juce::Colour layerColour() const
    {
        return MuLookAndFeel::channelPalette[(size_t) (proc.getChannelColourIndex(currentLayer)
                                                       % MuLookAndFeel::kChannelPaletteSize)];
    }

    KnobWithLabel*  findKnob (const char* s) { for (auto& k : knobs)  if (k.suffix == s) return k.comp.get(); return nullptr; }
    DropdownSelect* findCombo(const char* s) { for (auto& c : combos) if (c.suffix == s) return c.comp.get(); return nullptr; }

    // Boxed section (Osc 1/2/Mix, Appergater): dropdowns/toggles on top, Size-2 knobs flow.
    void layoutBox(int group, juce::Rectangle<int> rect)
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
    }

    void addKnob(int group, const char* suffix, const juce::String& label,
                 MuLookAndFeel::ColourIds colour, const char* prefix = "v")
    {
        KnobDef d;
        d.comp = std::make_unique<KnobWithLabel>(label, colour);
        d.suffix = suffix; d.prefix = prefix; d.group = group;
        d.comp->onStatusUpdate = [this](const juce::String& n, const juce::String& val)
        { if (onStatusUpdate) onStatusUpdate(n, val); };
        addAndMakeVisible(*d.comp);
        knobs.push_back(std::move(d));
    }

    void addCombo(int group, const char* suffix, const juce::StringArray& items)
    {
        ComboDef d;
        d.comp = std::make_unique<DropdownSelect>();
        d.suffix = suffix; d.group = group;
        for (int i = 0; i < items.size(); ++i) d.comp->addItem(items[i], i + 1);
        addAndMakeVisible(*d.comp);
        combos.push_back(std::move(d));
    }

    void addToggle(int group, const char* suffix, const juce::String& label)
    {
        ToggleDef d;
        d.comp = std::make_unique<juce::ToggleButton>(label);
        d.suffix = suffix; d.group = group;
        addAndMakeVisible(*d.comp);
        toggles.push_back(std::move(d));
    }

    static juce::StringArray scaleItems()
    { juce::StringArray a; for (int i = 0; i < kNumScales; ++i) a.add(kScales[(size_t) i].name); return a; }
    static juce::StringArray chordItems()
    { juce::StringArray a; for (int i = 0; i < kNumChords; ++i) a.add(kChords[(size_t) i].name); return a; }
    static juce::StringArray waveItems() { return mu_wavetable::WavetableBank::factoryTableNames(); }
    static juce::StringArray rateItems()
    { return { "1/4","1/4.","1/4T","1/8","1/8.","1/8T","1/16","1/16.","1/16T","1/32","1/32.","1/32T" }; }
    static juce::StringArray filterItems()
    { return { "LP12","HP12","BP12","Notch","LP24","HP24","BP24","LP6",
               "Comb+","AP12","Notch24","HP6","Peak","LoShf","HiShf","Comb-" }; }

    void timerCallback() override
    {
        modulatorPanel.setPlayheadBeat(proc.getInternalBeatPos());
        header.setStagingBadge(proc.hasPendingSwap(currentLayer));   // "SWP" while a layer preset waits for the bar line
    }

public:
    std::function<void(const juce::String&, const juce::String&)> onStatusUpdate;

private:
    PluginProcessor& proc;
    int currentLayer = 0;
    std::vector<KnobDef>   knobs;
    std::vector<ComboDef>  combos;
    std::vector<ToggleDef> toggles;
    std::array<juce::Rectangle<int>, 4> oscR;   // Osc 1, Osc 2, X-Mod, Mix
    juce::Rectangle<int> voiceR, arpR, headerR;
    // Metal style: the panels.
    juce::Rectangle<int> srcR, voicePanelR, arpPanelR, modR;

    ChannelHeaderBar header;

    InsertSubsection insertSub;
    // The voice band and its Pitch / Filter / Amp sections (built from the controls above).
    VoiceBandSection pitchBox  { 4, VoiceBand::kCols };
    VoiceBandSection filterBox { 6, VoiceBand::kFilterColW };
    VoiceBandSection ampBox    { 4, VoiceBand::kCols };
    VoiceBand        voiceBand;
    ::ModulatorPanel modulatorPanel;
    ModDestProvider  modDestProvider = makeModDestProvider();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EnginePanel)
};

} // namespace mu_toni
