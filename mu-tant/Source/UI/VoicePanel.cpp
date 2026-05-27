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
    for (auto* k : { &o1OctKnob, &o1ToneKnob, &o1FineKnob, &o1PosKnob,
                     &o2OctKnob, &o2ToneKnob, &o2FineKnob, &o2PosKnob,
                     &xmodKnob, &mixKnob, &fltCutKnob, &fltResKnob, &levelKnob })
        addAndMakeVisible(k);

    setupLabel(xmodLabel, "Mode");
    populateXmodModes(xmodModeDropdown);
    addAndMakeVisible(xmodModeDropdown);

    setupLabel(fltTypeLabel, "Type");
    populateFilterTypes(fltTypeDropdown);
    addAndMakeVisible(fltTypeDropdown);

    // ── Gating designer ─────────────────────────────────────────────────────
    addAndMakeVisible(gatingDesigner);

    // ── Modulator panel ─────────────────────────────────────────────────────
    addAndMakeVisible(modulatorPanel);
    modulatorPanel.setDestProvider(&modDestProvider);

    rebindAttachments();
    refreshVoiceTag();
}

VoicePanel::~VoicePanel() = default;

void VoicePanel::setVoice(int voiceIndex)
{
    voiceIndex = juce::jlimit(0, PluginProcessor::kMaxVoices - 1, voiceIndex);
    if (voiceIndex == currentVoice && o1OctAttachment != nullptr) return;
    currentVoice = voiceIndex;
    rebindAttachments();
    refreshVoiceTag();
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
    o1OctAttachment      = nullptr; o1ToneAttachment   = nullptr;
    o1FineAttachment     = nullptr; o1PosAttachment    = nullptr;
    o2OctAttachment      = nullptr; o2ToneAttachment   = nullptr;
    o2FineAttachment     = nullptr; o2PosAttachment    = nullptr;
    xmodAttachment       = nullptr; xmodModeAttachment = nullptr;
    mixAttachment        = nullptr;
    fltTypeAttachment    = nullptr; fltCutAttachment   = nullptr;
    fltResAttachment     = nullptr; levelAttachment    = nullptr;

    o1OctAttachment  = std::make_unique<APVTS::SliderAttachment>(apvts, id("o1_oct"),  o1OctKnob.getSlider());
    o1ToneAttachment = std::make_unique<APVTS::SliderAttachment>(apvts, id("o1_tone"), o1ToneKnob.getSlider());
    o1FineAttachment = std::make_unique<APVTS::SliderAttachment>(apvts, id("o1_fine"), o1FineKnob.getSlider());
    o1PosAttachment  = std::make_unique<APVTS::SliderAttachment>(apvts, id("o1_pos"),  o1PosKnob.getSlider());

    o2OctAttachment  = std::make_unique<APVTS::SliderAttachment>(apvts, id("o2_oct"),  o2OctKnob.getSlider());
    o2ToneAttachment = std::make_unique<APVTS::SliderAttachment>(apvts, id("o2_tone"), o2ToneKnob.getSlider());
    o2FineAttachment = std::make_unique<APVTS::SliderAttachment>(apvts, id("o2_fine"), o2FineKnob.getSlider());
    o2PosAttachment  = std::make_unique<APVTS::SliderAttachment>(apvts, id("o2_pos"),  o2PosKnob.getSlider());

    xmodAttachment     = std::make_unique<APVTS::SliderAttachment>  (apvts, id("xmod"),  xmodKnob.getSlider());
    xmodModeAttachment = std::make_unique<APVTS::ComboBoxAttachment>(apvts, id("xmode"), xmodModeDropdown.getComboBox());
    mixAttachment      = std::make_unique<APVTS::SliderAttachment>  (apvts, id("mix"),   mixKnob.getSlider());

    fltTypeAttachment = std::make_unique<APVTS::ComboBoxAttachment>(apvts, id("flt_type"), fltTypeDropdown.getComboBox());
    fltCutAttachment  = std::make_unique<APVTS::SliderAttachment>  (apvts, id("flt_cut"),  fltCutKnob.getSlider());
    fltResAttachment  = std::make_unique<APVTS::SliderAttachment>  (apvts, id("flt_res"),  fltResKnob.getSlider());

    levelAttachment = std::make_unique<APVTS::SliderAttachment>(apvts, id("level"), levelKnob.getSlider());
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

    // Header strip divider matching mu-clid's header band above the RhythmCircle.
    g.setColour(MuLookAndFeel::colour(Id::segmentInactiveBorder));
    g.fillRect(0, s(32) - 1, getWidth(), 1);

    // Band labels per design-voice.md "Three stacked bands fill the main content
    // area." Drawn on the top-left of each band's outer border.
    auto drawBandLabel = [&](const juce::String& text, int y)
    {
        g.setColour(MuLookAndFeel::colour(Id::mutedText));
        g.setFont(juce::Font(juce::FontOptions{}.withHeight(sf(9.0f))));
        g.drawText(text, s(20), y - s(8), s(120), s(12),
                   juce::Justification::centredLeft, false);
    };
    drawBandLabel("Oscillators", band1Y);
    drawBandLabel("Filter",      band2Y);
    drawBandLabel("Gating",      band3Y);

    // Band outlines.
    g.setColour(MuLookAndFeel::colour(Id::segmentInactiveBorder));
    g.drawRect(juce::Rectangle<int>(s(12), band1Y, getWidth() - s(24), band1H), 1);
    g.drawRect(juce::Rectangle<int>(s(12), band2Y, getWidth() - s(24), band2H), 1);
}

