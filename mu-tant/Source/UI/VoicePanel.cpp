#include "VoicePanel.h"
#include "Plugin/PluginProcessor.h"
#include "Audio/Scales.h"
#include "UI/Components/MuLookAndFeel.h"

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

    // ── Voice tag (top header strip) ────────────────────────────────────────
    voiceTag.setText("Voice 1", juce::dontSendNotification);
    voiceTag.setJustificationType(juce::Justification::centredLeft);
    voiceTag.setFont(juce::Font(juce::FontOptions{}.withHeight(14.0f)));
    addAndMakeVisible(voiceTag);

    deleteVoiceButton.setTooltip("Delete this voice");
    deleteVoiceButton.onClick = [this] { if (onDeleteVoice) onDeleteVoice(); };
    addAndMakeVisible(deleteVoiceButton);

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

    // ── Modulator panel ─────────────────────────────────────────────────────
    addAndMakeVisible(modulatorPanel);
    modulatorPanel.setDestProvider(&modDestProvider);

    rebindAttachments();
    refreshVoiceTag();

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
        refreshVoiceTag();
    }

    modulatorPanel.setVoiceSlot(&proc.voiceSlots[(size_t) currentVoice]);
    gatingDesigner.setPattern(&proc.gatePatterns[(size_t) currentVoice]);
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

void VoicePanel::refreshVoiceTag()
{
    voiceTag.setText(juce::String("Voice ") + juce::String(currentVoice + 1),
                     juce::dontSendNotification);
    const auto tagCol = MuLookAndFeel::channelPalette[
        (size_t)(currentVoice % MuLookAndFeel::kChannelPaletteSize)];
    voiceTag.setColour(juce::Label::textColourId, tagCol);
}

void VoicePanel::paint(juce::Graphics& g)
{
    using Id = MuLookAndFeel::ColourIds;
    using mu_ui::s;
    using mu_ui::sf;
    g.fillAll(MuLookAndFeel::colour(Id::windowBackground));

    // Header strip divider.
    g.setColour(MuLookAndFeel::colour(Id::segmentInactiveBorder));
    g.fillRect(0, s(32) - 1, getWidth(), 1);

    const auto voiceCol = MuLookAndFeel::channelPalette[
        (size_t)(currentVoice % MuLookAndFeel::kChannelPaletteSize)];
    const auto border = MuLookAndFeel::colour(Id::segmentInactiveBorder);
    const auto muted  = MuLookAndFeel::colour(Id::mutedText);

    g.setFont(juce::Font(juce::FontOptions{}.withHeight(sf(10.0f))));

    // Horizontal sub-panels carry a vertically-centred title on the left; the
    // mixer (a tall column) carries its title across the top. The oscillator +
    // mixer titles take the per-voice palette colour (mu-clid's per-channel
    // accent logic); the auxiliary sections stay muted-grey.
    auto leftTitle = [&](const juce::Rectangle<int>& r, const juce::String& t, juce::Colour c)
    {
        if (r.isEmpty()) return;
        g.setColour(border); g.drawRect(r, 1);
        g.setColour(c);
        g.drawText(t, r.getX() + s(6), r.getY(), s(40), r.getHeight(),
                   juce::Justification::centredLeft, false);
    };
    leftTitle(osc1PanelR,     "Osc 1",  voiceCol);
    leftTitle(osc2PanelR,     "Osc 2",  voiceCol);
    leftTitle(modNoisePanelR, "X-Mod",  muted);
    leftTitle(filterPanelR,   "Filter", muted);

    if (! mixerPanelR.isEmpty())
    {
        g.setColour(border); g.drawRect(mixerPanelR, 1);
        g.setColour(voiceCol);
        g.drawText("Mixer", mixerPanelR.getX(), mixerPanelR.getY() + s(3),
                   mixerPanelR.getWidth(), s(12), juce::Justification::centred, false);
    }
}

