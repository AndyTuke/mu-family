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
#include "Modulation/MuToniModDest.h"
#include "Audio/Scales.h"
#include "Audio/Chords.h"
#include "Audio/AnalogueOsc.h"
#include <array>
#include <memory>
#include <vector>

namespace mu_toni
{

// μ-Toni engine panel. The shared per-layer header bar sits on top (layer presets + reset).
// The Pitch·Filter·Amp·Insert voice band replicates
// mu-clid's VoiceSection exactly — fixed MuLookAndFeel constants (Size-2 knobs,
// compact two-row sub-sections, section widths + dividers). Oscillator 1/2/Mix
// (mu-toni's two-osc source) sits above; the Appergater band + shared modulator
// section below. All sizes come from MuLookAndFeel — no arbitrary values.
class EnginePanel : public juce::Component,
                    private juce::Timer
{
public:
    enum Group { G_OSC1, G_OSC2, G_MIX, G_PITCH, G_FILTER, G_AMP, G_INSERT, G_ARP, G_COUNT };

    explicit EnginePanel(PluginProcessor& processor)
        : proc(processor), insertSub(processor, "v")
    {
        using LF = MuLookAndFeel;

        // ── Oscillators + Mix (mu-toni source section, boxed) ────────────────
        addCombo(G_OSC1, "o1w", waveItems());
        addKnob (G_OSC1, "o1o", "Oct",  LF::knobEuclidean);
        addKnob (G_OSC1, "o1f", "Fine", LF::knobEuclidean);
        addCombo(G_OSC2, "o2w", waveItems());
        addKnob (G_OSC2, "o2o", "Oct",  LF::knobEuclidean);
        addKnob (G_OSC2, "o2s", "Semi", LF::knobEuclidean);
        addKnob (G_OSC2, "o2f", "Fine", LF::knobEuclidean);
        addCombo(G_MIX, "ntype", { "White", "Pink" });
        addKnob (G_MIX, "o1l",   "Osc 1", LF::knobLevel);
        addKnob (G_MIX, "o2l",   "Osc 2", LF::knobLevel);
        addKnob (G_MIX, "noise", "Noise", LF::knobLevel);
        addKnob (G_MIX, "pw",    "PW",    LF::knobEuclidean);

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

        addAndMakeVisible(insertSub);
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

    void lookAndFeelChanged() override { resized(); repaint(); }   // metal style widens the gaps

    void paint(juce::Graphics& g) override
    {
        using Id = MuLookAndFeel::ColourIds;
        using LF = MuLookAndFeel;
        using mu_ui::s;
        g.fillAll(MuLookAndFeel::colour(Id::panelBackground));

        // Metal style: every boxed section a titled metal panel, the voice band and the
        // modulators on metal panels with name plates over the voice sections.
        if (MuLookAndFeel::isMetal(*this))
        {
            const auto accent = MuLookAndFeel::appAccent(*this);
            MuLookAndFeel::drawAccentPanel(g, headerR.reduced(2).toFloat(), layerColour());   // preset bar in the layer colour
            static const char* const plateTitles[] = { "OSCILLATOR 1", "OSCILLATOR 2", "MIX" };
            for (int i = 0; i < 3; ++i)
                MuLookAndFeel::drawTitledPanel(g, oscR[(size_t) i].toFloat().reduced(2.0f), plateTitles[i], accent);
            MuLookAndFeel::drawTitledPanel(g, arpR.toFloat().reduced(2.0f), "APPERGATER", accent);
            MuLookAndFeel::drawAccentPanel(g, voiceR.expanded(s(6), s(2)).toFloat(), accent);
            MuLookAndFeel::drawAccentPanel(g, modulatorPanel.getBounds().expanded(s(2)).toFloat(), accent);

            const int VX = voiceR.getX(), VY = voiceR.getY();
            const int vDivW = LF::kVoiceDivW;
            const int vFltX = LF::kVoicePitchW + vDivW;
            const int vAmpX = vFltX + LF::kVoiceFilterW + vDivW;
            const int vInsX = vAmpX + LF::kVoiceAmpW + vDivW;
            auto plate = [&](const char* t, int x, int w)
            { MuLookAndFeel::drawCentredNamePlate(g, { (float) (VX + s(x)), (float) VY, (float) s(w), (float) s(LF::kVoiceLabelH) }, t); };
            plate("PITCH",   0,     LF::kVoicePitchW);
            plate("FILTER",  vFltX, LF::kVoiceFilterW);
            plate("AMP",     vAmpX, LF::kVoiceAmpW);
            plate("EFFECTS", vInsX, LF::kVoiceInsertW);
            return;
        }

        // Boxed sections (Osc 1/2/Mix + Appergater).
        static const char* const boxTitles[] = { "Oscillator 1", "Oscillator 2", "Mix" };
        g.setColour(MuLookAndFeel::colour(Id::segmentInactiveBorder));
        for (int i = 0; i < 3; ++i) g.drawRoundedRectangle(oscR[(size_t) i].toFloat().reduced(1.0f), 5.0f, 1.0f);
        g.drawRoundedRectangle(arpR.toFloat().reduced(1.0f), 5.0f, 1.0f);
        g.setColour(MuLookAndFeel::colour(Id::labelText));
        g.setFont(juce::Font(juce::FontOptions(12.0f, juce::Font::bold)));
        for (int i = 0; i < 3; ++i)
            g.drawText(boxTitles[i], oscR[(size_t) i].reduced(8, 4).removeFromTop(15), juce::Justification::topLeft, false);
        g.drawText("Appergater", arpR.reduced(8, 4).removeFromTop(15), juce::Justification::topLeft, false);

        // Voice band — mu-clid style: centred section labels + 0.5px dividers.
        const int X = voiceR.getX(), Y = voiceR.getY();
        const int divW = LF::kVoiceDivW;
        const int fltX = LF::kVoicePitchW + divW;
        const int ampX = fltX + LF::kVoiceFilterW + divW;
        const int insX = ampX + LF::kVoiceAmpW + divW;
        g.setColour(MuLookAndFeel::colour(Id::segmentInactiveBorder));
        const float top = (float) Y + mu_ui::sf(6.0f), bot = (float) (Y + s(LF::kVoiceLabelH + LF::kVoiceSubH)) - mu_ui::sf(6.0f);
        for (int dx : { LF::kVoicePitchW + divW / 2, fltX + LF::kVoiceFilterW + divW / 2, ampX + LF::kVoiceAmpW + divW / 2 })
            g.drawLine((float) (X + s(dx)), top, (float) (X + s(dx)), bot, 0.5f);
        g.setColour(MuLookAndFeel::colour(Id::mutedText));
        g.setFont(juce::Font(juce::FontOptions{}.withHeight(mu_ui::sf(10.0f))));
        auto lbl = [&](const char* t, int x, int w)
        { g.drawText(t, X + s(x), Y, s(w), s(LF::kVoiceLabelH), juce::Justification::centred, false); };
        lbl("PITCH",  0,    LF::kVoicePitchW);
        lbl("FILTER", fltX, LF::kVoiceFilterW);
        lbl("AMP",    ampX, LF::kVoiceAmpW);
        lbl("INSERT", insX, LF::kVoiceInsertW);
    }

    void resized() override
    {
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
        int ow = oscRow.getWidth();
        oscR[0] = oscRow.removeFromLeft(ow * 33 / 100); oscRow.removeFromLeft(gap);
        oscR[1] = oscRow.removeFromLeft(ow * 33 / 100); oscRow.removeFromLeft(gap);
        oscR[2] = oscRow;
        area.removeFromTop(gap);

        // Voice band (fixed mu-clid geometry).
        voiceR = area.removeFromTop(s(LF::kVoiceLabelH + LF::kVoiceSubH));
        area.removeFromTop(gap);

        // Modulator at the bottom; Appergater fills the middle.
        const int modH = juce::jmax(s(190), area.getHeight() * 42 / 100);
        modulatorPanel.setBounds(area.removeFromBottom(modH));
        area.removeFromBottom(gap);
        arpR = area;

        layoutBox(G_OSC1, oscR[0]);
        layoutBox(G_OSC2, oscR[1]);
        layoutBox(G_MIX,  oscR[2]);
        layoutBox(G_ARP,  arpR);
        layoutVoiceBand(voiceR);
    }

private:
    // Layout constants (unscaled; wrap in mu_ui::s at use). kDropdownH is the
    // family-standard dropdown height; kLabelGap keeps a control label off the
    // panel border (design-ui-family §"Control label gap").
    static constexpr int kBoxPad    = MuLookAndFeel::kSpaceS;
    static constexpr int kDropdownH = 24;
    static constexpr int kLabelGap  = MuLookAndFeel::kSpaceS;

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

    // Voice band: exact mu-clid VoiceSection geometry (Size-2 knobs, 2 rows).
    void layoutVoiceBand(juce::Rectangle<int> rect)
    {
        using LF = MuLookAndFeel;
        using mu_ui::s;
        const int kW = LF::kKnobSize2W, rowH = LF::kKnobSize2H, fW = LF::kVoiceFilterColW;
        const int row2 = rowH + LF::kVoiceGap, divW = LF::kVoiceDivW;
        const int fltX = LF::kVoicePitchW + divW, ampX = fltX + LF::kVoiceFilterW + divW, insX = ampX + LF::kVoiceAmpW + divW;
        const int X = rect.getX(), Y = rect.getY() + s(LF::kVoiceLabelH);

        auto kb = [&](const char* suf, int sx, int col, int cw, bool bottom)
        { if (auto* k = findKnob(suf)) k->setBounds(X + s(sx + col * cw), Y + (bottom ? s(row2) : 0), s(cw), s(rowH)); };
        auto dd = [&](const char* suf, int sx, int spanCols, int cw)
        { if (auto* c = findCombo(suf)) c->setBounds(X + s(sx), Y + s(rowH / 4), s(spanCols * cw), s(rowH / 2)); };

        // Pitch — target (row 1) + A/D/S/R/Depth (row 2).
        dd("ptgt", 0, 2, kW);
        kb("peA", 0, 0, kW, true); kb("peD", 0, 1, kW, true); kb("peS", 0, 2, kW, true); kb("peR", 0, 3, kW, true); kb("peDep", 0, 4, kW, true);

        // Filter — type (2 cols) + Drive/Cutoff/Reso/LowCut (row 1) + A/D/S/R/Depth (row 2).
        dd("ft", fltX, 2, fW);
        kb("drv", fltX, 2, fW, false); kb("cut", fltX, 3, fW, false); kb("res", fltX, 4, fW, false); kb("locut", fltX, 5, fW, false);
        kb("feA", fltX, 0, fW, true); kb("feD", fltX, 1, fW, true); kb("feS", fltX, 2, fW, true); kb("feR", fltX, 3, fW, true); kb("feDep", fltX, 4, fW, true);

        // Amp — Level + Eff/Dly/Rev sends (row 1) + A/D/S/R (row 2).
        kb("aeL", ampX, 0, kW, false); kb("sendEff", ampX, 2, kW, false); kb("sendDly", ampX, 3, kW, false); kb("sendRev", ampX, 4, kW, false);
        kb("aeA", ampX, 0, kW, true); kb("aeD", ampX, 1, kW, true); kb("aeS", ampX, 2, kW, true); kb("aeR", ampX, 3, kW, true);

        // Insert — the shared subsection.
        insertSub.setBounds(X + s(insX), Y, s(LF::kVoiceInsertW), s(LF::kVoiceSubH));
    }

    // Boxed section (Osc 1/2/Mix, Appergater): dropdowns/toggles on top, Size-2 knobs flow.
    void layoutBox(int group, juce::Rectangle<int> rect)
    {
        using LF = MuLookAndFeel;
        using mu_ui::s;
        auto inner = rect.reduced(s(kBoxPad));
        inner.removeFromBottom(s(kLabelGap));   // keep control labels off the panel border
        inner.removeFromTop(s(LF::kVoiceLabelH));

        bool hasTop = false;
        for (auto& c : combos)  if (c.group == group) hasTop = true;
        for (auto& t : toggles) if (t.group == group) hasTop = true;
        if (hasTop)
        {
            auto top = inner.removeFromTop(s(kDropdownH));
            for (auto& c : combos)
                if (c.group == group) { c.comp->setBounds(top.removeFromLeft(s(108)).reduced(1)); top.removeFromLeft(s(4)); }
            for (auto& t : toggles)
                if (t.group == group) { t.comp->setBounds(top.removeFromLeft(s(64)).reduced(1)); top.removeFromLeft(s(4)); }
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
    static juce::StringArray waveItems() { return { "Sine", "Triangle", "Saw", "Square", "Pulse" }; }
    static juce::StringArray rateItems()
    { return { "1/4","1/4.","1/4T","1/8","1/8.","1/8T","1/16","1/16.","1/16T","1/32","1/32.","1/32T" }; }
    static juce::StringArray filterItems()
    { return { "LP12","HP12","BP12","Notch","LP24","HP24","BP24","LP6",
               "Comb+","AP12","Notch24","HP6","Peak","LoShf","HiShf","Comb-" }; }

    void timerCallback() override { modulatorPanel.setPlayheadBeat(proc.getInternalBeatPos()); }

public:
    std::function<void(const juce::String&, const juce::String&)> onStatusUpdate;

private:
    PluginProcessor& proc;
    int currentLayer = 0;
    std::vector<KnobDef>   knobs;
    std::vector<ComboDef>  combos;
    std::vector<ToggleDef> toggles;
    std::array<juce::Rectangle<int>, 3> oscR;
    juce::Rectangle<int> voiceR, arpR, headerR;

    ChannelHeaderBar header;

    InsertSubsection insertSub;
    ::ModulatorPanel modulatorPanel;
    ModDestProvider  modDestProvider = makeModDestProvider();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EnginePanel)
};

} // namespace mu_toni