void VoicePanel::resized()
{
    using mu_ui::s;
    const int w = getWidth();
    const int h = getHeight();
    const int pad      = s(16);
    const int headerH  = s(32);
    const int rowGap   = s(8);
    const int knobW    = s(80);
    const int knobH    = s(80);
    const int labelW   = s(48);
    const int ddH      = s(24);
    const int hgap     = s(8);
    const int bandInsetX = s(20);
    const int bandPadY   = s(16);
    const int modPanelH = s(280);   // modulator panel band height

    // ── Header strip ────────────────────────────────────────────────────────
    voiceTag.setBounds(pad, 0, w / 2 - pad, headerH);

    // Shared tonal-centre row (Root / Scale) — sits in the header band just
    // below the voice tag so the user always sees the active tuning.
    {
        int x = w / 2;
        const int y = (headerH - ddH) / 2;
        rootLabel   .setBounds(x, y, labelW, ddH);    x += labelW + s(4);
        rootDropdown.setBounds(x, y, s(56), ddH);     x += s(56) + s(12);
        scaleLabel  .setBounds(x, y, labelW, ddH);    x += labelW + s(4);
        scaleDropdown.setBounds(x, y, s(100), ddH);
    }

    int y = headerH + s(8);

    // ── Band 1: Oscillators (2 rows of 4 knobs + xmod row) ─────────────────
    band1Y = y;
    band1H = bandPadY + 2 * knobH + rowGap + ddH + bandPadY;
    int innerY = y + bandPadY;
    int innerX = bandInsetX + pad;

    // Row: Osc1 four knobs
    int x = innerX;
    for (auto* k : { &o1OctKnob, &o1ToneKnob, &o1FineKnob, &o1PosKnob })
    {
        k->setBounds(x, innerY, knobW, knobH);
        x += knobW + hgap;
    }

    // Row: Osc2 four knobs (immediately below)
    int innerY2 = innerY + knobH + rowGap;
    x = innerX;
    for (auto* k : { &o2OctKnob, &o2ToneKnob, &o2FineKnob, &o2PosKnob })
    {
        k->setBounds(x, innerY2, knobW, knobH);
        x += knobW + hgap;
    }

    // X-mod controls + mix knob to the right of the osc grid.
    int xmodX = innerX + 4 * (knobW + hgap) + s(24);
    xmodKnob.setBounds(xmodX, innerY, knobW, knobH);
    xmodLabel.setBounds(xmodX, innerY + knobH + s(4), labelW, ddH);
    xmodModeDropdown.setBounds(xmodX + labelW + s(4),
                                innerY + knobH + s(4), s(72), ddH);
    mixKnob.setBounds(xmodX + knobW + s(24), innerY, knobW, knobH);

    y += band1H + rowGap;

    // ── Band 2: Filter (type + cutoff + resonance + level) ─────────────────
    band2Y = y;
    band2H = bandPadY + knobH + bandPadY;
    innerY = y + bandPadY;
    innerX = bandInsetX + pad;

    fltTypeLabel   .setBounds(innerX, innerY + (knobH - ddH) / 2, labelW, ddH);
    fltTypeDropdown.setBounds(innerX + labelW + s(4),
                               innerY + (knobH - ddH) / 2, s(100), ddH);
    fltCutKnob.setBounds(innerX + labelW + s(4) + s(100) + s(16),
                          innerY, knobW, knobH);
    fltResKnob.setBounds(innerX + labelW + s(4) + s(100) + s(16) + knobW + hgap,
                          innerY, knobW, knobH);
    levelKnob .setBounds(w - bandInsetX - pad - knobW, innerY, knobW, knobH);

    y += band2H + rowGap;

    // ── Band 3: Gating designer (full-width strip) ─────────────────────────
    const int gateH = s(112);   // header (24) + grid (80) + slack
    band3Y = y;
    band3H = gateH;
    gatingDesigner.setBounds(pad, y, w - 2 * pad, gateH);

    y += gateH + rowGap;

    // ── Modulator panel — bottom band, full width ───────────────────────────
    const int modY = juce::jmax(y, h - modPanelH - pad);
    modulatorPanel.setBounds(pad, modY, w - 2 * pad,
                              juce::jmax(s(160), h - modY - pad));
}

} // namespace mu_tant