void VoicePanel::resized()
{
    using mu_ui::s;
    const int w = getWidth();
    const int h = getHeight();

    const int pad      = s(12);
    const int headerH  = s(32);
    const int gap      = s(8);
    const int ddH      = s(24);
    const int s2W      = s(MuLookAndFeel::kKnobSize2W);   // 54 — voice/filter/level knobs
    const int s2H      = s(MuLookAndFeel::kKnobSize2H);   // 56
    const int oscTitleW = s(46);                          // left title column inside each sub-panel
    const int modPanelH = s(260);

    // ── Header strip (voice tag + shared Root / Scale + Delete) ─────────────
    voiceTag.setBounds(pad, 0, w / 3 - pad, headerH);
    {
        const int delW = s(56);
        deleteVoiceButton.setBounds(w - pad - delW, (headerH - s(20)) / 2, delW, s(20));

        const int labelW = s(44);
        int x = w / 3;
        const int yy = (headerH - ddH) / 2;
        rootLabel    .setBounds(x, yy, labelW, ddH);   x += labelW + s(4);
        rootDropdown .setBounds(x, yy, s(56), ddH);    x += s(56) + s(12);
        scaleLabel   .setBounds(x, yy, labelW, ddH);   x += labelW + s(4);
        scaleDropdown.setBounds(x, yy, s(100), ddH);
    }

    // ── Right-hand Mixer column ──────────────────────────────────────────────
    const int mixerW     = s(88);
    const int mixerX     = w - pad - mixerW;
    const int contentTop = headerH + gap;

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

    // ── X-Mod / Noise sub-panel (mode + noise dropdowns kept apart) ─────────
    {
        const int rowH = s2H + s(8);
        modNoisePanelR = { leftX, y, leftW, rowH };
        const int rowY  = y + s(4);
        const int ctrlY = rowY + (s2H - ddH) / 2;
        int x = leftX + oscTitleW;
        xmodKnob.setBounds(x, rowY, s2W, s2H);                 x += s2W + gap;
        xmodLabel.setBounds(x, ctrlY, s(40), ddH);             x += s(40) + s(2);
        xmodModeDropdown.setBounds(x, ctrlY, s(78), ddH);      x += s(78) + s(40);
        noiseTypeLabel.setBounds(x, ctrlY, s(44), ddH);        x += s(44) + s(2);
        noiseTypeDropdown.setBounds(x, ctrlY, s(88), ddH);
        y += rowH + gap;
    }

    // ── Filter sub-panel (Type + Cutoff + Resonance) + per-voice Level ──────
    {
        const int rowH = s2H + s(8);
        filterPanelR = { leftX, y, leftW, rowH };
        const int rowY  = y + s(4);
        const int ctrlY = rowY + (s2H - ddH) / 2;
        int x = leftX + oscTitleW;
        fltTypeLabel.setBounds(x, ctrlY, s(36), ddH);          x += s(36) + s(2);
        fltTypeDropdown.setBounds(x, ctrlY, s(96), ddH);       x += s(96) + gap;
        fltCutKnob.setBounds(x, rowY, s2W, s2H);               x += s2W + s(2);
        fltResKnob.setBounds(x, rowY, s2W, s2H);
        levelKnob.setBounds(leftRight - s2W - s(6), rowY, s2W, s2H);
        y += rowH + gap;
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
    // ── Mixer column — sits beside the two Osc rows (same height) ───────────
    // 3 level knobs (Osc 1 / Osc 2 / Noise) stacked to fill that height.
    mixerPanelR = { mixerX, contentTop, mixerW, oscRowsBottom - contentTop };
    {
        const int titleBand = s(14);
        const int cellW = mixerW - s(8);
        const int cx    = mixerX + s(4);
        const int slot  = (mixerPanelR.getHeight() - titleBand) / 3;
        int ky = contentTop + titleBand;
        for (auto* k : { &osc1LevelKnob, &osc2LevelKnob, &noiseLevelKnob })
        {
            k->setBounds(cx, ky, cellW, slot);
            ky += slot;
        }
    }

    // ── Modulator panel — full width, fills the remaining bottom space ──────
    const int modY = y;
    modulatorPanel.setBounds(pad, modY, w - 2 * pad, juce::jmax(modPanelH, h - modY - pad));
}

} // namespace mu_tant
